#!/usr/bin/env bash
# Build Policenauts (PSXRecomp) as a native PS5 title: a PPSA folder with
# eboot.bin + sce_module/libc.prx + sce_sys/, launched from the home screen
# via ShadowMountPlus. Same pipeline as the Project Picori (TMC) PPSA:
#
#   game + runtime objects (CMake build-ps5, PS5 SDL2)
#   + native-app CRT / C++ runtime / mmap malloc / libc gap fillers + app_shims
#   -> prospero-lld with ps5-pie.ld -> llvm-pie.elf
#   -> ps5-native-tool link (SCE module) -> self --sign (FSELF) -> eboot.bin
#
# usage: ps5/package_app.sh
# env:   PS5_PAYLOAD_SDK  (default /opt/ps5-payload-sdk; PS5 SDL2 installed in
#                          target/user/homebrew, from ps5-payload-dev/SDL)
#        PS5_TITLE_ID     default PPSA77066
#        PS5_BUILD_DIR    default build-ps5 (CMake tree of this project)
set -euo pipefail

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
here="$repo/ps5"
native="$here/native-app"
export PS5_PAYLOAD_SDK=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
sdk=$PS5_PAYLOAD_SDK
sys="$sdk/target"
hb="$sys/user/homebrew"
cmake_build=$(mkdir -p "${PS5_BUILD_DIR:-$repo/build-ps5}" && cd "${PS5_BUILD_DIR:-$repo/build-ps5}" && pwd)
build="$cmake_build/app"
title_id=${PS5_TITLE_ID:-PPSA77066}

CC="$sdk/bin/prospero-clang"
CXX="$sdk/bin/prospero-clang++"
LLD="$sdk/bin/prospero-lld"
AR="$sdk/bin/prospero-ar"
TFLAGS=(-fno-plt -fno-stack-protector -ffunction-sections -fdata-sections)
MODULE_SDK=0x02000009
COMPANION_SDK=0x08050001
FSELF_MAGIC=0x1D3D154F

[[ $title_id =~ ^PPSA[0-9]{5}$ ]] || { echo "PS5_TITLE_ID must be PPSA + 5 digits" >&2; exit 2; }
[[ -f $hb/lib/libSDL2.a ]] || { echo "missing $hb/lib/libSDL2.a (build ps5-payload-dev/SDL)" >&2; exit 2; }
say() { printf '==> %s\n' "$*"; }

# 1. Game + runtime objects ---------------------------------------------------
if [[ ! -f $cmake_build/build.ninja ]]; then
    say "configuring"
    "$sdk/bin/prospero-cmake" -S "$repo" -B "$cmake_build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DPSX_SDL_BACKEND=SDL2 -DPSX_ENABLE_VULKAN=OFF -DCMAKE_VERBOSE_MAKEFILE=OFF >/dev/null
fi
say "compiling game + runtime"
# Build every object of psx-runtime and the static libraries it links, but not
# the payload-style executable itself (that link needs the payload crt).
ninja -C "$cmake_build" build.ninja >/dev/null   # regenerate first if CMakeLists changed
mapfile -t objs < <(ninja -C "$cmake_build" -t targets all | grep -oE '^CMakeFiles/psx-runtime\.dir/[^:]+\.o' | sort -u)
mapfile -t libs < <(ninja -C "$cmake_build" -t targets all | grep -oE '^[^:]+\.a:' | sed 's/:$//' | sort -u)
ninja -C "$cmake_build" "${objs[@]}" "${libs[@]}" | grep -E "error|FAILED" || true
for o in "${objs[@]}"; do [[ -f $cmake_build/$o ]] || { echo "object not built: $o" >&2; exit 1; }; done
mkdir -p "$build/obj" "$build/host" "$build/libc"
game_a="$build/obj/libpolicenauts_app.a"
rm -f "$game_a"
(cd "$cmake_build" && "$AR" rcs "$game_a" "${objs[@]}")
dep_libs=()
while IFS= read -r -d '' l; do dep_libs+=("$l"); done < <(
    find "$cmake_build" -path "$build" -prune -o -name '*.a' -print0 | sort -z)
((${#dep_libs[@]})) || { echo "no dependency archives found" >&2; exit 1; }

# 2. Host converter + clean-room libc.prx -------------------------------------
say "host converter"
tool="$build/host/ps5-native-tool"
zlib_a=$(ls /usr/lib/*/libz.a /usr/lib/libz.a 2>/dev/null | head -1 || true)
[[ -n $zlib_a ]] || { echo "host static zlib (libz.a, zlib1g-dev) is required" >&2; exit 2; }
if [[ ! -x $tool ]]; then
    clang++ -std=c++20 -O2 "$native/native_app_builder.cpp" "$native/self_container.cpp" \
        "$native/elf_object.cpp" "$native/sce_module_writer.cpp" "$zlib_a" -o "$tool"
    clang++ -std=c++20 -O2 "$native/libc_builder.cpp" -o "$build/host/libc-builder"
fi
"$build/host/libc-builder" "$native/runtime/api-surface.txt" "$native/runtime/imports.txt" \
    "$build/libc/libc.raw.elf" >/dev/null
"$tool" self --sign --in "$build/libc/libc.raw.elf" --out "$build/libc/libc.prx" >/dev/null
want=$(awk '{print $1}' "$native/runtime/libc.prx.sha256")
got=$(sha256sum "$build/libc/libc.prx" | cut -d' ' -f1)
[[ $got == "$want" ]] || { echo "libc.prx digest $got != pinned $want" >&2; exit 1; }

# 3. Native-app runtime objects -----------------------------------------------
say "native-app runtime"
"$CXX" -std=c++20 -O2 -fno-exceptions -fno-rtti "${TFLAGS[@]}" -c "$native/app_crt.cpp" -o "$build/obj/app_crt.o"
"$CXX" -std=c++20 -O2 -fno-exceptions -fno-rtti "${TFLAGS[@]}" -c "$native/app_cpp_runtime.cpp" -o "$build/obj/app_cpp_runtime.o"
"$AR" rcs "$build/obj/libpthread.a" "$build/obj/app_cpp_runtime.o"   # libc++'s recorded -lpthread
"$CC" -std=gnu11 -O2 -w "${TFLAGS[@]}" -c "$native/stubs/libc_ext.c" -o "$build/obj/libc_ext.o"
"$CC" -std=gnu11 -O2 -w "${TFLAGS[@]}" -c "$native/stubs/malloc_shim.c" -o "$build/obj/malloc_shim.o"
"$CC" -std=gnu11 -O2 -Wall "${TFLAGS[@]}" -c "$here/app_shims.c" -o "$build/obj/app_shims.o"
# The OpenGL renderer is compiled in but never selected (the PS5 SDL2 port has
# only the software presenter). Give every GL entry point it references a
# local no-op definition so no system GL module (libSceGLSlimVSH) is imported.
"$sdk/bin/llvm-nm" -u "$game_a" 2>/dev/null | awk '{print $NF}' | grep -E '^gl[A-Z]' | sort -u |
    awk 'BEGIN{print "/* generated by package_app.sh: GL never runs on PS5 */"} {print "void " $1 "(void) {}"}' \
    > "$build/obj/gl_stubs.c"
"$CC" -std=gnu11 -O2 -w "${TFLAGS[@]}" -c "$build/obj/gl_stubs.c" -o "$build/obj/gl_stubs.o"
echo "    $(grep -c '^void' "$build/obj/gl_stubs.c") GL entry points stubbed"

# 4. Link the intermediate PIE ------------------------------------------------
say "linking"
# Virtual working directory (see app_shims.c): chdir() is refused and
# getcwd() faults in a fake-signed title, so wrap the path-taking file API.
wraps=()
for f in open fopen freopen stat lstat mkdir rmdir unlink remove rename opendir \
         chmod truncate utimes statvfs access getcwd chdir realpath; do
    wraps+=(--wrap="$f")
done
log="$build/link.log"
if ! "$LLD" -T "$native/ps5-pie.ld" --eh-frame-hdr "${wraps[@]}" \
        --version-script "$native/app-symbols.map" --exclude-libs=ALL --error-limit=0 \
        -L "$build/obj" -e _start -o "$build/llvm-pie.elf" \
        "$build/obj/malloc_shim.o" "$build/obj/app_crt.o" "$build/obj/app_cpp_runtime.o" \
        "$build/obj/libc_ext.o" "$build/obj/app_shims.o" "$build/obj/gl_stubs.o" \
        --whole-archive "$game_a" --no-whole-archive \
        --start-group "${dep_libs[@]}" "$hb/lib/libSDL2.a" --end-group \
        "$sys/lib/libc++.a" "$sys/lib/libc++abi.a" "$sys/lib/libunwind.a" \
        --as-needed "$sys"/lib/*.so \
        --start-group "$sys/lib/libc.a" --end-group \
        2> "$log"; then
    grep -oE "undefined symbol: .*" "$log" | sort -u | head -60 || true
    tail -20 "$log"
    echo "link failed (full log: $log)" >&2
    exit 1
fi
"$sdk/bin/llvm-nm" -u "$build/llvm-pie.elf" 2>/dev/null | awk '{print $NF}' | sort -u > "$build/pie-imports.txt" || true

# 5. Convert + sign -----------------------------------------------------------
say "converting to PS5 module + signing"
"$tool" link --in "$build/llvm-pie.elf" --out "$build/eboot.elf" --stub-dir "$sys/lib" \
    --module-sdk "$MODULE_SDK" --companion-sdk "$COMPANION_SDK" --file-name eboot.elf
out="$build/dist/$title_id"
rm -rf "$build/dist"; mkdir -p "$out/sce_sys" "$out/sce_module"
"$tool" self --sign --in "$build/eboot.elf" --out "$out/eboot.bin" --magic "$FSELF_MAGIC"
cp "$build/libc/libc.prx" "$out/sce_module/libc.prx"

# 6. Title metadata -----------------------------------------------------------
python3 - "$here/param.json.in" "$out/sce_sys/param.json" "$title_id" <<'PY'
import json, sys
src, dst, tid = sys.argv[1:4]
p = json.load(open(src))
p["titleId"] = tid
p["conceptId"] = tid[4:]
p["contentId"] = f"UP9000-{tid}_00-POLICENAUTS00000"
p["localizedParameters"]["en-US"]["titleName"] = "Policenauts"
json.dump(p, open(dst, "w"), indent=2)
open(dst, "a").write("\n")
PY
cp "$here/icon0.png" "$out/sce_sys/icon0.png"
cp "$here/README-PPSA.txt" "$out/README.txt"
"$tool" self --inspect --file "$out/eboot.bin" | head -5
(cd "$build/dist" && rm -f "$title_id.zip" && zip -qr "$title_id.zip" "$title_id")
echo "Native title: $out"
echo "Folder ZIP:   $build/dist/$title_id.zip"

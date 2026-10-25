# Policenauts — PS5 native title (PPSA77066), source

Personal use only. The engine (psxrecomp) is PolyForm Noncommercial 1.0.0, and
`policenauts_recomp/generated/` is C translated from your own disc. 

## What's in here

| Folder | What it is |
|---|---|
| `policenauts_recomp/` | The game project: `game.toml`, `CMakeLists.txt`, function seeds, play captures, and `generated/` (recompiled game code plus the 53 overlays compiled ahead of time). |
| `policenauts_recomp/ps5/` | Everything specific to the PS5 build: `package_app.sh` (the whole build), `app_shims.c` (libc gaps, virtual working directory, keyboard/IME stubs), `ps5_game.c` (default game.toml and .cue files), `ps5_bios_blob.c` (embedded OpenBIOS), `native-app/` (fake-signed native-title toolchain from EVO-PLAYER via TMC), `param.json.in`, `icon0.png`, `README-PPSA.txt`. |
| `policenauts_recomp/overlay-captures/` | Overlay capture sets used to compile `generated/overlays_static*.c` (`aot_with6c.json` = what's in the build). Only needed to regenerate. |
| `psxrecomp/` | Framework (`dguillot-gh/psxrecomp-android`) at the `ps5-port` branch head. |
| `psxrecomp-ps5-port.diff` / `.log` | Only the PS5 changes to the framework, against base `58e9fd9`. Use this to move the port to a newer framework. |
| `sdl2-ps5/` | The patch for `ps5-payload-dev/SDL` (`release-2.30.x-ps5`; base commit in `BASE_COMMIT.txt`). It adds the raw DualSense accessor, the direct scan-out present path and on-screen-message overlays. |
| `mousebridge/` | Source for the mouse-bridge payload (`make` builds `mousebridge.elf`). |

## Framework changes, by file

- `runtime/src/ps5_platform.c` (new): startup — data folder `/data/policenauts`, log, virtual working directory, TMPDIR, default files, hiding the splash screen, synthesized arguments, exit through `LoadExec("exit")`.
- `runtime/src/main.cpp`, inside the `__PROSPERO__` blocks:
  - controller and touch pointer;
  - Options shortcuts and disc swap;
  - async present thread;
  - 5-second performance report;
  - `ps5_controls.ini`;
  - USB mouse through the mousebridge mailbox.
- Smaller FreeBSD/PROSPERO fixes: `dma.c`, `debug_server.c`, `traps.c`, `psx_fiber.c`, `runtime.cmake`.

## Build (Linux x86-64)

You need these first:
- The **ps5-payload-sdk** at `/opt/ps5-payload-sdk` (or set `PS5_PAYLOAD_SDK`).
- Host `clang++` (C++20), `ninja`, `cmake`, `python3`, `zip`, and `zlib1g-dev` (the build needs the static `libz.a`).

### 1. PS5 SDL2 (once per SDK)

```sh
git clone -b release-2.30.x-ps5 https://github.com/ps5-payload-dev/SDL.git ps5sdl
cd ps5sdl
git checkout ee4c47dc0d617b3bc8f35108f9956baf228a1322   # base the patch was made on
git apply ../sdl2-ps5/ps5-sdl2-psxrecomp.patch
cmake -S . -B build-ps5 -DCMAKE_TOOLCHAIN_FILE=$PS5_PAYLOAD_SDK/toolchain/prospero.cmake \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/user/homebrew \
      -DSDL_SHARED=OFF -DSDL_STATIC=ON
cmake --build build-ps5 -j
DESTDIR=$PS5_PAYLOAD_SDK/target cmake --install build-ps5
# -> $PS5_PAYLOAD_SDK/target/user/homebrew/lib/libSDL2.a
```

### 2. Game

```sh
cd policenauts_recomp
ln -s ../psxrecomp psxrecomp          # the project expects the framework here
bash ps5/package_app.sh
# -> build-ps5/app/dist/PPSA77066/ (eboot.bin, sce_module/libc.prx, sce_sys/) + PPSA77066.zip
```

`package_app.sh` does the following:
- configures CMake the first time;
- compiles the game and runtime;
- builds the host converter and the clean-room `libc.prx`, and checks it against the pinned sha256;
- links with the PS5 PIE script;
- converts the result to a PS5 module and fake-signs it.

Set `PS5_TITLE_ID` to build a different PPSA ID.

### 3. Mouse bridge payload

```sh
cd mousebridge && make PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # -> mousebridge.elf
```

## Regenerating `generated/` (only if you change the framework's codegen)

The recompiler fingerprints its output, so a stale `generated/` fails to build
with a fingerprint mismatch.

1. **Extract the boot EXE.** Copy `NAUTS\SLPS_002.15` from disc 1 to `disc/SLPS_002.15`, and put your `.cue`/`.bin` files in `disc/`.

2. **Build the recompiler and recompile the main executable:**

   ```sh
   cmake -S psxrecomp/recompiler -B psxrecomp/recompiler/build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build psxrecomp/recompiler/build
   psxrecomp/recompiler/build/psxrecomp-game --config game.toml
   ```

3. **Compile the overlays in ahead of time.** Without this step the game runs at half speed on PS5, because the overlays fall back to the interpreter.

   ```sh
   python3 psxrecomp/tools/compile_overlays.py --static \
       --captures overlay-captures/aot_with6c.json --game-toml game.toml \
       --recompiler psxrecomp/recompiler/build/psxrecomp-game \
       --runtime-include psxrecomp/runtime/include --out-dir generated/ --cps
   ```

   `aot_with6c.json` includes the 0x8006C000 overlay slot and leaves out 0x80012000 (see `aot_exclude.txt`).

4. **Rebuild** with `ps5/package_app.sh`.

## PS5 constraints this build works around

- **Linked system modules:** exactly the 7 NEEDED modules — kernel, LibcInternal, VideoOut, AudioOut, Pad, UserService, SystemService. Imports of anything else (libSceKeyboard, libSceMouse, libSceImeDialog) bind to NULL.
- **Runtime symbol lookups:** a fake-signed title can't look up system symbols; `dlsym` returns `0x80020003`. That's why the mouse goes through `mousebridge`.
- **Working directory and files:** `chdir` returns EPERM and `getcwd` faults, so the build provides a virtual working directory through `ld --wrap`.
- **No `/tmp`:** TMPDIR points at the data folder instead.
- **Exiting:** returning from `main` raises SIGSYS, so an atexit handler calls `sceSystemServiceLoadExec("exit")`.
- **System libraries:** the sandbox shows them under `/<sceKernelGetFsSandboxRandomWord()>/common/lib`, not under `/system`.

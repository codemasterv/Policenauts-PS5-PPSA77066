# Policenauts for PS5 (PPSA77066)

A native PlayStation 5 build of *Policenauts* (Konami, 1996, PS1): the Japanese release (SLPS-00215/00216) with Slowbeef's English patch v1.0. It installs as a home-screen title on a jailbroken PS5 through ShadowMountPlus.

> **Personal use only.** The engine is PolyForm Noncommercial 1.0.0, and `policenauts_recomp/generated/` is C translated from your own disc. 

## What it is

This is not an emulator. [PSXRecomp](https://github.com/mstan/psxrecomp) is a **static recompiler**: it translates the game's PS1 MIPS machine code into C ahead of time. That C is compiled for the PS5's x86-64 CPU and linked against a runtime that reproduces the PS1 hardware: GPU, SPU audio, CD-ROM, controller and memory-card ports, timers and DMA. The game runs as an ordinary native program, booting through the bundled open-source **OpenBIOS**, so no Sony BIOS is needed.

## Where it came from

| Piece | Source |
|---|---|
| Engine | [mstan/psxrecomp](https://github.com/mstan/psxrecomp), by way of [dguillot-gh/psxrecomp-android](https://github.com/dguillot-gh/psxrecomp-android) |
| Game project (config, function seeds, play captures, cursor addresses) | [dguillot-gh/policenauts-android](https://github.com/dguillot-gh/policenauts-android) |
| PS5 native-title toolchain (fake-signed PPSA) | EVO-PLAYER lineage, shared with the Trouble Makers (PPSA77064) and Project Picori (PPSA77065) ports |
| Graphics, audio and input | [ps5-payload-dev/SDL](https://github.com/ps5-payload-dev/SDL) `release-2.30.x-ps5`, with this project's patches |

## Architecture

```
your disc (.bin/.cue in /data/policenauts/disc)
        │  data streamed at runtime (CD-ROM emulation)
        ▼
┌───────────────────────────── eboot.bin (PPSA77066) ─────────────────────────────┐
│ recompiled game: SLPS_002.15 main EXE + 53 code overlays compiled ahead of time │
│ psxrecomp runtime: OpenBIOS (recompiled), GPU (software), SPU, CD, SIO, DMA     │
│ PS5 layer: startup, virtual cwd, controls, async present, disc swap, mailbox    │
│ SDL2 (PS5 port, patched) → VideoOut / AudioOut / Pad                            │
└─────────────────────────────────────────────────────────────────────────────────┘
        ▲ optional: mouse function addresses written in by…
mousebridge.elf (payload, kernel access) ── /data/mousebridge/request-<pid>.txt
```

### Build pipeline

1. **Recompile.** The PSXRecomp recompiler turns `SLPS_002.15` into about 60 C files.
2. **Compile the overlays in.** `compile_overlays.py --static --cps` compiles the 53 code overlays the game loads from disc, so they're part of the binary instead of running through the interpreter.
3. **Compile for PS5.** `prospero-clang` builds the game, runtime and patched SDL2 into object archives.
4. **Link.** `prospero-lld` links them with the PS5 PIE script and the native-app C runtime, a mmap malloc, libc gap fillers and `app_shims.c`. It also gets `--wrap` around about 20 file functions, for the virtual working directory.
5. **Make the PS5 module.** `ps5-native-tool` converts the ELF into a PS5 module with NID imports, then fake-signs it as `eboot.bin`. A clean-room `libc.prx`, checked against a pinned hash, goes in `sce_module/`.
6. **Package.** `param.json`, the icon and the README are added to make the PPSA folder.

`ps5/package_app.sh` runs all of it in one command.

## Install and play

1. Copy the `PPSA77066` folder to `/data/homebrew/` and let ShadowMountPlus register it.
2. Copy your disc images to `/data/policenauts/disc/` as `disc1.bin` (748,928,544 bytes) and `disc2.bin` (613,822,608 bytes). The `.cue` files and `game.toml` are written there on the first launch.
3. Launch **Policenauts** from the home screen.
4. *(Optional, for a USB/Bluetooth mouse)* Send `mousebridge.elf` to the ELF loader on port 9021, once per boot. You can do this before or after starting the game.

### Controls

The game is played with the PS1 mouse, so port 1 is emulated as a Sony Mouse.

| Input | Action |
|---|---|
| Touchpad | Absolute pointer: where your finger is on the pad is where the cursor goes; click to click |
| Left / right stick | Move the cursor (push further for faster) |
| D-pad | Step the cursor (tap for one step, hold to glide) |
| Cross / Circle | Left / right mouse button |
| Options + L1 / R1 | Insert Disc 1 / Disc 2 |
| Options + Square / Triangle | Save / load state |
| Options + L3 | FPS readout on/off |
| USB / Bluetooth mouse | Move and click (needs mousebridge; experimental) |

Settings are in `/data/policenauts/ps5_controls.ini`:

```ini
stick_speed = 1.0        # stick cursor speed
touch_pointer = on       # finger position = cursor position
usb_mouse = on           # use a USB/Bluetooth mouse through mousebridge
usb_mouse_speed = 1.0    # 2.0 = raw mouse counts; higher = faster
```

### Files on the console

```
/data/homebrew/PPSA77066/      eboot.bin, sce_module/libc.prx, sce_sys/{param.json,icon0.png}
/data/policenauts/
  disc/disc1.bin, disc2.bin    your images (cues auto-written)
  game.toml                    game config (auto-written; delete to restore)
  ps5_controls.ini             control settings
  bios/openbios.bin            written from the embedded copy
  saves/                       memory cards and save states
  psx_ps5.log                  log of the last run (5-second fps/present report)
/data/mousebridge/             payload lock, requests, mousebridge.log
```

## Problems solved along the way (B1–B15)

### Startup and environment

- **Native-title limits.**
  - `chdir` is refused (EPERM) and `getcwd` faults, so the build emulates a working directory (`ps5_set_virtual_cwd` plus the wrapped file functions).
  - There's no `/tmp`, so TMPDIR points at `/data/policenauts/tmp`.
  - Returning from `main` crashes with SIGSYS, so an exit handler calls `sceSystemServiceLoadExec("exit")`.
  - The keyboard and IME libraries don't link, so they're stubbed out.
  - The title links exactly 7 system modules: kernel, LibcInternal, VideoOut, AudioOut, Pad, UserService and SystemService.
- **Files the engine expects.**
  - The engine looks for its BIOS next to the executable, so OpenBIOS is embedded in the binary and written out on first launch.
  - `game.toml` and both `.cue` files are generated automatically.
- **Renderer.** The PS5 SDL2 port has only a software renderer. The original fallback check could never trigger (it tested a value that isn't a macro), so it was rewritten to ask for any available renderer.

### Performance: from half speed to a steady 60 fps

- **Overlays.** At first they ran through the dirty-RAM interpreter. They're now captured and compiled in ahead of time. A CMake argument-order bug that silently dropped them from the build also had to be fixed.
- **30 fps lock.** It came from SDL's simulated vsync waiting on top of the real flip wait. SDL's vsync is now off on PS5.
- **Scaling cost.** SDL's alpha-blended, format-converting scale took about 13 ms per frame. It was replaced by a separate present thread with a direct nearest-neighbour scale into the scan-out buffer, a tiled copy of only the changed areas, and "latest frame wins" queuing.
- **Result:**
  - the emulated game runs at 60.0 fps;
  - the scale takes about 1 ms and the tiled copy about 0.45 ms;
  - the rest of each frame is spent waiting for vblank;
  - audio is clean with a 2048-sample buffer.

### Input

- **Opening the controller.** With port 1 set as a mouse the engine never opens a controller, so the runtime opens one itself.
- **Raw DualSense access.** A patched SDL accessor (`SDL_PS5_GetPadExtra`) exposes the touchpad and buttons directly.
- **Touch steering.** The touch pointer steers by reading the game's own cursor position (RAM `0x80068928/2A`, from `game.toml` `mouse_cursor`) and clicks on arrival.
- **Shortcuts.** Options shortcuts have a 60-frame cooldown, so one press fires once.
- **Gyro removed.** The motion fields in SDL's pad struct are mislabelled, so the gyro read gravity.

### On-screen messages

Disc insert, save state, FPS and similar messages are drawn as overlays inside the async present path (`SDL_PS5_PresentPixelsOverlay`).

### USB/Bluetooth mouse: blocked, then a bridge

| Build | Attempt | Result |
|---|---|---|
| B10 | Load `libSceMouse` from `/system` | `0x80020002`: file not found (the sandbox hides `/system`) |
| B11 | Load from `/<sandbox word>/common/lib` | Loads, but no functions found |
| B12 | `sceKernelDlsym` by name and by NID, POSIX `dlsym` | All `0x80020003`; system modules hidden from the title |
| B13 | Link at startup (NEEDED import) | Starts fine, functions come back empty (same as the keyboard) |
| B15 | **mousebridge payload** | Built; not yet tested on hardware |

The bridge works like this:

1. The game loads the library and sets aside a mailbox in its memory for the function addresses (magic `MOUSEBR1`).
2. It writes a request file to `/data/mousebridge/`.
3. The payload, running with kernel access, finds the mouse functions inside the game's process (`kernel_dynlib_dlsym`) and writes their addresses into the mailbox (`kernel_proc_copyin`).
4. If the mouse still won't open, the game asks again and the payload raises its permissions one step at a time: caps first, then authid.

The full protocol is in `mousebridge/README.md`.

## Source layout

| Path | Contents |
|---|---|
| `policenauts_recomp/` | Game project: `game.toml`, `CMakeLists.txt`, seeds, play captures, and `generated/` (recompiled C, made from your own disc) |
| `policenauts_recomp/ps5/` | PS5 packaging: `package_app.sh`, `app_shims.c`, `ps5_game.c`, `ps5_bios_blob.c`, `native-app/` toolchain, `param.json.in`, `icon0.png`, `README-PPSA.txt` |
| `policenauts_recomp/overlay-captures/` | Overlay capture sets used to make `generated/overlays_static*.c` |
| `psxrecomp/` | Framework, `ps5-port` branch |
| `psxrecomp-ps5-port.diff` | Only the PS5 changes to the framework, against base `58e9fd9` |
| `sdl2-ps5/` | SDL2 patch (raw pad accessor, direct present and overlays, present statistics) and the commit it applies to |
| `mousebridge/` | Payload source, Makefile and the game-side client code |
| `BUILD-PS5.md` | Detailed build and regeneration instructions |

### Framework changes, by file

- **`runtime/src/ps5_platform.c`** (new): startup. It sets up the data folder, the log, the virtual working directory, TMPDIR, the default files, hiding the splash screen, the synthesized launch arguments, and the exit handler.
- **`runtime/src/main.cpp`**, inside the `__PROSPERO__` blocks:
  - controller and touch pointer;
  - Options shortcuts and disc swap;
  - async present thread;
  - 5-second performance report;
  - `ps5_controls.ini`;
  - mousebridge client.
- **`dma.c`, `debug_server.c`, `traps.c`, `psx_fiber.c`, `runtime.cmake`:** FreeBSD/PROSPERO fixes. These are `clock_gettime`, `INADDR_LOOPBACK` visibility, the HLE scheduler and fiber stub (no `makecontext`), and the PS5 source list.

## Building

You need these first (Linux x86-64):
- the [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) at `/opt/ps5-payload-sdk` (or set `PS5_PAYLOAD_SDK`);
- host `clang++` (C++20), `cmake`, `ninja`, `python3`, `zip` and `zlib1g-dev`.

### 1. Patched PS5 SDL2 (once per SDK install)

```sh
git clone -b release-2.30.x-ps5 https://github.com/ps5-payload-dev/SDL.git ps5sdl && cd ps5sdl
git checkout ee4c47dc0d617b3bc8f35108f9956baf228a1322
git apply ../sdl2-ps5/ps5-sdl2-psxrecomp.patch
cmake -S . -B build-ps5 -DCMAKE_TOOLCHAIN_FILE=$PS5_PAYLOAD_SDK/toolchain/prospero.cmake \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/user/homebrew \
      -DSDL_SHARED=OFF -DSDL_STATIC=ON
cmake --build build-ps5 -j
DESTDIR=$PS5_PAYLOAD_SDK/target cmake --install build-ps5
```

### 2. The game

```sh
cd policenauts_recomp
ln -s ../psxrecomp psxrecomp
bash ps5/package_app.sh
# -> build-ps5/app/dist/PPSA77066/ and PPSA77066.zip
```

Set `PS5_TITLE_ID=PPSAxxxxx` to build under another title ID.

### 3. mousebridge

```sh
cd mousebridge
make PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
# -> mousebridge.elf (send to port 9021)
```

`BUILD-PS5.md` covers regenerating `generated/` from your disc. You only need that after changing the recompiler.

## Status

| Area | State |
|---|---|
| Boot, both discs, disc swap | Working |
| Speed and audio | 60 fps, clean audio |
| Touchpad, sticks, D-pad, clicks | Working (touchpad a little sensitive) |
| Save states, memory card | Working |
| On-screen messages (B9+) | Built, not yet confirmed on hardware |
| USB/Bluetooth mouse via mousebridge (B15) | Built, not yet tested; use the fixed `mousebridge.elf` from `mousebridge-source.zip` |
| Keyboard | Not available (system library blocked for fake-signed titles) |

## Reusing it for other games

Everything below the game layer is generic: the package script, startup, present path, input handling and mousebridge. Another PSXRecomp game needs:
- its own `game.toml` and generated C;
- an overlay ahead-of-time compile pass;
- a new PPSA ID.

The games in [dguillot-gh/psx-android](https://github.com/dguillot-gh/psx-android) are the closest fits because they use the same framework fork: Einhänder, Parasite Eve, Persona, FF7 and others. Controller-only games skip all the mouse work. Games built on upstream psxrecomp need `psxrecomp-ps5-port.diff` moved onto their pinned framework version.

## Licenses

- **psxrecomp:** PolyForm Noncommercial 1.0.0.
- **OpenBIOS:** MIT.
- **SDL2:** zlib.
- **ps5-payload-sdk:** GPLv3 for the samples, other parts under their own licenses.
- **Policenauts:** © Konami. The English patch is by Slowbeef and the translation team.

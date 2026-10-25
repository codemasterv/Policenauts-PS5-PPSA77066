Policenauts (PSXRecomp) - PS5 native title
==========================================
Unofficial PS5 build of the PSXRecomp static recompilation of Policenauts
(SLPS-00215/00216, Slowbeef English patch v1.0). Personal use only: the
engine is PolyForm Noncommercial and this eboot.bin contains code translated
from your own disc. Do not share it.

INSTALL
  1. Copy this PPSA folder to /data/homebrew/ over FTP and let
     ShadowMountPlus register it (same as Trouble Makers / Project Picori).
  2. Copy your two disc images to /data/policenauts/disc/ and rename them:
        disc1.bin   (Disc 1, 748,928,544 bytes)
        disc2.bin   (Disc 2, 613,822,608 bytes)
     The .cue files and game.toml are written there automatically on the
     first launch; you only supply the .bin files.
  3. Launch "Policenauts" from the home screen.

CONTROLS (DualSense) - the game is played with the PS1 mouse
  Touchpad              finger position = cursor position; click to click
  Left or right stick   move the cursor (push further = faster)
  D-pad                 step the cursor (tap = one step, hold = glide)
  Cross                 left mouse button (select / talk / advance)
  Circle                right mouse button (cancel / back)
  Options + L1          insert Disc 1
  Options + R1          insert Disc 2   (when the game asks for it)
  Options + Square      save state
  Options + Triangle    load state
  Options + L3          FPS readout on/off
  USB / Bluetooth mouse  moves the cursor; left/right buttons click.
                        Needs the mousebridge payload (see below).
  Settings: /data/policenauts/ps5_controls.ini (stick_speed, touch_pointer,
  usb_mouse, usb_mouse_speed).

MOUSE BRIDGE (experimental)
  A homebrew title is not allowed to use the system mouse library on its
  own. mousebridge.elf is a small payload that does the lookup for it.
  Send it once per boot to the ELF loader (port 9021), before or after
  starting the game - a "Mouse bridge running" notification confirms it,
  and "USB mouse ready" appears in game once linked.
  Log: /data/mousebridge/mousebridge.log (send it with psx_ps5.log).
  In-game saves go to the virtual memory card in /data/policenauts/saves/.

FILES IN /data/policenauts
  game.toml      game configuration (delete it to get the default back)
  disc/          disc1/disc2 .cue (auto) + your .bin files
  saves/         memory cards and save states
  psx_ps5.log    log of the last run - send this with any bug report

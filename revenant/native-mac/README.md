# Revenant: native Apple Silicon port

A native arm64 macOS build of the GOG version of *Revenant* (Cinematix
Studios, 1999). It does not use Wine or Rosetta. The game's own x86 code
(`Revenant.exe`) is statically recompiled to arm64 with
[M-HT/SR](https://github.com/M-HT/SR), and the shared native layer in
[`../../common/native-mac/runtime`](../../common/native-mac/README.md) replaces the Windows APIs.

Game data is not included. This repository never contains game files or
code generated from them.

## Status

Tested with scripted input (`REVENANT_SCRIPT`) and screenshots of the game's
own frames:

| Area | Result |
|------|--------|
| Intro videos (Smacker, 640x240 interlaced, 44.1 kHz stereo sound) | Play. Escape skips them. |
| Main menu, mouse, keyboard | Work. |
| New game: the Keep, walking, talking (dialog choices) | Work. |
| Speech (MP3 samples in `resources.rvr`) | Plays (decoded with macOS AudioToolbox). Not checked by ear. |
| 3D characters (Direct3D 6 on the software rasterizer) | Drawn with smooth light, as with a 3D card (see "3D renderer" below). The light and shade on the figures match Wine in Direct3D mode (`Software3D=No`). (Before 2026-10-05 the point lights, for example the inventory light and the torches, were almost zero: the figures were flat, and the inventory figure looked stocky.) |
| Text (GDI fonts on surfaces: dialogs, messages, stats) | Drawn with the macOS fonts of the same name (Times New Roman, Arial) and their hints. Smooth edges at the sizes where the font asks for them (its "gasp" table), as Wine and GDI with font smoothing do: the dialog text has smooth edges. |
| 3D figure on the inventory screen | Clean when it moves. (Before 2026-10-05 it left old poses on the screen: the game repaints the screen area that Direct3D reports in `GetClipStatus`, and the port reported no area.) |
| Mouse cursor | Only the game cursor shows (the Mac cursor is hidden). |
| Load game | Works (a GOG save in Misthaven loads). |
| Save game into a new slot | Works (typed name, `Save/Single/<name>` is written). |
| Music (the GOG `Music/TrackNN.ogg` files as CD tracks) | Starts with the game. Not checked by ear. |
| Frame rate | About 25 frames per second in the Keep, at about 20% of one CPU core. |
| Process | arm64 only (no Rosetta). |

Not tested yet: combat, spells, later areas, and long play sessions.

3D renderer: the GOG `revenant.ini` has `Software3D=Yes`. With it, the game
draws the 3D figures with its own software renderer ("Blue"), not with
Direct3D. Blue lights a figure in 32 steps from one light and a dark ambient
light, so the sides that face away from the light are black. Wine gives the
same picture: a Wine screenshot of the new-game scene matches this port's
Blue picture (no pixel differs by more than 40 of 255). This port reads
`Software3D` as `No` (`GAME_INI_OVERRIDES` in `runtime/game.h`), so the game
draws the figures with Direct3D, on the port's renderer. The value in
`revenant.ini` stays as it is.

Known limits:

- Multiplayer is off: DirectPlay finds no service providers.
- Joysticks are not available (DirectInput lists no joystick).
- The 3D renderer is software only. It implements the parts of Direct3D 6
  that the game uses; other games may need more.
- The intro videos show black lines between the video lines, as the original
  player does for interlaced Smacker files.

## Play

1. Install the GOG game. The default folder is `~/Games/Revenant/Game Data`
   (the folder with `resources.rvr`). Another folder: set `REVENANT_DATA`.
2. Build the program and the app bundle (see below), then open
   `Revenant (Native).app`.

The game starts in full screen at the size of your screen (the 640x480
picture is scaled, with its shape kept). Cmd+Return or Alt+Return switches
between full screen and a window. To start in a window, put
`Display_Mode=window` in `Revenant-native.cfg` in the game folder
(`desktop` is the default; `fullscreen` changes the display mode). The
game's own `Windowed` setting in `revenant.ini` has no effect in this port.

Saves go to `Save/` in the game folder, as in the Windows version. The game
keeps the current map in `Curmap/` and its settings in `revenant.ini`.

## Build

From the repository root (once): `conda env create -f environment.yml`,
`. common/native-mac/tools/env.sh`, `common/native-mac/tools/install-ldc.sh`. Then:

```sh
cd revenant/native-mac
. ../../common/native-mac/tools/env.sh
make tools                       # once for all games
make                             # reads Revenant.exe from ~/Games/Revenant/Game Data
../../common/native-mac/macos/make-bundle.sh   # build/Revenant (Native).app
```

`make GAME_DIR=<folder>` reads the exe from another folder.

## Port notes

These findings are specific to the GOG `Revenant.exe` version 1.22
(sha256 `28bec273...72b5`, `GAME_EXE_SHA256` in `game.conf`; the build
stops for another version). They are in `srw/`:

- `srw/data_in_text.txt`: the DirectInput data format `c_dfDIJoystick` is
  linked into `.text` at `0x58a3b0`.
- `srw/jump_tables.txt`: the MSVC `memcpy`/`memmove` jump tables, and the
  switch at `0x461b34`.
- `srw/not_relocations.txt`: `push 0x600000` at `0x45b698` is surface flags,
  not an address in `.data`.
- `srw/llasm/instruction_replacements.sci`: four jumps into the middle of an
  instruction (`0x4b0f3d`, `0x4b0fa2`, `0x4b59c5`, `0x4b5a2a`), two `and`
  with a code address, two `adc ah`, the `memcpy` tail entries, and the
  broken multiplayer switch at `0x46190d`.
- The MMX code (`0x43c...`) is not translated (139 traps). The game uses it
  only when CPUID reports MMX, and the runtime reports no MMX.
- The game asks for DirectX 6: it gets IDirectDraw4 from DirectDraw 1 by
  QueryInterface, and IDirect3D3 from IDirectDraw4. With `Software3D=No`
  (the port's value) it picks the hardware (HAL) device and draws with
  DrawPrimitive. With `Software3D=Yes` it picks the RGB software device, but
  draws with its own renderer and calls Direct3D only for lights and
  materials.
- Point lights: the game gives D3DLIGHT2 lights with attenuation values
  for the Direct3D 6 rule (inventory light: 0.1, 0.8, 1.0). Direct3D 6 and
  older use the distance as a part of the range (1 at the light, 0 at the
  range) and multiply the light by the sum. Direct3D 7 divides by the sum
  of the real distance, which makes these lights almost zero. The port uses
  the Direct3D 6 rule, as Wine does for these versions.
- The game gives a D3DVIEWPORT with `dvMinZ` = `dvMaxZ` = 0. Direct3D 6 and
  Wine ignore these two values for a D3DVIEWPORT, so the depth range stays
  0..1.
- The speech is MP3 data in `resources.rvr`. The game plays it through
  `AIL_set_named_sample_file` with the suffix `.mp3` (Miles decodes it with
  `mp3dec.asi`).
- The game reads `GetDeviceIdentifier` into a 1064-byte DirectX 6 structure
  on its stack (a larger write breaks its return address).
- The game draws its interface text with GDI on surfaces with
  `DDSCAPS_OWNDC`, and sizes those surfaces from `GetTextMetrics`.

## License and credits

- M-HT/SR: Copyright (C) Roman Pauer, MIT license.
- The files in this folder: MIT license (see [LICENSE](../../LICENSE)).
- *Revenant* is a game by Cinematix Studios, published by Eidos Interactive.
  You need your own copy.

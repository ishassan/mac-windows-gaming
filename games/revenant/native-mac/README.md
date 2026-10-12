# Revenant: native Apple Silicon port

A native arm64 macOS build of the GOG version of *Revenant* (Cinematix
Studios, 1999). It does not use Wine or Rosetta. The game's own x86 code
(`Revenant.exe`) is statically recompiled to arm64 with
[M-HT/SR](https://github.com/M-HT/SR), and the shared native layer in
[`../../../common/native-mac/runtime`](../../../common/native-mac/README.md) replaces the Windows APIs.

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
| Speech and sound effects (MP3 speech in `resources.rvr`, WAV effects) | Play (MP3 decoded with macOS AudioToolbox). Tested 2026-10-11 with traces: in a conversation that starts 150 s after a save loads, each speech line starts, is decoded (for example 723456 bytes) and plays to its end. Before 2026-10-11 all sound stopped after about one minute of play, so the conversations had text and no speech (see "Port notes"). Not checked by ear. |
| 3D characters (Direct3D 6 on the software rasterizer) | Drawn with smooth light, as with a 3D card (see "3D renderer" below). The light and shade on the figures match Wine in Direct3D mode (`Software3D=No`). (Before 2026-10-05 the point lights, for example the inventory light and the torches, were almost zero: the figures were flat, and the inventory figure looked stocky.) |
| Text (GDI fonts on surfaces: dialogs, messages, stats) | Drawn with the macOS fonts of the same name (Times New Roman, Arial) and their hints. Smooth edges at the sizes where the font asks for them (its "gasp" table), as Wine and GDI with font smoothing do: the dialog text has smooth edges. |
| Message log and dialog text | Clean (tested 2026-10-11: each line of the message log matches the game's text surface pixel for pixel, and the exit dialog reads "and return to Windows?"). The game copies its GDI text into textures and draws them as Direct3D quads. Before 2026-10-11 some rows and columns of these textures were doubled and others were missing ("Locke entered" read "Luuku ulurud"), see "Port notes". |
| 3D figure on the inventory screen | Clean when it moves. (Before 2026-10-05 it left old poses on the screen: the game repaints the screen area that Direct3D reports in `GetClipStatus`, and the port reported no area.) |
| Mouse cursor | Only the game cursor shows (the Mac cursor is hidden). |
| Load game | Works (a GOG save in Misthaven loads). |
| Save game into a new slot | Works (typed name, `Save/Single/<name>` is written). Since 2026-10-11 the save also gets its picture (`ss.bmp`), and the Load Game menu shows it (tested 2026-10-11). Saves made with the port before 2026-10-11 have no picture: when one is selected, the menu keeps the picture of the save selected before it. |
| Music (the GOG `Music/TrackNN.ogg` files as CD tracks) | Starts with the game. Not checked by ear. |
| Frame rate | About 25 frames per second in the Keep, at about 20% of one CPU core. |
| Process | arm64 only (no Rosetta). |

Not tested yet: combat, spells and later areas. Long play: three 15-minute
scripted walks with fights ran without a crash (2026-10-12, after the
`memcpy` fix in "Port notes").

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

1. Install the GOG game. The default folder is
   `~/Games/Revenant/Original Game Files` (the folder with `resources.rvr`;
   layout: the top README). Another folder: set `REVENANT_DATA`.
2. Build the program and the app bundle (see below), then open
   `Revenant (Native).app`.

The game starts in full screen at the size of your screen (the 640x480
picture is scaled, with its shape kept). Cmd+Return or Alt+Return switches
between full screen and a window. To start in a window, put
`Display_Mode=window` in `native-mac/Settings/Revenant-native.cfg` of the game folder
(`desktop` is the default; `fullscreen` changes the display mode). The
game's own `Windowed` setting in `revenant.ini` has no effect in this port.

The game writes its saves to `Save/`, the current map to `Curmap/` and its
settings to `revenant.ini`, all in its own folder. The port sends them
elsewhere (`GAME_PATH_REDIRECTS` in `runtime/game.h`): the saves go to
`Saves/` of the game folder, next to `Original Game Files` (the Wine versions
use the same folder), and `revenant.ini`, `Curmap/`, `revboot.log` and
`ss.bmp` go to `native-mac/Settings/`. The original game files stay as they
are.

If the game crashes, the report is added to `native-mac/Settings/crash.log`
(with the date). Send that file with a bug report.

## Build

From the repository root (once): `conda env create -f environment.yml`,
`. common/native-mac/tools/env.sh`, `common/native-mac/tools/install-ldc.sh`. Then:

```sh
cd games/revenant/native-mac
. ../../../common/native-mac/tools/env.sh
make tools                       # once for all games
make                             # reads Revenant.exe from ~/Games/Revenant/Original Game Files
../../../common/native-mac/macos/make-bundle.sh   # build/Revenant (Native).app
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
  with a code address, two `adc ah`, the `memcpy` tail entries, the
  `memcpy` backward-copy entries (see below), and the broken multiplayer
  switch at `0x46190d`.
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
- The game plays some empty WAV files (196 bytes, a `data` chunk of 0
  bytes, made with "WaveConvertPro"), about once every few seconds while
  Locke is in a room. It has 15 sample handles and uses only a handle
  whose status is "done". Until 2026-10-11 the mixer marked an empty sound
  as playing and never ended it (it skips a channel with no frames), so
  each empty WAV blocked one handle for ever. A trace on 2026-10-11 showed
  all 15 handles blocked 85 s after the "talk" save loaded; after that the
  game started no sound at all, and a conversation showed its text with no
  speech. Since 2026-10-11 a sound with no frames ends at once
  (`Mixer_Start` in `audio-mixer.c`), the end-of-sample callback comes,
  and the game uses the handle again.
- The save picture: the game writes `ss.bmp` into its folder (the port
  sends it to `native-mac/Settings`), then checks
  `FindFirstFileA(".\ss.bmp")` and copies the file with the name from
  the search result into the save folder. Until 2026-10-11 two things
  broke this: the redirect did not match `./ss.bmp` (the check failed and
  the game did not copy the picture), and a search with no wildcard gave
  the whole found path (`../native-mac/Settings/ss.bmp`), not only the
  file name as Windows does (the copy then failed, and the game tried
  again without end). Since 2026-10-11 `CLIB_FindFile` removes `./` parts
  before the redirect, and `FindFirstFileA` gives only the file name.
- The crash of 2026-10-12 (the game closed during play, exit code 138 =
  128 + SIGBUS): the game's `memcpy` (`0x58b780`) and `memmove`
  (`0x592230`) copy 4 to 28 bytes backward with `jmp [ecx*4 + table]`, with
  `ecx` from -7 to 0 (`UnwindDownVec`). In the translated data, SRW puts the
  table entries -7..-1 2 bytes too low (it aligns the label of entry 0, at
  `0x58ba20` and `0x5924b0`), so the jump read half of one address and half
  of the next one. The game uses these copies, for example, to insert into a
  sorted list. Since 2026-10-12 the two jumps compare `ecx` with each index
  and jump to the correct code without the table
  (`srw/llasm/instruction_replacements.sci`; the targets match the tables
  in the exe). Test 2026-10-12: three 15-minute scripted walks with fights
  on copies of the saves, in background mode. Before the fix, one of them
  crashed in less than 2 minutes; after the fix, all three ran to the end.
- The game reads `GetDeviceIdentifier` into a 1064-byte DirectX 6 structure
  on its stack (a larger write breaks its return address).
- The game draws its interface text with GDI on surfaces with
  `DDSCAPS_OWNDC`, and sizes those surfaces from `GetTextMetrics`.
- The message log and the dialogs are textured quads: the game draws a
  line with GDI on a 640x12 surface, copies it into 144x144 textures (12
  rows for each line), and draws them with `DrawPrimitive` (pre-transformed
  vertices at whole pixel positions, texture coordinates plus half a texel,
  for example v = 108.5 / 144 at y = 444). Direct3D 6 puts the pixel
  centers at whole coordinates, so each pixel samples a texel center. The
  port's rasterizer used centers at x + 0.5 and y + 0.5 until 2026-10-11,
  which sampled exactly on the edges between texels: rounding then doubled
  some rows and columns and dropped others. Since 2026-10-11 it uses the
  Direct3D rule (`raster_triangle` in `WinApi-d3d.c`).

## License and credits

- M-HT/SR: Copyright (C) Roman Pauer, MIT license.
- The files in this folder: MIT license (see [LICENSE](../../../LICENSE)).
- *Revenant* is a game by Cinematix Studios, published by Eidos Interactive.
  You need your own copy.

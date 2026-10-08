# Command & Conquer Generals Zero Hour: native port (recompile)

A native arm64 macOS build of Zero Hour made the same way as our Commandos
and Revenant ports: the game's own x86 program (`game.dat`, patch 1.04) is
statically recompiled to arm64 with [M-HT/SR](https://github.com/M-HT/SR),
and the shared native layer in
[`../../../../common/native-mac/runtime`](../../../../common/native-mac/README.md)
replaces the Windows APIs. It uses no Wine, no Rosetta and no source code of
the game. Direct3D 8 goes to DXVK (Direct3D to Vulkan) and MoltenVK (Vulkan
to Metal), both native arm64 libraries.

It started as a pilot (done 2026-10-07); the record of the work (time,
problems, "source assists") is in [`PILOT-LOG.md`](PILOT-LOG.md). The
community port GeneralsX is in [`../../open-source-port`](../../open-source-port/README.md).

Game data is not included. This repository never contains game files or
code generated from them.

## Status (2026-10-07)

Tested with scripted input (`GENERALSZH_SCRIPT`) and screenshots of the
game's own frames:

| Area | Result |
|------|--------|
| Start-up, intro movies (Bink), menus | Work. Escape skips the trailer (not the EA logo, as on Windows). |
| 3D menu scene (shell map) | Draws with the correct ground, water, units and trees. |
| Menu music, sound effects | Play (MP3 music from the game archives, Miles 6.5 calls). Not checked by ear. |
| Mouse and keyboard in menus and in the game | Work. |
| Skirmish against the AI | 10 minutes with no crash: units train, money and tooltips work, the AI attacks. 30 frames per second (the game's limit). |
| Campaign | The USA campaign starts: briefing movie with sound, then the mission. |
| Save and load | A save of the Windows version (Wine 8) loads in this port, and a save of this port loads in Wine 8. |
| Speed (menu scene, 1280x800) | 0.25 CPU cores at 31 frames per second (window; 0.26 offscreen). Community port: 0.46 cores at about 70 fps; Wine 8: 2.22 cores at 21.8 fps. |
| Process | arm64 only. |

Known problems:

- Multiplayer is off: sockets fail. The network start succeeds only so that
  the skirmish player name ("Player") works.

## Play

1. The game data is the EA app copy in `~/Games/Command and Conquer
   Generals Zero Hour/Original Game Files/` (the Zero Hour folder and the
   base game folder next to it; layout: the top README). Another folder:
   set `GENERALSZH_DATA`.
2. Build the program and the app bundle (see below), then open
   `Command & Conquer Generals Zero Hour (Native).app`.

My Documents (options, maps, replays, the DXVK shader cache) is
`other-versions/native-mac/Settings/` of the game folder, and the saves are the shared
`Saves/Zero Hour/` (`GAME_PATH_REDIRECTS` in `runtime/game.h`). The save
format is the one of the Windows version (the same exe), so the Wine
versions use the same saves. This port also loads saves of the community
port GeneralsX that have 4 bytes for each text character (GeneralsX before
2026-10-08; see "Port notes"). Another My Documents: `GENERALSZH_DOCUMENTS`
(the saves are then in its own `Save` folder).

The first start (no `Options.ini` yet) takes about one minute with no
window: the game runs its own speed test (memory copy loops timed with
`clock`) to set the detail level. On this Mac it sets Medium; change it in
the options menu. Later starts skip the test.

## Build

From the repository root (once): `conda env create -f environment.yml`
(FFmpeg for Bink is in it), `. common/native-mac/tools/env.sh`,
`common/native-mac/tools/install-ldc.sh`. The DXVK headers and libraries
come from the community port build (`../../open-source-port/make-app.sh`). Then:

```sh
cd games/command-and-conquer-generals-zero-hour/other-versions/native-mac
. ../../../../common/native-mac/tools/env.sh
make tools                       # once for all games
make                             # reads game.dat from the Wine game folder; about 10 minutes
../../../../common/native-mac/macos/make-bundle.sh   # build/Command & Conquer Generals Zero Hour (Native).app
```

The app is about 150 MB: FFmpeg from conda brings its dependencies.

## Debug options

Environment variables (prefix `GENERALSZH_`):

| Variable | Effect |
|---|---|
| `DATA`, `DOCUMENTS` | Other folders for the game data and the saves. |
| `DXVK_DIR` | Load DXVK, the Vulkan loader and MoltenVK from this folder (default: the app's `Frameworks`). |
| `SCRIPT` | Scripted input (see `common/native-mac/runtime/input-script.c`). |
| `BACKGROUND=offscreen` | No window and no focus: SDL's offscreen driver (Vulkan headless surfaces), no sound. Screenshots still work. |
| `DUMP_TEXTURES` | Save each texture that the game fills (16- and 32-bit formats) as a BMP file there. |
| `DUMP` | Save every 30th frame and the script's `shot` frames as BMP files there. |
| `TRACE_D3D8` | 1: failed calls; 2: every call; 3: every call with its arguments. |
| `TRACE_SOUND`, `TRACE_VIDEO`, `TRACE_FILES`, `TRACE_REG`, `TRACE_EH`, `TRACE_LAG` | Traces of Miles, Bink, files, registry, C++ exceptions, frame times. |

## Port notes

These are for the EA app `game.dat` 1.04 (sha256 in `game.conf`; the build
stops for another file):

- Save files: `XferLoad::xferUnicodeString` (`0x602110`) reads 2 bytes for
  each character of a text (wchar_t on Windows). GeneralsX wrote 4 bytes
  for each character before 2026-10-08 (wchar_t on macOS), so its saves gave
  "Error loading game". `srw/llasm/instruction_replacements.sci` changes the
  read call at `0x602139` into a call of `runtime/llasm/xfer-unicode.c`,
  which finds the width of each file at its first text (as the GeneralsX
  reader does) and gives the game 2-byte characters. Tested 2026-10-08: the
  load list shows the real names of the GeneralsX saves, and the newest one
  loads. The text width was the only difference in that save. The game
  still writes 2 bytes for each character.
- `srw/data_in_text.txt`: the DirectInput keyboard data format at
  `0x7d7730` is in `.text`.
- `srw/llasm/instruction_replacements.sci`: four `cmp [list], list` checks,
  two spin locks (`lock bts`), one load of a code address, and two size
  checks in the shadow buffer manager. In 1.04 a mesh with too many shadow
  polygons reads past the table of buffer slots (the menu scene crashed
  after 75 s); now it gets no slot and no shadow, as in the later source.
- `GEN_RELOCS_OPTIONS=--strict-data-code` in `game.conf`: the relocation
  finder accepts code pointers in data only at function starts (the big
  `.text` holds many numbers that look like code addresses).
- The game measures the CPU speed with `cpuid` and `rdtsc`, and turns the
  3D menu scene off under 600 MHz. The runtime reports an Intel CPU with a
  3 GHz time stamp counter and without MMX, 3DNow! or SSE, so the game
  takes its x87 code paths (SRW does not translate the SIMD code).
- The game keeps the pointer of a system-memory surface lock after the
  unlock (the shroud), and some code ignores the lock pitch (the tree
  texture). The Direct3D 8 layer handles both.
- The D3DX box filter in the exe rounds with `lea ecx, [ecx + edx +
  0x800080]`, and D3DX tables hold the same number. The strict relocation
  rules keep these constants (otherwise the mip levels of the tree texture
  are garbage).
- The Miles end-of-sample callbacks run on the main thread (from `Sleep`
  and `PeekMessageA`): the game's callback changes the audio manager's
  lists, which the main loop uses at the same time.
- SDL's offscreen driver has no display modes. The Direct3D 8 layer then
  reports usual desktop modes, so an offscreen test makes the same device
  (32-bit color, stencil buffer, volume shadows) as a windowed run.
- The frame limiter calls `Sleep(0)` in a loop. The runtime makes repeated
  `Sleep(0)` calls sleep 0.2 ms, which takes the CPU use from 1.04 to 0.26
  cores at the same frame rate.

## License and credits

- M-HT/SR: Copyright (C) Roman Pauer, MIT license.
- DXVK (zlib license) and MoltenVK (Apache 2.0) come from the community port
  build; FFmpeg (LGPL 2.1 or later) comes from conda-forge.
- The files in this folder: MIT license (see [LICENSE](../../../../LICENSE)).
- *Command & Conquer Generals Zero Hour* is a game by Electronic Arts. You
  need your own copy.

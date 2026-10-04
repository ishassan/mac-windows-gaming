# Commandos: Behind Enemy Lines: native Apple Silicon port

A native arm64 macOS build of the GOG version of *Commandos: Behind Enemy Lines*.
It does not use Wine or Rosetta. The game's own x86 code (`comandos.exe`) is
statically recompiled to arm64 with [M-HT/SR](https://github.com/M-HT/SR)
(SRW, llasm, LLVM). The shared native layer in
[`../common/runtime`](../common/README.md) (partly based on the SR Septerra
Core port, MIT license) replaces the Windows APIs with SDL2.

Game data is not included. This repository never contains game files or
code generated from them. All of that is in the ignored `build/` folder.

## Status

Single-player works. These items were tested with scripted input
(`COMMANDOS_SCRIPT`) and screenshots of the game's own frames:

| Area | Result |
|------|--------|
| Intro video (Cinepak AVI with MS ADPCM sound) | Plays. Escape skips it. |
| Main menu, new game menu, mouse and keyboard | Work. |
| Campaign video, briefing (text and voice files) | Play. |
| Mission 1 | Plays: the camera tour, select a commando, walk, Ctrl+B briefing. |
| Frame rate | 20 frames per second in the mission (the game's own limit is 50 ms per frame). |
| In-game menu (Escape), Save Game into a slot | Works. The file `SAVE0000.SAV` is written. |
| Quick save (Ctrl+S) and quick load (Ctrl+L) | Work. `QLOAD.SAV` is written and read back. |
| Music (WAV streams) and sound effects (Miles samples) | Reach the mixer. Not checked by ear. |
| Stress test (3 minutes of random input) | No crash. |
| Exit from the menu | Clean (exit code 0). |
| Process | arm64 only, not translated (no Rosetta). |

Not tested yet: the full campaign, the tutorials, and all later missions.

Known issues:

- In the in-game menu (Escape), the game ignores the first mouse click. The
  second click works. The keyboard (arrows and Return) works at once. It is
  not known if the Windows version does the same.
- The game uses GDI text only for the developer overlay ("FPS", screen
  mode) that `.DEVELOP 1` in `OUTPUT/Comando.cfg` turns on. The shared text
  layer draws GDI text now, but this overlay was not tested.
- Multiplayer is off (the network functions fail on purpose).
- CD audio (Miles "redbook" functions) is off. The GOG version plays music
  from WAV files, which work.

## Play

1. Install the GOG game data. The default folder is
   `~/Games/Commandos Behind Enemy Lines/Game Data` (the folder with `WARGAME.DIR`).
   Another folder: set `COMMANDOS_DATA`, or give the folder as the first argument.
2. Build the program and the app bundle (see below), then open
   `Commandos Behind Enemy Lines (Native).app`.

The game starts in full screen at the size of your screen (the 640x480
picture is scaled, with its shape kept). Cmd+Return or Alt+Return switches
between full screen and a window. To start in a window, put
`Display_Mode=window` in `Commandos.cfg` in the game folder.

Saves go to `User/Pyro Studios/Commandos/OUTPUT/` in the game folder.

## Build

From the repository root (once): `conda env create -f environment.yml`,
`. common/tools/env.sh`, `common/tools/install-ldc.sh`. Then:

```sh
cd commandos-native
. ../common/tools/env.sh
make tools                                   # once for all games
make                                         # reads comandos.exe from the game folder
./build/Commandos
../common/macos/make-bundle.sh [output folder]   # default output folder: build
```

All build tools come from the repository's conda env `mac-windows-gaming`
(`../environment.yml`). The LDC D compiler is not on conda-forge:
`common/tools/install-ldc.sh` puts the official release into the env folder.
`make` reads `comandos.exe` from `GAME_DIR` (default: the folder in
`game.conf`) and writes everything it makes into `build/`. `make` stops if
`CONDA_PREFIX` is not set (run `. ../common/tools/env.sh` first), so that
SDL2 always comes from the env.

`make-bundle.sh` copies the program, SDL2 (sdl2-compat) and SDL3 into
the bundle, makes the icon from the GOG icon in the game folder, and signs
the bundle ad hoc (`codesign -s -`).

## Debug options

Environment variables for `build/Commandos`:

| Variable | Effect |
|----------|--------|
| `COMMANDOS_DATA=<folder>` | Game folder. |
| `COMMANDOS_SCRIPT=<file>` | Scripted input for tests: lines `<ms> move x y`, `click x y`, `rclick x y`, `key <name>` (`Ctrl+S` holds a modifier), `shot <name>`, `quit` and more. See `../common/runtime/input-script.c`. |
| `COMMANDOS_DUMP=<folder>` | Saves every 30th frame as BMP, and is the folder for `shot` (default `$TMPDIR`). |
| `COMMANDOS_TRACE_FILES=1` | Logs file opens and searches. |
| `COMMANDOS_TRACE_MSG=1` | Logs window messages, key state reads and key mapping calls. |
| `COMMANDOS_TRACE_SOUND=1` or `2` | Logs Miles sound calls (2: every call). |
| `COMMANDOS_TRACE_VIDEO=1` | Logs the video player (DirectShow stream) calls. |
| `COMMANDOS_TRACE_GDI=1` | Logs fonts and text output. |
| `COMMANDOS_TRACE_TIME=1` | Prints statistics of `Sleep` calls. |

On a crash, the program prints the host and guest registers and a guest
stack trace (only the first entry is always correct).

## Tools

The tools (relocation finder, glue makers, disassembler, debug helpers) are
shared: see [common/README.md](../common/README.md).

## How it works

- SRW translates each x86 instruction of `comandos.exe` to llasm. llasm makes
  LLVM IR, and `llc` makes arm64 code. Guest addresses stay 32-bit: a guest
  address is the host address minus a fixed offset (`-ptrofs`).
- The C runtime functions that SRW cannot translate well (memcpy jump tables,
  80-bit x87 helpers, printf float formatting) are replaced with native code
  (`srw/llasm/*.sci`, `../common/runtime/llasm/c2asm-crt.llasm`).
- The runtime implements kernel32, user32, gdi32, DirectDraw, winmm, a Miles
  Sound System layer on a native mixer, and the DirectShow multimedia stream
  (Cinepak and MS ADPCM decoders) that the game uses for its videos.

## License and credits

- M-HT/SR and the runtime files from its Septerra Core port: Copyright (C) Roman Pauer, MIT license.
- The files in this folder: MIT license.
- *Commandos: Behind Enemy Lines* is a game by Pyro Studios. You need your own copy.

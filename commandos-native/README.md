# Commandos: Behind Enemy Lines: native Apple Silicon port (work in progress)

A native arm64 macOS build of the GOG version of *Commandos: Behind Enemy Lines*.
No Wine and no Rosetta. The game's own x86 code (`comandos.exe`) is statically
recompiled to arm64 with [M-HT/SR](https://github.com/M-HT/SR) (SRW, llasm, LLVM).
A native layer (`runtime/`, based on the SR Septerra Core port, MIT license)
replaces the Windows APIs with SDL2.

Game data is not included. This repository never contains game files or
code generated from them (all of that is in the ignored `build/` folder).

## Build

```sh
conda env create -f environment.yml
. tools/env.sh
tools/install-ldc.sh
make tools
make GAME_DIR="$HOME/Games/Commandos Behind Enemy Lines/Game Data"
./build/Commandos            # data folder: argument, $COMMANDOS_DATA, or the default above
```

## Status

- Done: relocation rebuild (`tools/gen_relocs.py`), SRW patches (`tools/patches/srw.patch`),
  full translation to arm64, C runtime start-up, WinMain, window class, registry,
  DirectDraw start (640x480, 16-bit).
- Next: GDI on surfaces (`SelectObject` and friends), Miles sound (`MSS`, stubs now),
  video, input checks, cursors from the exe resources, app bundle.

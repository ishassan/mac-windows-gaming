# Windows Games on Mac

Native Apple Silicon (arm64) ports of classic Windows games. Each port
recompiles the game's own x86 program to arm64 and replaces the Windows APIs
with a native layer. There is no Wine, no emulator and no Rosetta.

Each game is in its own folder. The `common/` folder holds what more than one
game uses: the build rules, the tools and the Windows API layer.

Game data is not included: you need your own copy of each game (the GOG
versions were used). This repository never contains game files or code
generated from them. All of that stays in the ignored `build/` folders.

## Games

| Game | Folder | Status |
|------|--------|--------|
| Commandos: Behind Enemy Lines (1998) | [commandos-native/](commandos-native/) | Single-player works: videos, menus, missions, save and load. |
| Revenant (1999) | [revenant-native/](revenant-native/) | Single-player works: intro video, menus, new game, walking and talking, load and save, music. Not played to the end. |

## Layout

| Folder | Contents |
|--------|----------|
| `common/` | Shared parts: see [common/README.md](common/README.md). |
| `common/mk/game.mk` | The build rules. A game's `Makefile` has only two lines that include it. |
| `common/tools/` | Recompiler set-up, relocation finder, glue makers, debug helpers. |
| `common/runtime/` | The native Windows API layer (kernel32, user32, gdi32, DirectDraw, Direct3D, DirectInput, DirectPlay, Miles, Smacker, ...). |
| `common/macos/make-bundle.sh` | Makes the `.app` bundle of a game. |
| `<game>-native/` | `game.conf` (names and folders), `runtime/game.h` (values for the runtime), `srw/` (recompiler settings for that exe), `macos/Info.plist`. |
| `build/` | Shared tools that the build makes (SRW, llasm, the M-HT/SR sources). Not tracked. |

## Build a game

All build tools come from one conda environment (`environment.yml`).
Nothing is installed globally.

```sh
conda env create -f environment.yml   # an existing env: conda env update -f environment.yml
. common/tools/env.sh
common/tools/install-ldc.sh      # the D compiler for llasm (not on conda-forge)
cd revenant-native               # or commandos-native
make tools                       # SRW and llasm, once for all games
make                             # recompile the exe and build build/<Game>
../common/macos/make-bundle.sh   # build/<Game> (Native).app
```

The game folder's README has the data folder, the status and the debug
options of that game.

## Add a game

1. Make a folder `<game>-native/` with `game.conf`, `runtime/game.h`,
   `srw/SR.cfg`, `macos/Info.plist` and a two-line `Makefile`
   (copy them from a port that exists).
2. Run `make`. Fix the recompiler errors with the files in `srw/`
   (see [common/README.md](common/README.md)).
3. Add the Windows functions that the game needs to `common/runtime/`
   (or to the game's own `runtime/` folder if only that game needs them).

## License and credits

- [M-HT/SR](https://github.com/M-HT/SR) (static recompiler) and the parts of
  the runtime that come from its Septerra Core port: Copyright (C) Roman Pauer,
  MIT license.
- The app bundles contain the FreeType library (GDI text). Portions of this
  software are copyright (C) The FreeType Project (www.freetype.org). All
  rights reserved. FreeType License.
- The other files in this repository: MIT license, Copyright (c) 2026 Islam
  Hassan (see [LICENSE](LICENSE)).
- The games belong to their owners: Commandos (Pyro Studios), Revenant
  (Cinematix Studios, Eidos Interactive).

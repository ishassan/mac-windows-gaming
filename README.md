# Windows Games on Mac

Native Apple Silicon (arm64) ports of classic Windows games. Each port
recompiles the game's own x86 program to arm64 and replaces the Windows APIs
with a native layer. The ports use no Wine, no emulator and no Rosetta.

Each game also has the setup of its original Windows version on Wine. We
use it only as a reference, to compare with the native version.

Each game has its own folder in `games/`, named with the official game name in
lowercase with hyphens (spaces break the build rules). It has one subfolder
per version of the game, with the same names as the version folders in
`~/Games` on the Mac:

- `native-mac/`: our native port (its build settings).
- `wine-11-athei/`: the Windows version on the athei Wine (the default
  Wine): the app, the registry settings, the patches and the setup steps.
- `wine-8/`: the Windows version on Wine 8, kept to compare: its app (the
  launcher is the same script as for `wine-11-athei`).
- `open-source-port/` (Generals only): the community port GeneralsX, built
  from the source code that EA released.

The `common/` folder has `native-mac/` and `wine/`, for the parts that more
than one game uses. `linux-test-vm/` is a test tool for the Wine versions,
and `benchmark/` the speed test of all versions.

On the Mac, each game folder in `~/Games` has the same version folders:

```
~/Games/<Game name>/
├── Original Game Files     the files of the GOG or EA install (shared by all versions)
├── Saves                   the saves (shared by the versions that can read them)
├── native-mac              <Game> (Native).app, Settings/
├── wine-11-athei           <Game> (Wine).app, wineprefix/, Settings/
├── wine-8                  <Game> (Wine 8).app, wineprefix/, Settings/
└── open-source-port        Generals only: the GeneralsX app, Settings/ (its own saves)
```

Each version keeps its own settings in its `Settings` folder. The games
write into their own folders; the native ports send those paths to Settings
and Saves (`GAME_PATH_REDIRECTS` in `runtime/game.h`), and the Wine
launchers make links in the prefix.

Game data is not included: you need your own copy of each game (the GOG
versions were used for Commandos and Revenant). This repository never
contains game files or code generated from them. All of that stays in the
ignored `build/` folders. Where a Wine version needs a changed game file,
the repository has a script that makes the change on your own copy.

## Games

| Game | Native Mac | Wine (reference) | Native status |
|------|------------|------------------|---------------|
| Commandos: Behind Enemy Lines (1998) | [games/commandos-behind-enemy-lines/native-mac/](games/commandos-behind-enemy-lines/native-mac/) (our port) | [games/commandos-behind-enemy-lines/wine-11-athei/](games/commandos-behind-enemy-lines/wine-11-athei/) | Single-player works: videos, menus, missions, save and load. |
| Revenant (1999) | [games/revenant/native-mac/](games/revenant/native-mac/) (our port) | [games/revenant/wine-11-athei/](games/revenant/wine-11-athei/) | Single-player works: intro video, menus, new game, walking and talking, load and save, music. Not played to the end. |
| Command & Conquer Generals Zero Hour (2003) | [games/command-and-conquer-generals-zero-hour/native-mac/](games/command-and-conquer-generals-zero-hour/native-mac/) (our recompile of the 1.04 exe, Direct3D 8 on DXVK and MoltenVK) | [games/command-and-conquer-generals-zero-hour/wine-11-athei/](games/command-and-conquer-generals-zero-hour/wine-11-athei/) | Menus, 3D menu scene, skirmish (10 minutes), campaign start, saves load both ways with Wine. |
| Command & Conquer Generals Zero Hour (2003), community port | [games/command-and-conquer-generals-zero-hour/open-source-port/](games/command-and-conquer-generals-zero-hour/open-source-port/) (GeneralsX, not ours; our fork of it is the submodule `GeneralsX/`) | (same as above) | Plays (community port, branch `custom` of our fork). Its saves do not load in the original exe. |

## Layout

| Folder | Contents |
|--------|----------|
| `common/native-mac/` | Shared parts of our native ports: see [common/native-mac/README.md](common/native-mac/README.md). |
| `common/native-mac/mk/game.mk` | The build rules. A game's `Makefile` has only two lines that include it. |
| `common/native-mac/tools/` | Recompiler set-up, relocation finder, glue makers, debug helpers. |
| `common/native-mac/runtime/` | The native Windows API layer (kernel32, user32, gdi32, DirectDraw, Direct3D, DirectInput, DirectPlay, Miles, Smacker, ...). |
| `common/native-mac/macos/make-bundle.sh` | Makes the `.app` bundle of a game. |
| `games/<game>/native-mac/` | Our ports: `game.conf` (names and folders), `runtime/game.h` (values for the runtime, with the paths of the settings and saves), `srw/` (recompiler settings for that exe), `macos/Info.plist`. |
| `games/<game>/wine-11-athei/` | The Wine version: `app/` (the app's launcher and `Info.plist`), `prefix.reg` (registry settings), patches, and a README with the Windows files that it needs and the setup steps. |
| `games/<game>/wine-8/` | The Wine 8 app: `app/Info.plist`, and `app/launcher.sh` (a link to the `wine-11-athei` launcher, which picks the Wine from the folder name). |
| `common/wine/` | Shared parts of the Wine versions: `make-app.sh` (makes a Wine app), `patch-miles.py` (the Miles sound fix), and the notes: see [common/wine/README.md](common/wine/README.md). |
| `benchmark/` | The speed test of Wine 8, athei and the native ports, with fixed scenes, scripts and rules: see [benchmark/README.md](benchmark/README.md). |
| `linux-test-vm/` | A Linux VM that runs the Wine versions on an in-memory screen, so a test takes no window from the Mac: see [linux-test-vm/README.md](linux-test-vm/README.md). |
| `build/` | Shared tools that the build makes (SRW, llasm, the M-HT/SR sources). Not tracked. |

## Build a game

All build tools come from one conda environment (`environment.yml`).
Nothing is installed globally.

```sh
conda env create -f environment.yml   # an existing env: conda env update -f environment.yml
. common/native-mac/tools/env.sh
common/native-mac/tools/install-ldc.sh      # the D compiler for llasm (not on conda-forge)
cd games/revenant/native-mac               # or games/commandos-behind-enemy-lines/native-mac
make tools                       # SRW and llasm, once for all games
make                             # recompile the exe and build build/<Game>
../../../common/native-mac/macos/make-bundle.sh   # build/<Game> (Native).app
```

The game folder's README has the data folder, the status and the debug
options of that game.

## Add a game

1. Make a folder `games/<game>/native-mac/` with `game.conf`, `runtime/game.h`,
   `srw/SR.cfg`, `macos/Info.plist` and a two-line `Makefile`
   (copy them from a port that exists).
2. Run `make`. Fix the recompiler errors with the files in `srw/`
   (see [common/native-mac/README.md](common/native-mac/README.md)).
3. Add the Windows functions that the game needs to `common/native-mac/runtime/`
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
  (Cinematix Studios, Eidos Interactive), Command & Conquer Generals Zero
  Hour (Electronic Arts).

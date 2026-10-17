# Medal of Honor: Allied Assault: open-source port (OpenMoHAA)

The game runs natively on Apple Silicon with OpenMoHAA, a community engine
for Medal of Honor: Allied Assault, Spearhead and Breakthrough
(https://github.com/openmoh/openmohaa, GPL 2 or later). It is not our port:
this folder holds the setup only. The engine reads the game data (the `.pk3`
files) of the original game. No Windows program runs, also not to install
the game.

## Source of the app

- OpenMoHAA v0.82.1 (released 2025-08-05), the release file
  `openmohaa-v0.82.1-macos-multiarch-arm64-x86_64.zip` (10,588,420 bytes,
  SHA-256 `dd590e88...a59e579a`, the same as the digest on the GitHub
  release page). It holds `openmohaa`, `game.dylib`, `cgame.dylib` and the
  libraries (SDL2, OpenAL, curl), for arm64 and x86_64. It has no `.app`.
- The game data: the GOG version, "Medal of Honor: Allied Assault War Chest"
  (the base game and both expansion packs). Use the offline installer from the
  GOG library ("Download offline backup game installers", Windows):
  `setup_medal_of_honor_2.0.0.21.exe` with `-1.bin` and `-2.bin` (2.5 GB). The
  "GOG Galaxy" download (0.5 MB) is only a downloader for Windows.

## Files

| File | What it does |
|---|---|
| `make-app.sh [installer]` | Downloads the release (once, into `build/`), checks its SHA-256, and makes the three apps in `build/`. With the GOG installer, the apps get the game icons from it. |
| `app/start.sh`, `app/Info.plist` | The launcher and `Info.plist` of the apps. `make-app.sh` puts in the app name and the game number. |
| `extract-game-files.sh <installer> <game folder>` | Takes the data out of the installer with `innoextract` and sorts it (see "Game files"). |
| `install.sh <game folder>` | Puts the apps from `build/` into `<game folder>/open-source-port/`, and makes the `Settings` folder, the save links and the shortcuts. |
| `controller.cfg` | The buttons of the PS4 controller (DUALSHOCK 4). |
| `mac-keys.cfg` | Cmd+Enter: full screen or window. |
| `mkalias.swift` | Makes the Finder aliases with the bookmark API of Foundation, so Finder does not come to the front. |

## Set up

```sh
cd games/medal-of-honor-allied-assault/open-source-port
G="$HOME/Games/Medal of Honor Allied Assault"
I="<folder of the installer>/setup_medal_of_honor_2.0.0.21.exe"
mkdir -p "$G"
./extract-game-files.sh "$I" "$G"   # once
./make-app.sh "$I"
./install.sh "$G"
```

The result:

```
~/Games/Medal of Honor Allied Assault/
├── Medal of Honor Allied Assault              Finder alias to the app (and one for each expansion)
├── Original Game Files                        main, mainta (Spearhead), maintt (Breakthrough), readme files
├── Saves                                      Allied Assault, Spearhead, Breakthrough
└── open-source-port
    ├── Medal of Honor Allied Assault.app      (and the Spearhead and Breakthrough apps)
    └── Settings                               fs_homepath: configs, controller.cfg, mac-keys.cfg
        └── main|mainta|maintt/save            links to Saves/<game>
```

## How the apps work

- Each app holds a copy of the release files in `Contents/MacOS` (27 MB), and
  `start` as its program. `start` finds the game folder (the nearest folder
  above with `Original Game Files`) and starts `openmohaa` with:
  - `+set com_target_game 0`, `1` or `2` (Allied Assault, Spearhead,
    Breakthrough). The launchers in the release do only this.
  - `+set fs_basepath "<game folder>/Original Game Files"`: the game data.
  - `+set fs_homepath "<game folder>/open-source-port/Settings"`: the
    settings and saves. Without it the engine writes to
    `~/Library/Application Support/openmohaa`.
  - `+set in_joystick 1 +set in_joystickUseAnalog 1`: the controller, with
    analog sticks. Both are off by default, and `in_joystick` is read only
    at start.
  - `+exec controller.cfg +exec mac-keys.cfg`: these run after the game's
    own configs (`default.cfg`, the player config, `autoexec.cfg`).
- The saves go to `<fs_homepath>/<main|mainta|maintt>/save/<player>`, so
  `Settings/<game>/save` is a link to `Saves/<game>`.
- The release zip loses the exec bit of `openmohaa`. `make-app.sh` sets it.
- The apps are signed ad hoc (`codesign --force --deep -s -`).
- The engine starts in full screen at the screen size of the Mac
  (`r_mode -2`, `r_fullscreen 1`).

## Game files

`extract-game-files.sh` sorts the files of the installer's `app/` folder by
the Windows files rule of `~/Games`:

- `Original Game Files/`: `main`, `mainta`, `maintt` (the `.pk3` packs,
  configs, sound, video), the readme files, and two bitmaps
  (`00000000.016`, `00000000.256`, 640x480 pictures; not certain what uses
  them, so they stay).
- `Saves/Allied Assault/`: the save that comes with the GOG game
  (`unnamedsoldier/training0000.*`).
- The Trash, as "Medal of Honor Allied Assault Windows files (<date>)": the
  Windows programs (`MOHAA.exe`, `moh_spearhead.exe`, `moh_breakthrough.exe`,
  the servers and configure tools), all DLLs (also the Windows game code
  `gamex86.dll`, `cgamex86.dll`, `ffx86.dll` in `main`, `mainta`, `maintt`),
  the Miles sound drivers (`snddrivers`), the server `.bat` files, the icons,
  and three empty files (`sm.*`). Only a Wine version would need them, and
  there is none.

The script checks the SHA-1 of every file after the moves (8,905 files).

## Controller

`controller.cfg` (DUALSHOCK 4 names; SDL calls Cross `A`, Circle `B`,
Square `X`, Triangle `Y`, SHARE `BACK`, OPTIONS `START`):

| Button | Action |
|---|---|
| Left stick, right stick | move; turn and aim (analog: the binds `+forward`, `+back`, `+moveleft`, `+moveright`, `+left`, `+right`, `+lookup`, `+lookdown` select the axes) |
| R2, L2 | fire, second fire mode |
| Cross, Circle | jump, crouch |
| Square, Triangle | reload, next weapon |
| L1, R1 | lean left, lean right |
| L3, R3 | walk (hold), use |
| D-pad | rifle (up), submachine gun (right), pistol (left), grenade (down) |
| Touchpad, SHARE, OPTIONS | health or binoculars, scores, menu |

The menus need the trackpad or mouse. The engine finds a controller that is
connected after the start (`SDL_CONTROLLERDEVICEADDED`).

## Cmd+Enter

The engine has no key to switch between full screen and a window, and a bind
cannot have a modifier. `mac-keys.cfg` binds Cmd (`COMMAND`) to a hold alias:
while Cmd is down, Enter and keypad Enter run
`toggle r_fullscreen; vid_restart`. When Cmd is up, Enter is free again. No
config of the game binds ENTER, KP_ENTER or COMMAND. In the menus the keys
go to the menu, so it works only during play.

## Tests

The engine has no mode without a window. A test with a window is for normal
mode only (see `CLAUDE.md`). To keep the real settings unchanged, start
`Contents/MacOS/start` with a later `+set fs_homepath <test folder>` (a later
`+set` wins) and copy the two `.cfg` files into `<test folder>/main/`.

- A path in a `+set` value must not have `//`: the engine reads `//` as the
  start of a comment and cuts the path there.
- 2026-10-17: the main menu showed, in a window (`r_fullscreen 0`, `r_mode
  4`). The log showed the data of `Original Game Files/main` (18,297 files in
  the packs), "GL_RENDERER: Apple M2" and "execing controller.cfg". No
  controller was connected ("0 possible joysticks"), so the buttons are not
  tested yet. Cmd+Enter is not tested yet.
- 2026-10-17: the three scripts gave the same result as the same setup done
  step by step: the apps were the same byte for byte, and the game folder had
  the same file list.

# Medal of Honor: Allied Assault: open-source port (OpenMoHAA)

The game runs natively on Apple Silicon with OpenMoHAA, a community engine
for Medal of Honor: Allied Assault, Spearhead and Breakthrough
(https://github.com/openmoh/openmohaa, GPL 2 or later). It is not our port:
this folder holds the setup, and our fork of the engine is the submodule
`openmohaa/`. The engine reads the game data (the `.pk3`
files) of the original game. No Windows program runs, also not to install
the game.

## Source of the app

- The engine: our fork of OpenMoHAA, https://github.com/ishassan/openmohaa
  (made on 2026-10-18 from `openmoh/openmohaa`), the git submodule
  `openmohaa/` in this folder. The fork has the license of OpenMoHAA (GPL 2
  or later), not the MIT license of this repository. Get the files with
  `git submodule update --init`. Branches (both start from the tag `v0.82.1`,
  commit `a72bc153`, the source of the v0.82.1 release):
  - `fix/cmd-enter-fullscreen`: Cmd+Enter switches between full screen and a
    window, as Alt+Enter (Option+Enter) does. One line in
    `code/client/cl_keys.cpp`. It can become a pull request to
    `openmoh/openmohaa`.
  - `fix/trigger-threshold`: a separate dead zone for the two triggers,
    the setting `joy_triggerThreshold` (default 0.15, the same as
    `joy_threshold`). In `code/sdl/sdl_input.c`. It can become a pull
    request too.
  - `fix/joystick-zoom-sensitivity`: the stick turns slower while zoomed
    in (a rifle scope), with the same scale as the mouse
    (`CG_SensitivityScale()`, the vertical view angle / 75). Before, the
    stick turned at the same degrees per second in the scope, so the view
    moved about 4 times too fast. In `code/client/cl_input.cpp`.
  - `custom`: the tag `v0.82.1` plus a merge of each fix branch. We build the
    apps from it.
- The libraries SDL2, OpenAL and curl: from the OpenMoHAA v0.82.1 release
  file `openmohaa-v0.82.1-macos-multiarch-arm64-x86_64.zip` (10,588,420
  bytes, SHA-256 `dd590e88...a59e579a`, the same as the digest on the GitHub
  release page). They are built for macOS with no other dependencies.
- The game data: the GOG version, "Medal of Honor: Allied Assault War Chest"
  (the base game and both expansion packs). Use the offline installer from the
  GOG library ("Download offline backup game installers", Windows):
  `setup_medal_of_honor_2.0.0.21.exe` with `-1.bin` and `-2.bin` (2.5 GB). The
  "GOG Galaxy" download (0.5 MB) is only a downloader for Windows.

## Files

| File | What it does |
|---|---|
| `make-app.sh [installer]` | Builds the engine from `openmohaa/` (CMake and Ninja, arm64, about 1 minute), downloads the release (once, into `build/`) for its libraries and checks its SHA-256, and makes the three apps in `build/`. With the GOG installer, the apps get the game icons from it. |
| `app/start.sh`, `app/Info.plist` | The launcher and `Info.plist` of the apps. `make-app.sh` puts in the app name and the game number. |
| `extract-game-files.sh <installer> <game folder>` | Takes the data out of the installer with `innoextract` and sorts it (see "Game files"). |
| `install.sh <game folder>` | Puts the apps from `build/` into `<game folder>/open-source-port/`, and makes the `Settings` folder, the save links and the shortcuts. |
| `controller.cfg` | The buttons of the PS4 controller (DUALSHOCK 4). |
| `mac-keys.cfg` | Removes the Cmd and Enter binds of an earlier version of this file (the player config keeps binds). |
| `mkalias.swift` | Makes the Finder aliases with the bookmark API of Foundation, so Finder does not come to the front. |

## Set up

```sh
git submodule update --init                          # in the repository root, once
conda env update -f environment.yml                  # the build tools, in the repository root
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

- Each app holds the engine built from the fork (`openmohaa`, `game.dylib`,
  `cgame.dylib`, arm64) and the three release libraries in `Contents/MacOS`
  (16 MB), and
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
| R2, L2 | fire, aim (hold; with a sniper rifle, the scope) |
| Cross, Circle | jump, crouch |
| Square, Triangle | reload, next weapon |
| L1, R1 | grenades, second fire mode (rifle butt hit, grenade throw, scope) |
| L3, R3 | walk (hold), use |
| D-pad | rifle (up), submachine gun (down), lean left and right (hold) |
| Touchpad, SHARE, OPTIONS | health or binoculars, scores, menu |

Right stick speed: the turn speed is `j_yaw` (left and right) or `j_pitch`
(up and down) x 32767 x the zoom scale degrees per second at full stick, in
a straight line from the dead zone. The zoom scale is the vertical view
angle / 75: about 0.86 with no zoom at `cg_fov 80`, much less in a rifle
scope (the fork branch `fix/joystick-zoom-sensitivity`). The engine default,
0.022 (about 620 degrees per second with no zoom), was much too fast with a
PS4 controller (2026-10-18), so `controller.cfg` sets `j_yaw -0.0064` and
`j_pitch 0.0047` (about 180 and 130 degrees per second with no zoom).
`controller.cfg` runs after the player config, so its values win at each
start.

Stick curve: the fork branch `feature/controller-aim` adds `j_curve`. The
right stick value (0 to 1, after the dead zone) goes to this power before
the turn speed is applied. `controller.cfg` sets 3: a half push turns at an
eighth of the full speed, so small moves are slow and exact, and a full
push turns at the speed above. 1 (the default) is the old straight line.

Aim mode (2026-10-29, the same branch): the command `+aim` (L2).

- While it is held, the client sets the setting `cl_aim` to 1. The cgame
  then zooms from `cg_fov` to `cg_aimFov` (40, about 2 times) in
  `cg_aimTime` (150) ms, and multiplies the turn speed by
  `cg_aimSensitivity` (0.5), on top of the zoom scale. The original game
  has no aim animation, so the gun stays at the hip.
- Aim mode is off in a scope, at intermission, and in a camera view
  (`PMF_CAMERA_VIEW`), but not in a camera view with `PMF_TURRET`: a gun
  on a vehicle (for example the car in mission 1) uses that view, and the
  player aims there.
- When it is pressed and released, the client also sends the game the
  command `aimzoom 1` or `aimzoom 0`. With a weapon that has a zoom (a
  scope, for example the Springfield: `zoom 20` in its `.tik`), the game
  opens or closes the scope, as the second fire button does. The cgame
  zoom of aim mode stops while a scope is open.
- So the second fire mode moved to R1, and lean moved to the D-pad. The
  pistol has no D-pad button now: Triangle (next weapon) reaches it.
- Test 2026-10-29 (test `fs_homepath`, level m2l1, console commands
  `+aim`/`-aim`, no controller): with the Springfield '03 Sniper, the
  scope opened (`fov` 20) and closed (`fov` 80). With the silenced pistol,
  the view zoomed in and back, the gun stayed in view, and the game `fov`
  stayed 80. The stick curve and the slower turn need the controller, so
  they were not tested then.
- Test 2026-10-29 with the PS4 controller (installed app, test
  `fs_homepath` with a copy of the player config): L2 set `cl_aim` to 1
  and back to 0 three times, and the scope opened. With the first values
  (`cg_aimFov 55`, `cg_aimSensitivity 0.6`, `j_curve 2`) the zoom was too
  small to notice and small stick moves were still too fast. With 40, 0.5
  and 3 the zoom and the stick speed were good.

Trigger dead zone: `controller.cfg` sets `joy_triggerThreshold 0.8` (the
fork's setting; `joy_threshold` 0.15 stays for the sticks). Measured
2026-10-18 on the PS4 controller of this Mac, with R2 not touched: R2 read
47 to 63 percent of its travel (L2 0), and 100 percent when pressed. With
the old dead zone the game saw R2 as held: each switch between full screen
and window opens the controller again (`IN_Restart`), the game saw a new R2
press and fired. With 0.8, a test with 6 switches gave no R2 press.

The menus need the trackpad or mouse. The engine finds a controller that is
connected after the start (`SDL_CONTROLLERDEVICEADDED`).

## Full screen or window

Press Cmd+Enter or Option+Enter.

- Option+Enter is the engine's own key: in `CL_KeyEvent`
  (`code/client/cl_keys.cpp`), Alt+Enter calls `Key_ClearStates()` and
  `Cvar_SetValue("r_fullscreen", ...)`. `Cvar_SetValue` changes the latched
  setting at once, and `GLimp_EndFrame` changes the window with
  `SDL_SetWindowFullscreen`, with no restart. Tested 2026-10-18 with the
  release build: 4 presses, 4 changes, both ways.
- Cmd+Enter: the fork branch `fix/cmd-enter-fullscreen` adds `K_COMMAND` to
  the same check, so Cmd+Enter does the same thing.
- A settings file cannot do this. The first version of `mac-keys.cfg` used a
  hold alias on Cmd that bound Enter to `toggle r_fullscreen; vid_restart`
  (`toggle` cannot change a latched setting at once). After `vid_restart`
  the game got only key-up events for Cmd, so the second Cmd+Enter did
  nothing (tested 2026-10-18). `mac-keys.cfg` now removes those binds,
  because the player config keeps them.

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
  controller was connected ("0 possible joysticks"), so the buttons were not
  tested then. - 2026-10-17: the three scripts gave the same result as the same setup done
  step by step: the apps were the same byte for byte, and the game folder had
  the same file list.

# Command & Conquer Generals Zero Hour: open-source port (GeneralsX)

Zero Hour runs natively on Apple Silicon with a community port that is
built on the source code that EA released. It is not our port: this folder
holds only the notes to install and update it, and the local changes that
we make to it. It needs no Wine, no Rosetta, and no Origin. For the Windows
version on Wine (the reference), see [../other-versions/wine-11-athei](../other-versions/wine-11-athei/README.md).
Our own native port of the original exe is in [../other-versions/native-mac](../other-versions/native-mac/README.md).

## Source of the app

- The app is a community port of the game, built on the source code that EA
  released. Its project name is "GeneralsX" (https://github.com/fbraz3/GeneralsX).
  We use this name only here and in fixed paths that the app requires.
- Installed version: 1.0.2 (release date 2026-09-28).
- Download file: `macOS-GeneralsXZH.zip` from the GitHub release page.
- The game data comes from the original Origin copy (base game and Zero Hour).

## Source code (our fork)

`GeneralsX/` in this folder is a git submodule: our fork of the port,
https://github.com/ishassan/GeneralsX (made on 2026-10-05 from
`fbraz3/GeneralsX`). This repository keeps only the link and the commit to
use. The files are in the fork. The fork has the port's license (GPL 3 or
later), not the MIT license of this repository.

- Get the files: `git submodule update --init` (in the repository root).
- Branches in the fork (all start from `fbraz3/GeneralsX` main after 1.0.2):
  - One branch for each general fix. Each one can become a pull request to
    `fbraz3/GeneralsX`.
    - `fix/water-shader-assembler`: the water shaders (river, texbem water,
      trapezoid water with sparkles). The port had no shader assembler on
      macOS and Linux, so the water was drawn without these shaders.
  - `custom`: our own changes, plus a merge of each fix branch. It stays in
    the fork, and we build the app from it.
    - Cmd+Enter or Option+Enter switches between full screen and a window.
    - The app starts in a window (in full screen the game often skips the
      intro movie). Start it with `-fullscreen` to start in full screen.
    - Cmd+Q does not quit (2026-10-09): it is next to Cmd+1 (the control
      groups) and easy to press by mistake. SagePatch takes the key off the
      Quit item of the app menu and eats the key
      (`Patches/SagePatch/src/macos/QuitKey_macos.cpp`). The game's Exit,
      the Quit menu item and Quit in the Dock still quit. Not yet built or
      tested on the Mac.
- Build the app: `./make-app.sh` (in this folder). It builds the fork with
  the conda env of the repository (`environment.yml`) and vcpkg (in the
  ignored `build/vcpkg` of the repository root), then applies the local
  changes below (steps 1 to 5; `run-local.sh` is the block of step 4). The result is
  `build/Command & Conquer Generals Zero Hour.app`. It installs nothing.
  The first build is slow, because vcpkg builds the libraries first.
- The build uses the Homebrew `ffmpeg` and `libpng` of this Mac if they are
  installed, as the official release does. The bundle step copies them into
  the app, so the app itself does not need Homebrew.
- The app that is installed now is the build of fork commit `4c5c39c`
  (branch `fix/retail-save-compat`, merged into `custom` as `9555140`),
  built with `make-app.sh` and installed 2026-10-09. The build before it
  (`4c33825`) is `generals-open-source-port-app-before-ghost-fix.app`, and
  the build `a692659` is `generals-open-source-port-app-before-save-fix`,
  both in `~/Library/Caches/games-reorg-backup-2026-10-08`. The build `5f02335`
  went to the Trash on 2026-10-08, and the downloaded 1.0.2 app on
  2026-10-06.
- The build step also makes the app self-contained: it points MoltenVK at the
  C++ library of macOS and removes the paths to build folders from every
  file. Without this, the app loaded `libc++` from the conda env.

## Folder layout

```
~/Games/Command and Conquer Generals Zero Hour/
├── Original Game Files
│   ├── Command and Conquer Generals             base game files (.big)
│   └── Command and Conquer Generals Zero Hour   Zero Hour files (.big)
├── Saves                     saves of all versions (Wine, our native port, this app)
├── open-source-port
│   ├── Command & Conquer Generals Zero Hour.app   this app (double-click to play)
│   └── Settings              ~/Library/Application Support/GeneralsX is a link to it
│       ├── GeneralsZH        Options.ini, SagePatch.ini, Save → ../../../Saves/Zero Hour
│       ├── registry.ini
│       ├── fontconfig-cache
│       └── Command and Conquer Generals Zero Hour.dxvk-cache, ..._d3d9.log
├── native-mac                our native port of the original exe
├── wine-11-athei             the original game on the athei Wine
├── wine-8                    the original game on Wine 8
└── README.md                 a pointer to the repository
```

## Decisions

1. Each game has its own folder in `~/Games`. The folder holds the app and all
   of the game's data.
2. Use the official game name. Do not use the port's name where we can avoid it.
3. The game files are in `Original Game Files` of the game folder. The
   launcher script finds them there. There is no `~/GeneralsX` folder or link.
4. Settings and saves of this app are in `open-source-port/Settings`. The app
   always uses `~/Library/Application Support/GeneralsX`, and we cannot
   change this path. That path is a link to `open-source-port/Settings`.
   Since 2026-10-08 its save folder `GeneralsZH/Save` is a link to the shared
   `Saves/Zero Hour`. The save fix of branch `fix/load-retail-unicode-saves`
   makes this app read and write the Windows format (2 bytes for each
   character of text). Before that, it wrote 4 bytes, and the original exe
   gave "Error loading game". Our native port and the Wine versions also read
   those old saves (see `../native-mac/README.md` and
   `../wine-11-athei/save-fix/`).
5. The original Porting Kit (Wine) version went to the Trash on 2026-10-03.
   On 2026-10-05 the user asked for a Wine version again, to compare with the
   native app when a problem occurs. It is in `other-versions/wine-11-athei/` and
   `other-versions/wine-8/` (see "Wine version" below).
6. Windows files (rule of 2026-10-05, for all games in `~/Games`): a file
   that no app uses goes to the Trash. A file that a native app needs stays
   in `Original Game Files`. A file that only the Wine version needs goes
   into the Wine prefix. A file that both need stays in `Original Game Files`.
7. Since 2026-10-08 the DXVK shader cache and log, and the font cache, go to
   `open-source-port/Settings`, not into the game files or a `var` folder
   (steps 4 and 5).
8. Updates are optional. The game checks GitHub when it starts and shows a
   message, but it does not install anything. For single-player play, an
   update is only necessary to fix a problem. For online play, an update can
   be necessary to match other players.
9. Before you install a new version, do security checks on the download.

## Wine version (for comparison)

See [../other-versions/wine-11-athei/README.md](../other-versions/wine-11-athei/README.md).

## Paths that we cannot change (fixed in the compiled app)

| Path | Contents | What we did |
|---|---|---|
| `~/Library/Application Support/GeneralsX` | settings, saves, maps, `registry.ini` | Link to `open-source-port/Settings` |
| `open-source-port/Settings/GeneralsZH` | subfolder name made by the app | Kept |
| `~/.generals_online` | online lobby login tokens | Kept in the home folder (hidden, holds login secrets) |
| Version text in the game menu | shows the port's name | Kept (needs a rebuild to change) |

## Local changes to the app (apply again after each update)

An update replaces the whole `.app`, so all of these changes are lost. Apply
them again, in this order.

1. Rename the app to `Command & Conquer Generals Zero Hour.app`, and put it in
   `~/Games/Command and Conquer Generals Zero Hour/open-source-port`.
2. In `Contents/Info.plist`, set these values:
   - `CFBundleName` and `CFBundleDisplayName`: `Command & Conquer Generals Zero Hour`
   - `CFBundleIdentifier`: `local.games.command-and-conquer-generals-zero-hour`
   - `CFBundleIconFile`: `Command and Conquer Generals Zero Hour.png`
3. Rename these files inside `Contents`:
   - `Resources/bin/GeneralsXZH` to `Resources/bin/Command and Conquer Generals Zero Hour`
   - `Resources/generalsx-zh_icon.png` to `Resources/Command and Conquer Generals Zero Hour.png`
   - Remove the link `MacOS/GeneralsXZH`. Make a link `MacOS/Command and Conquer Generals Zero Hour` that points to `run.sh`.
4. In `Contents/MacOS/run.sh`, change two parts:
   - Replace the block that sets `CNC_GENERALS_PATH` and `CNC_GENERALS_ZH_PATH`
     to `~/GeneralsX/...` with this block (the file `run-local.sh` here):

     ```bash
     # LOCAL CHANGE (see README.md): the app is in the "open-source-port" folder of the
     # game folder. The game files are in "Original Game Files" of the game folder, not
     # in ~/GeneralsX. The DXVK shader cache and log go to the Settings folder next to
     # the app, not into the game files.
     VERSION_DIR="$(cd "${CONTENTS_DIR}/../.." && pwd)"
     GAME_DIR="$(dirname "${VERSION_DIR}")"
     if [[ -z "${CNC_GENERALS_PATH:-}" ]]; then
         export CNC_GENERALS_PATH="${GAME_DIR}/Original Game Files/Command and Conquer Generals"
     fi
     if [[ -z "${CNC_GENERALS_ZH_PATH:-}" ]]; then
         export CNC_GENERALS_ZH_PATH="${GAME_DIR}/Original Game Files/Command and Conquer Generals Zero Hour"
     fi
     mkdir -p "${VERSION_DIR}/Settings"
     export DXVK_STATE_CACHE_PATH="${DXVK_STATE_CACHE_PATH:-${VERSION_DIR}/Settings}"
     export DXVK_LOG_PATH="${DXVK_LOG_PATH:-${VERSION_DIR}/Settings}"
     ```

   - In the last command, change `"${BIN_DIR}/GeneralsXZH"` to
     `"${BIN_DIR}/Command and Conquer Generals Zero Hour"`.
   - If the new `run.sh` is very different from the old one, compare the two
     files before you change it. Keep all new lines from the update.
5. In `Contents/Resources/fontconfig/fonts.conf`, change the cache folder
   line `<cachedir>./../../var/cache/fontconfig</cachedir>` to
   `<cachedir>~/Library/Application Support/GeneralsX/fontconfig-cache</cachedir>`
   (without this, the font cache goes to a `var` folder in the game folder).
6. Register the app again:
   `/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "<path to the app>"`
7. Make sure that the link `~/Library/Application Support/GeneralsX` still
   points to `open-source-port/Settings`. Make sure that no `~/GeneralsX`
   folder exists.

## Update procedure

1. Keep the old app until the new one works. Move it to the Trash only at the end.
2. Download the new `macOS-GeneralsXZH.zip` to a temporary folder. Do not open it yet.
3. Security checks:
   - Compare the SHA-256 of the zip with the digest that GitHub shows for the file.
   - Make sure that the file was uploaded by `github-actions[bot]` from a release
     workflow run on the tagged commit.
   - All Mach-O files must be arm64 (`file`, `lipo -archs`).
   - Every library that the app loads (`otool -L`) must be inside the app or
     part of macOS.
   - Read `run.sh` and every other script in full.
   - No LaunchAgents, login items, `crontab`, or `osascript` use.
   - Look at the web addresses in the binaries (`strings`). Expected: game
     lobby servers, STUN/TURN servers, and the GitHub update check.
4. Apply the local changes above.
5. Test:
   - Start the app with a double-click. The game must reach the main menu.
   - The process must be native: in `vmmap <pid>`, "Code Type" is ARM64.
   - `lsof -p <pid>` must show `.big` files from `Original Game Files`.
   - Load a save, then save once. The new file must appear in
     `Saves/Zero Hour`, with 2 bytes for each character of its name
     (`xxd` of the first 0x40 bytes), and our native port must load it.
6. Update the "Installed version" line in this file.

## Check log

- 2026-10-09: build of branch `fix/retail-save-compat` (two save fixes for
  the retail game). (1) With the player observer option (on by default),
  GeneralsX keeps ghost object pictures (buildings under the fog) for all
  players and saved all of them. The retail game loads them, but frees only
  those of the local player, so every next load in the same session failed
  with "Error loading game" (`ERROR_BAD_INI`). Seen in Wine and in our
  native port; it also fails with the unpatched `game.dat`. A copy of save
  `00000017` that keeps only the local player's pictures did not cause the
  failure.
  The fix saves the pictures of the local player only. (2) The save header
  stored the full Mac path of the map as its label; it now stores the file
  name only (`getMapLeafName`). Tested with a test home folder: GeneralsX
  loaded `00000017` and saved it again; the new save has pictures for the
  local player only and the label `md_gla02.map`. Our native port
  (offscreen, scripted input) loaded the new save, and then the Windows
  save `00000010` in the same session. Installed. Then the older GeneralsX
  saves `00000014` to `00000018` in `Saves/Zero Hour` were converted the
  same way (only the local player's pictures, the map file name as label),
  with the originals backed up in
  `~/Library/Caches/games-reorg-backup-2026-10-08/saves-before-convert-2026-10-09`.
  The converted `00000015` loaded in our native port, and then `00000010`
  in the same session.
- 2026-10-08: build of fork commit `4c33825` (writes 2-byte text) tested
  with a test copy of the settings and saves (`HOME=/tmp/gxhome`): the
  Windows save "GLA 1" loaded, a new save had 2-byte text, and our native
  port loaded it (same map, units and money). Installed. Then
  `GeneralsZH/Save` became a link to `Saves/Zero Hour`; the load list shows
  all 18 saves.
- 2026-10-03: 1.0.2 installed and passed the security checks. Started from
  the game folder: native ARM64, reached the main menu, and loaded `.big` files
  from `Game Data`. Not tested yet: a new save written through the
  `Settings and Saves` link.

- 2026-10-06: build of branch `custom` (fork commit `5f02335`) tested in a
  window, and then installed. ARM64; every library loads from inside the app;
  the main menu shows the online entry; the intro plays in the window;
  water shows its sparkles; Cmd+Enter switches to full screen and back; the
  settings and saves did not change (19 files, same checksums). Not tested:
  loading a save, and water inside a mission.

- 2026-10-06, later: the installed build (`5f02335`) loaded the save "GLA 1",
  saved once to a new file, and loaded that file again. River water inside
  the mission moves. The 17 old save files did not change. Windows saves showed
  broken names in the load list ("GA5" and boxes).

- 2026-10-06: build of fork commit `a692659` (adds the save fix
  `fix/load-retail-unicode-saves`) tested in a copy of the app, not installed.
  The Windows saves show their real names, and the Windows save "GLA 5" loads
  and plays. The Wine version loads the same save with the same state
  ($6900).

- 2026-10-06: the same build installed. ARM64; 47 `.big` files open from
  `Game Data`; the load list shows the real names of the Windows saves; the
  save "GLA 1" made by the native app loads and plays. Settings and saves did
  not change (20 files, same checksums).

- 2026-10-08: moved into `open-source-port/` of the new layout. Steps 4 and
  5 applied to the installed app and it was signed again. The link in
  `~/Library/Application Support` points to `open-source-port/Settings`.
  Not started yet after the move.

## Camera in the main menu

The camera height in the menu is the same as in the Wine version. The
menu only looks different because the two versions show different moments
of the scripted battle at the same time after the start, and because the Wine
version does not draw the menu frame.

- Check of 2026-10-06 (1280x800 window, 24 frames each, 5 s apart): where
  the same object is in both pictures (the junk boat on the beach), it has
  about the same size in native and in Wine 8 (125 and 120 pixels wide in
  the scaled pictures, measured by eye).
- Tested and ruled out: a build with `PRESERVE_RETAIL_SCRIPTED_CAMERA` set
  to `1` in `Core/GameEngine/Include/Common/GameDefines.h` (the retail
  scripted camera of upstream pull request 2524). Also ruled out:
  `MaxCameraHeight = 310` in place of 350 in `SagePatch.ini` (on a copy of
  the settings). The menu did not change in either test. The change was not
  kept.
- An earlier note (also 2026-10-06) said that the menu camera is about 1.3
  times higher. That came from pictures of different moments, and it is
  wrong.
- In a mission the two versions also look almost the same (same save,
  1024x768).

## Known problems

- Intro movie (1.0.2, tested 2026-10-03): in full screen the game goes
  directly to the main menu with no intro (3 of 3 starts). In window mode
  (`-win`) the intro plays. The shader cache file is not the cause. The user
  saw the intro one time in full screen, so the problem does not occur every
  time. Cause not found yet. Reported as https://github.com/fbraz3/GeneralsX/issues/354, then closed by us the same day (not planned). Check again after each update.

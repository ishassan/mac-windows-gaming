# Command & Conquer Generals Zero Hour (native Apple Silicon)

Zero Hour runs natively on Apple Silicon with a community port that is
built on the source code that EA released. It is not our port: this folder
holds only the notes to install and update it, and the local changes that
we make to it. It needs no Wine, no Rosetta, and no Origin. For the Windows
version on Wine (the reference), see [../wine](../wine/README.md).

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
- Commit in use: tag `1.0.2` (`96cd64f`).
- Plan for the branches in the fork:
  - One branch for each general fix (for example the save text and the
    water shaders). Each one can become a pull request to `fbraz3/GeneralsX`.
  - One branch for our own changes (for example Cmd+Enter for full screen).
    It stays in the fork, and we build the app from it.
- The app that is installed now is still the downloaded 1.0.2 release.

## Folder layout

```
~/Games/Command and Conquer Generals Zero Hour/
  Command & Conquer Generals Zero Hour.app   the game (double-click to play)
  Game Data/
    Command and Conquer Generals/            base game files (.big)
    Command and Conquer Generals Zero Hour/  Zero Hour files (.big)
  Settings and Saves/                        settings, saves, maps
  Old Windows Saves/                         saves and options of the Wine version
  Wine/
    Command & Conquer Generals Zero Hour (Wine).app   the original game on Wine
    wineprefix/                              the Wine prefix of that app, with
                                             the files that only Wine needs
  README.md                                  a pointer to this file
```

## Decisions

1. Each game has its own folder in `~/Games`. The folder holds the app and all
   of the game's data.
2. Use the official game name. Do not use the port's name where we can avoid it.
3. The game files are in `Game Data`, next to the app. The launcher script
   finds them there. There is no `~/GeneralsX` folder or link.
4. Settings and saves are in `Settings and Saves`. The app always uses
   `~/Library/Application Support/GeneralsX`, and we cannot change this path.
   That path is a link to `Settings and Saves`.
5. The original Porting Kit (Wine) version went to the Trash on 2026-10-03.
   On 2026-10-05 the user asked for a Wine version again, to compare with the
   native app when a problem occurs. It is in `Wine/` (see "Wine version"
   below).
6. Windows files (rule of 2026-10-05, for all games in `~/Games`): a file
   that neither app uses goes to the Trash. A file that only the native app
   needs stays in `Game Data`. A file that only the Wine version needs goes
   into `Wine/`. A file that both need stays in `Game Data`.
7. Updates are optional. The game checks GitHub when it starts and shows a
   message, but it does not install anything. For single-player play, an
   update is only necessary to fix a problem. For online play, an update can
   be necessary to match other players.
8. Before you install a new version, do security checks on the download.

## Wine version (for comparison)

See [../wine/README.md](../wine/README.md).

## Paths that we cannot change (fixed in the compiled app)

| Path | Contents | What we did |
|---|---|---|
| `~/Library/Application Support/GeneralsX` | settings, saves, maps, `registry.ini` | Link to `Settings and Saves` |
| `Settings and Saves/GeneralsZH` | subfolder name made by the app | Kept |
| `~/.generals_online` | online lobby login tokens | Kept in the home folder (hidden, holds login secrets) |
| Version text in the game menu | shows the port's name | Kept (needs a rebuild to change) |

## Local changes to the app (apply again after each update)

An update replaces the whole `.app`, so all of these changes are lost. Apply
them again, in this order.

1. Rename the app to `Command & Conquer Generals Zero Hour.app`, and put it in
   the game folder (`~/Games/Command and Conquer Generals Zero Hour`).
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
     to `~/GeneralsX/...` with this block:

     ```bash
     # LOCAL CHANGE (see README.md in the game folder): game files are in the
     # "Game Data" folder next to this app, not in ~/GeneralsX.
     GAME_DIR="$(cd "${CONTENTS_DIR}/../.." && pwd)"
     if [[ -z "${CNC_GENERALS_PATH:-}" ]]; then
         export CNC_GENERALS_PATH="${GAME_DIR}/Game Data/Command and Conquer Generals"
     fi
     if [[ -z "${CNC_GENERALS_ZH_PATH:-}" ]]; then
         export CNC_GENERALS_ZH_PATH="${GAME_DIR}/Game Data/Command and Conquer Generals Zero Hour"
     fi
     ```

   - In the last command, change `"${BIN_DIR}/GeneralsXZH"` to
     `"${BIN_DIR}/Command and Conquer Generals Zero Hour"`.
   - If the new `run.sh` is very different from the old one, compare the two
     files before you change it. Keep all new lines from the update.
5. Register the app again:
   `/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "<path to the app>"`
6. Make sure that the link `~/Library/Application Support/GeneralsX` still
   points to `Settings and Saves`. Make sure that no `~/GeneralsX` folder exists.

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
   - `lsof -p <pid>` must show `.big` files from `Game Data`.
   - Load a save, then save once. The new file must appear in
     `Settings and Saves/GeneralsZH/Save`.
6. Update the "Installed version" line in this file.

## Check log

- 2026-10-03: 1.0.2 installed and passed the security checks. Started from
  the game folder: native ARM64, reached the main menu, and loaded `.big` files
  from `Game Data`. Not tested yet: a new save written through the
  `Settings and Saves` link.

## Known problems

- Intro movie (1.0.2, tested 2026-10-03): in full screen the game goes
  directly to the main menu with no intro (3 of 3 starts). In window mode
  (`-win`) the intro plays. The shader cache file is not the cause. The user
  saw the intro one time in full screen, so the problem does not occur every
  time. Cause not found yet. Reported as https://github.com/fbraz3/GeneralsX/issues/354, then closed by us the same day (not planned). Check again after each update.

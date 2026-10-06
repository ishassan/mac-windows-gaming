# Wine versions: shared parts

Each game also runs as the original Windows version on Wine. We use it
only as a reference: when the native port shows a problem, the Wine version
shows how the original looks and behaves. This folder has what all Wine
versions share. Each game's `wine/` folder has its own setup steps.

The repository holds no game files. The Windows files that a Wine version
needs come from your own copy of the game (GOG or EA). The scripts here
change those files on your Mac.

## Wine

- One Wine for all games: Homebrew cask `wine-crossover` 23.7.1 (Wine
  8.0.1, CrossOver patches). Install: `brew install --cask wine-crossover`.
  It is an Intel program, so it needs Rosetta.
- To compare with the native port without a window on the Mac, use the
  Linux test VM ([../../linux-test-vm](../../linux-test-vm/README.md)). It runs
  the same Wine apps' scripts with Linux Wine 11.

## Wine 11 (`patch-wow64cpu.py`)

Wine 11.0 (Gcenx `wine-stable` 11.0_1, the app "Wine Stable") runs 32-bit
games in WoW64 mode. Under Rosetta, 32-bit Direct3D games then crash at the
start in most tries. Run the fix once after each install or update of Wine
Stable:

```
common/wine/patch-wow64cpu.py
```

- Cause: the switch between 32-bit and 64-bit code in `wow64cpu.dll` is a
  far jump. Under Rosetta, the CPU mode sometimes does not change on that
  jump.
- Fix: both jumps land on a small stub that checks the CPU mode and does the
  switch again when it did not change. The script docstring has the detail.
- The script keeps the original as `wow64cpu.dll.orig`. It works only on
  the 11.0_1 build. With any other build it stops and changes nothing.
- Tested 2026-10-06: without the fix, Revenant started in only 3 to 4 of 10
  tries; with it, 20 of 20.

## Folder layout on the Mac

```
~/Games/<Game>/
  <Game> (Native).app        the native port (see <game>/native-mac)
  Game Data/                 the game files and the saves
  Wine/
    <Game> (Wine).app        the Wine version (make-app.sh)
    wineprefix/              the Wine prefix (Revenant: wineprefix_cx)
    wine-launcher.log        the output of the last starts
```

The Wine version does not run the game from `Game Data/` directly. Each
prefix has a real game folder (the `C:\...` folder that the game sees). It
holds:

- the Windows files that only the Wine version needs (the list is in each
  game's `wine/README.md`), and
- a link to each item in `Game Data/`.

So both versions use the same game files and, where noted, the same saves.
When it starts, the Wine app adds a link for each new item in `Game Data/`.

The DLLs must be next to the game program. Miles (the sound library) stops
with "The MSS DLL is incorrectly installed in the Windows system directory"
when it is in the Windows system folder.

## The Wine app (`make-app.sh`)

Each game has `wine/app/Info.plist` and `wine/app/launcher.sh`. The
launcher:

1. sets `WINEPREFIX` to the prefix next to the app, and `WINEDEBUG=-all`;
2. makes the Wine user folders (Desktop, Downloads, ...) local folders, not
   links to the Mac home folders. Where the game keeps its saves in My
   Documents, `Documents` is a link into the game folder;
3. adds the links to `Game Data/`;
4. changes to the Wine game folder and starts the game program. The output
   goes to the log file next to the app.

Make the app:

```
common/wine/make-app.sh <game>/wine/app "$HOME/Games/<Game>/Wine" <icon file>
```

After you edit the launcher inside an app, sign the app again:
`codesign -s - --force "<app>"`.

A new prefix links My Documents to `~/Documents`. Start the game for the
first time with the Wine app (it sets the links first), else the game
writes into the Mac Documents folder.

## The Miles fix (`patch-miles.py`)

Revenant and Commandos use the Miles Sound System (`mss32.dll`). With this
Wine, the Miles timer thread hangs in `SuspendThread`, and the game hangs at
the start with no sound.

`patch-miles.py` changes the import names `SuspendThread` and
`ResumeThread` (from `KERNEL32.dll`) in `mss32.dll` to `GetThreadId`.
`GetThreadId` takes the same single argument (a thread handle) and uses the
same calling convention, so the stack stays correct. No thread is then
suspended. Only the two name strings change (24 bytes).

```
common/wine/patch-miles.py "<Wine game folder>/mss32.dll"
```

The script keeps the original as `mss32.dll.orig`, does not change a file
that is already patched, and prints the SHA-256 before and after. For the
GOG files of Revenant and Commandos, it tells you if the result matches the
files that were patched on 2026-10-05 (tested 2026-10-05: both match).

Generals Zero Hour has Miles 6.5c (Revenant: 5.0r). Its `mss32.dll` also
imports `SuspendThread` and `ResumeThread`, but the game did not hang
without the fix (tested 2026-10-05), so its file stays as it is.

## Test methods

- `WINEDEBUG=+file,+loaddll` logs the files that Wine opens and the DLLs
  that it loads.
- Without a window on the Mac (a start test, no picture):
  1. Make an APFS copy of the prefix: `cp -cR <prefix> <scratch>/prefix`.
     Do not change the real prefix.
  2. In the copy, set the null display driver and no sound:
     `WINEPREFIX=<copy> wine reg add 'HKCU\Software\Wine\Drivers' /v Graphics /d null /f`
     and the same with `/v Audio /d ''`.
  3. Start the program from the copy's game folder with
     `WINEDEBUG=+loaddll,+msgbox,err+all`. The log shows the DLLs that
     load, missing DLLs (`err:module`), and the text of each message box.
  - With the null driver the games cannot draw. They stop at the graphics
    start (Revenant: "DDERR_OUTOFMEMORY"; Generals: "Please make sure you
    have DirectX 8.1"). So this test shows only that a program starts and
    finds its files and DLLs. For pictures, use the Linux test VM.
  - The copy's links still point to the real `Game Data`, so a game can
    write there (settings, logs). Back up the saves first.

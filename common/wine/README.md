# Wine versions: shared parts

Each game also runs as the original Windows version on Wine. We use it
only as a reference: when the native port shows a problem, the Wine version
shows how the original looks and behaves. This folder has what all Wine
versions share. Each game's `wine/` folder has its own setup steps.

The repository holds no game files. The Windows files that a Wine version
needs come from your own copy of the game (GOG or EA). The scripts here
change those files on your Mac.

## Wine

- One Wine for all games: the athei CrossOver 26.3 build (Wine 11,
  [athei/wine-build](https://github.com/athei/wine-build) release
  `cx-26.3.0-7`), with the
  [x87sidecar](https://github.com/athei/x87sidecar) math helper (v1.8.0) and
  our thread QoS fix (`qos.c`). Install all three:
  `common/wine/install-athei.sh` (to `~/Applications/Wine athei`; pinned
  sha256 values). It is an Intel program, so it needs Rosetta.
- Before: Homebrew cask `wine-crossover` 23.7.1 (Wine 8.0.1). To move a
  game's prefix to athei: make a copy of the prefix (`cp -cRp`; the update
  cannot be undone), run
  `WINEPREFIX=<prefix> WINEDLLOVERRIDES="mscoree,mshtml=" "$HOME/Applications/Wine athei/wine/bin/wine" wineboot -u`,
  then make the Wine app again (`make-app.sh`) or copy the new
  `launcher.sh` into it and sign it again.
- Scan of the athei downloads (2026-10-07): no malicious code found. All
  files are in `wine/`. The network code is only in the usual Wine parts.
  The D3DMetal files have a valid Apple signature. The source changes over
  CrossOver start x87sidecar only when `ROSETTA_X87_PATH` is set. x87sidecar
  has no network code. Limit: the binaries were not rebuilt from source.
- The launcher sets `ROSETTA_X87_PATH` (x87 math in native code, not in
  Rosetta's slow x87 emulation), `DYLD_INSERT_LIBRARIES` to `qos.dylib`, and
  `WINEDLLOVERRIDES="mscoree,mshtml="` (the build has no Mono and no Gecko;
  without this, the first start waits for an install prompt that does not
  show).
- `qos.c`: the athei build makes its threads with the default macOS QoS
  class, so a timed `Sleep()` ends 6 to 8 ms late. Commandos limits its frame
  rate with `Sleep()` and dropped to 17.8 frames per second. With the QoS
  class "user interactive" (as the Wine 8 build sets it), it gets 19.5.
- athei uses the Windows user name `crossover` (Wine 8 uses the Mac user
  name), so My Documents is `drive_c/users/crossover/Documents`. A new or
  updated prefix links it to the Mac Documents folder. The launchers set up
  both user folders, so both Wines use the saves in the game folder. (Before
  this fix, Generals on athei read and wrote `Options.ini` in
  `~/Documents`, at Low detail.)
- Measured 2026-10-07 in full screen, on prefix copies with the same game
  settings for both Wines (game process cores, median frames per second;
  two runs each; Wine 8 / athei + x87sidecar + qos.dylib):

  | Game and scene | Wine 8 | athei |
  |---|---|---|
  | Commandos, mission | 0.25-0.28, 19.5-19.8 | 0.20-0.22, 19.5-19.8 |
  | Revenant, Keep scene (`Software3D=Yes`) | 0.42, 24.4 | 0.26, 24.4 |
  | Generals, menu scene, High, 1280x800, full screen | 2.19-2.20, 24.0-24.2 | 1.43, 31.2 |
  | Generals, the same in a window (`-win`) | 2.20, 23.0-23.6 | 1.46, 31.2 |

  No start failed. On athei, full screen in Generals uses emulated display
  modes (`EmulateModeset`, see the Generals `prefix.reg`); the window and
  full-screen values show that this costs no speed. An earlier Generals
  value for athei (1.21 cores) came from Low detail (the My Documents
  problem above) and is not valid. Generals on athei draws the ground
  correctly (Wine Stable 11.0 drew it white).
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

## Wine 8 and Wine 11 compared (2026-10-06)

This comparison is from before the move to athei. It compares Wine 8
(`wine-crossover` 23.7.1) with plain WineHQ Wine 11 (Gcenx `wine-stable`),
not with the athei CrossOver 26.3 build. Result then: plain Wine 11 was not
better for these three games:

- Revenant: as good as Wine 8 with the game's own software 3D (the setting
  that the Wine app uses). With Direct3D figures it uses about 25% more CPU.
- Commandos: the same CPU, but about 9% fewer frames per second.
- Generals: about 30% fewer frames per second, and the ground is white.

All tests: the same game scene, the same scripted input, the Mac screen, and
APFS copies of the prefixes. "csmt" is the Wine Direct3D setting `csmt`
(on is the Wine default, off is `csmt=0`). CPU is in cores (1.00 = one
full core). Frame rate is the median frames per second.

| Game and scene | Wine 8, csmt on | Wine 8, csmt off | Wine 11, csmt on | Wine 11, csmt off |
|---|---|---|---|---|
| Revenant, Keep scene, software 3D (`Software3D=Yes`) | 1.18 cores, 24.4 fps | 0.42 cores, 24.4 fps | 0.49 cores, 24.4 fps | 0.43 cores, 24.4 fps |
| Revenant, Keep scene, Direct3D figures (`Software3D=No`) | 1.61 cores, 24.4 fps | 0.68 cores, 24.4 fps | 1.54 cores, 24.4 fps | 0.85 cores, 24.4 fps |
| Commandos, mission (save slot 1) | 0.30 cores, 19.6 fps | not used (1) | 0.28 cores, 17.8 fps | not used (1) |
| Generals, menu scene, 1280x800 window | 2.22 cores, 21.8 fps | 1.18 cores, 22.6 fps | 1.76 cores, 15.5 fps | 1.20 cores, 15.2 fps |

(1) Commandos draws with cnc-ddraw, which uses OpenGL directly, so the
Direct3D setting `csmt` does not apply.

Each value is the mean of two runs, which differed by 0.01 cores or less;
the Generals values are single runs. Revenant: 24.4 fps is the game's own
limit, so the frame rate cannot show a difference there.

### Why Wine 8 is faster here

"Wine 8" here is not plain Wine 8. It is CrossOver 23.7.1 from CodeWeavers:
Wine 8.0.1 plus their Mac patches. "Wine 11" is the plain WineHQ release.
Both run 32-bit games the same way, with the WoW64 layer: a Wine 8 Revenant
process loads `wow64win.dll`, and both apps have `wow64.dll` and
`wow64cpu.dll`. All three games are 32-bit, and each frame makes thousands
of OpenGL calls from 32-bit code through that layer.

- A Mac patch that only Wine 8 has: when OpenGL gives buffer memory above
  the 4 GB that 32-bit code can use, CrossOver maps that memory again at a
  low address. Its `opengl32.so` has the message "failed to find low memory
  to remap to"; the Wine 11 file does not. Plain Wine 11 copies the buffer
  in place of this: Generals on Wine 11 logs `fixme:opengl:wow64_map_buffer
  Doing a copy of a mapped buffer (expect performance issues)`.
- Measured (Revenant, Wine 11, `csmt=0`, profile of 2026-10-06): about 0.18
  cores go to these buffer copies.
- Generals: on both Wines its main thread is busy all the time (0.95 cores
  on Wine 8, 0.97 on Wine 11), so the frame rate shows the work per frame:
  44 ms on Wine 8 and 66 ms on Wine 11. Not profiled yet, so the share of
  the buffer copies in the extra 22 ms is not known.
- Not the cause: timers. A test program measured the same `Sleep`,
  `timeGetTime` and periodic timer accuracy on both Wines.
- Not found yet: the reason for the Commandos gap (about 5 ms more per game
  frame). These settings did not change it: cnc-ddraw `singlecpu=false`,
  `maxgameticks=0`.

### Generals: white ground on Wine 11

- The ground texture (2048x1024, `A1R5G5B5`, 3 mipmap levels) is made,
  filled and used the same way on both Wines (`WINEDEBUG=trace+d3d8`). Both
  Wines report the same texture formats as supported.
- On Wine 11, many draws log `No resource view bound at index 1` (and 2):
  a texture stage has no texture when the ground is drawn. So the cause is
  in the Wine 11 Direct3D code, not in the game.
- Settings that did not help: `csmt` on and off, `MaxShaderModelPS=0`,
  `VideoMemorySize=1024`. `renderer=vulkan` crashes
  (`Unhandled texgen 0x20000`, then an HLSL compile error).
- In the Linux test VM (Hangover Wine 11.16, Mesa software OpenGL), the
  same Wine app, prefix settings and shell map show the ground correctly
  (sand and grass; tested 2026-10-06). So the bug shows only on the Mac.
- On the Mac, Wine 11.16 (Gcenx `wine-devel` 11.16, in a scratch folder,
  test copy of the prefix) also draws the ground white, with the same
  `No resource view bound` warnings (tested 2026-10-06). So the Wine
  version is not the cause. The cause is on the Mac side: Apple's OpenGL
  (4.1, made on Metal) or the Mac display driver of Wine. A newer plain Wine
  is not likely to fix it.
- Wine 11.16 started Generals without `patch-wow64cpu.py` (1 of 1 tries;
  the script does not fit its `wow64cpu.dll`).

### What could make Wine 11 as good as Wine 8

1. Revenant: `csmt=0` (in `games/revenant/other-versions/wine-11-athei/prefix.reg` since
   2026-10-06). With software 3D, Wine 11 is then as good as Wine 8.
2. A newer Wine 11 build: not a fix for the white ground (Wine 11.16 on
   the Mac has it too, see above). `wine-devel` or `wine-staging` 11.18
   (Gcenx/macOS_Wine_builds, 2026-09-25) is not tested.
3. CrossOver 26.3 is Wine 11.0 plus CodeWeavers' Mac patches (CodeWeavers
   changelog: "CrossOver 26 includes Wine 11.0"). Ready builds: the paid
   CrossOver app. No free ready build of its open source was found;
   `Gcenx/macports-wine` can build it with MacPorts (not installed here).
   The newest free ready build of CrossOver source that was found is
   CrossOver 24.0.7 (Wine 9), as the Sikarugir engine `WS12WineCX24.0.7`.
   None of these were tested.

## Folder layout on the Mac

```
~/Games/<Game>/
├── Original Game Files      the game files (shared by all versions)
├── Saves                    the saves (shared)
├── native-mac               the version to play (Generals: open-source-port)
└── other-versions
    ├── wine-11-athei
    │   ├── <Game> (Wine).app    the Wine version on athei (make-app.sh)
    │   ├── wineprefix           the Wine prefix
    │   ├── Settings             the settings of this version
    │   └── wine-launcher.log    the output of the last starts
    ├── wine-8               the same on Wine 8: <Game> (Wine 8).app, wineprefix, Settings
    └── native-mac           Generals only: our native port
```

The Wine version does not run the game from `Original Game Files/`
directly. Each prefix has a real game folder (the `C:\...` folder that the
game sees). It holds:

- the Windows files that only the Wine version needs (the list is in each
  game's `other-versions/wine-11-athei/README.md`),
- links to the shared `Saves` and to the version's `Settings`, where the
  game writes its saves and settings into this folder or into My
  Documents, and
- a link to each item in `Original Game Files/`.

So all versions use the same game files and, where the game allows it, the
same saves. When it starts, the Wine app sets these links and adds a link
for each new item in `Original Game Files/`.

The DLLs must be next to the game program. Miles (the sound library) stops
with "The MSS DLL is incorrectly installed in the Windows system directory"
when it is in the Windows system folder.

## The Wine app (`make-app.sh`)

Each game has `other-versions/wine-11-athei/app/Info.plist` and
`other-versions/wine-11-athei/app/launcher.sh`, and
`other-versions/wine-8/app/Info.plist` (the launcher there is a link to the
same script). The launcher:

1. picks the Wine from the name of its version folder (`wine-8`: Wine 8,
   else athei), sets `WINEPREFIX` to the prefix next to the app, and
   `WINEDEBUG=-all`. The game folder is the nearest folder above the app
   with `Original Game Files`;
2. makes the Wine user folders (Desktop, Downloads, ...) local folders, not
   links to the Mac home folders, for both Wine user names (the Mac user
   name for Wine 8, `crossover` for athei). Where the game keeps its saves
   or settings in My Documents, the matching folder is a link to `Saves` or
   `Settings`;
3. adds the links to `Saves`, `Settings` and `Original Game Files/`;
4. changes to the Wine game folder and starts the game program. The output
   goes to the log file next to the app.

Make the app:

```
common/wine/make-app.sh games/<game>/other-versions/wine-11-athei/app "$HOME/Games/<Game>/other-versions/wine-11-athei" <icon file>
common/wine/make-app.sh games/<game>/other-versions/wine-8/app "$HOME/Games/<Game>/other-versions/wine-8" <icon file>
```

After you edit the launcher inside an app, sign the app again:
`codesign -s - --force "<app>"`.

**Cmd+Q (2026-10-09).** The Mac driver of Wine puts Cmd+Option+Q, not
Cmd+Q, on the Quit item of its menu (`dlls/winemac.drv/cocoa_app.m`), so
Cmd+Q goes to the game (as Ctrl+Q where `LeftCommandIsCtrl` is set) and
does not quit. The Wine apps need no change for it. Not yet checked on the
athei build.

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
  - The copy's links still point to the real `Original Game Files`,
    `Saves` and `Settings`, so a game can write there. Back up the saves
    first. (`benchmark/relink.py` points the links of a copy at the copy.)

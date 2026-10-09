# Revenant on Wine (reference version)

The original Windows Revenant (GOG, version 1.22) on Wine. It is a
reference for the native port ([../../native-mac](../../native-mac/README.md)).
This folder is for the athei Wine (the default); [../wine-8](../wine-8/README.md)
is the same setup on Wine 8.
The parts that all Wine versions share (Wine, folder layout, the Miles fix,
test methods) are in [../../../../common/wine](../../../../common/wine/README.md).

## Files here

| File | What it is |
|---|---|
| `app/Info.plist`, `app/launcher.sh` | The Wine app (`common/wine/make-app.sh`). The launcher is the same for both Wine versions: the name of the version folder picks the Wine. |
| `prefix.reg` | The registry settings of the prefix. Import: `wine regedit /S prefix.reg` (with `WINEPREFIX`). |
| `dispmode-fix/` | Source of the display-mode fix (`dispmode_fix.c`, `_inmm.def`). |

## Layout on the Mac

```
~/Games/Revenant/
├── Original Game Files        the GOG game
├── Saves                      the saves (shared by all versions)
├── native-mac
├── wine-11-athei
│   ├── Revenant (Wine).app
│   ├── Settings               revenant.ini and Curmap/ of this version
│   ├── wineprefix             the Wine prefix
│   │   └── drive_c/Revenant   C:\Revenant: the Wine-only files and links
│   ├── scale_game.py          an old tool that scales the game window to the screen
│   └── wine-launcher.log
└── wine-8                     the same for Wine 8
```

The game writes its saves (`Save\`), the current map (`Curmap\`) and its
settings (`revenant.ini`) into its own folder. In `C:\Revenant`, `Save` is a
link to the shared `Saves`, and `revenant.ini` and `Curmap` are links to
`Settings` of the version. The other items are links to `Original Game
Files`. The launcher makes these links before each start. The native port
uses the same `Saves` and its own `native-mac/Settings`.

## Wine-only files (in `C:\Revenant`)

All come from the GOG install. The DLLs must stay next to `Revenant.exe`.

| File | What it does |
|---|---|
| `_inmm.dll` | Our display-mode fix (see below). It sends all calls to `_inmm_real.dll`. |
| `_inmm_real.dll` | The GOG `_inmm.dll` (GOG's CD-audio and display-mode helper; it plays the music from Ogg files), renamed. |
| `mss32.dll` | Miles sound, patched with `common/wine/patch-miles.py`. The original is `mss32.dll.orig`. |
| `mp3dec.asi`, `*.m3d` | Miles MP3 decoder (the speech is MP3 data in `resources.rvr`) and 3D sound providers. |
| `smackw32.dll` | Smacker video player. |
| `libvorbis.dll`, `libvorbisfile.dll`, `libogg.dll` | Ogg music for `_inmm_real.dll`. |
| `Launcher.exe`, `wh32LIB.DLL` | The game's Windows launcher. |
| `ipxwrapper.dll`, `wsock32.dll`, `mswsock.dll`, `ipxconfig.exe`, `ipx_reg.cmd`, `directplay.cmd` | IPX network play (multiplayer). |
| `__redist/` | DirectX and other Windows installers. |
| `dispmode_fix.log` | Written by the display-mode fix. |

## Set up again

These are the steps for a new prefix. The prefix of 2026-03 was made in
earlier sessions, so this list repeats its result (the files and the
settings in it), not a recorded command history.

1. Install Wine: `common/wine/install-athei.sh` (the athei CrossOver 26.3
   build, see [`common/wine`](../../../../common/wine/README.md)). In the steps
   below, `wine` is `"$HOME/Applications/Wine athei/wine/bin/wine"`, with
   `WINEDLLOVERRIDES="mscoree,mshtml="` set. (The installed prefix was made
   with Wine 8 and then updated by athei; a new prefix made with athei is
   not tested yet.)
2. Put the GOG game in `~/Games/Revenant/Original Game Files` (the files
   that the native port also needs). Move its `Save` folder to
   `~/Games/Revenant/Saves`, and its `revenant.ini` to
   `other-versions/wine-11-athei/Settings/`.
3. Make the prefix:
   `WINEPREFIX="$HOME/Games/Revenant/other-versions/wine-11-athei/wineprefix" wine wineboot -i`.
4. Settings (with the same `WINEPREFIX`): `wine regedit /S games/revenant/other-versions/wine-11-athei/prefix.reg`.
   It sets `ddraw=builtin` (the Wine DirectDraw, not a replacement
   `ddraw.dll`), the Mac driver value `ForceOpenGLBackingStore=y`, and
   Direct3D `csmt=0` (since 2026-10-06). With `csmt=0`, Revenant used
   0.69 cores in place of 1.60 on Wine 8, and 0.85 in place of 1.53 on
   Wine 11, at the same 24.4 frames per second. Screenshots on Wine 11 with
   and without it differ no more than two runs with the same setting.
   The real Wine app started with it on 2026-10-06 and reached the main
   menu (0.21 cores in the menu); the saves did not change.
5. Make `wineprefix/drive_c/Revenant` and copy the Wine-only files above
   from the GOG install into it. Rename the GOG `_inmm.dll` to
   `_inmm_real.dll`.
6. Build the display-mode fix (needs `brew install mingw-w64`) and copy it
   into `C:\Revenant`:

   ```
   cd games/revenant/other-versions/wine-11-athei/dispmode-fix
   i686-w64-mingw32-gcc -shared -s -o _inmm.dll dispmode_fix.c _inmm.def -luser32 -lkernel32 -ldxguid -O2
   ```

7. Patch Miles: `common/wine/patch-miles.py "<prefix>/drive_c/Revenant/mss32.dll"`.
8. Make the app:
   `common/wine/make-app.sh games/revenant/other-versions/wine-11-athei/app "$HOME/Games/Revenant/other-versions/wine-11-athei" "$HOME/Games/Revenant/Original Game Files/Revenant.icns"`.
9. Start the app. At each start it sets the `Save`, `revenant.ini` and
   `Curmap` links and adds a link for each new `Original Game Files` item.

Check (2026-10-05): in a new prefix in the Linux test VM, `prefix.reg`
imports with the same values as the installed prefix. `make-app.sh` makes an app that is the same as the
installed one (the signature excluded). The display-mode fix builds from
this source, with the same 178 exports as the installed `_inmm.dll`, but
the file is not the same (112870 bytes, the installed one 112358 bytes).
Cause: a newer compiler. The installed file was made with mingw-w64 13.0.0
(GCC 15.2.0, binutils 2.45), the new one with mingw-w64 14.0.0 (GCC 16.1.0,
binutils 2.46.1). Both have the same functions, the same text and log
strings, the same exports and forwards, and the same calls and constants
in the three fix functions. Only the stack layout, the register use and the
linked mingw runtime code differ. On the Mac, Revenant (Wine) started with
the new file: the fix loaded and set 960x600, and the game reached the main
menu.

## The two fixes

### 1. Display mode (`_inmm.dll`)

The game sets the display to 640x480 with 16-bit color. A Mac with Apple
Silicon has no 640x480 mode, so the mode change fails and the game stops
with `Fatal Error: Direct Error DDERR_GENERIC in file
d:\revenant\DirectDraw.cpp at line 333`.

Fix: `C:\Revenant\_inmm.dll` replaces the GOG DLL. `_inmm.def` sends each
export to `_inmm_real.<name>` (the GOG DLL). It has three parts (source:
`dispmode-fix/dispmode_fix.c`):

1. Mode change. It hooks `ChangeDisplaySettingsExW`. When a mode change
   fails, it tries 960x600 with 32-bit color, then the current desktop mode,
   then reports success.
2. Scaling (since 2026-10-09). After a fallback, the game still draws a
   640x480 frame. Hooks in the Wine DirectDraw surface vtables (versions 1,
   2, 3, 4 and 7) send all writes to the primary surface (Blt, BltFast,
   Flip, Lock/Unlock, GetDC/ReleaseDC) to a 640x480 surface in system
   memory ("the frame"). After each write, the frame is stretched into the
   largest 4:3 area at the center of the screen, with black bars at the
   sides (on 960x600: 800x600 at x=80). Revenant writes the whole frame to
   the primary with one `Blt` per frame; it does not use `Flip`.
3. Mouse. The game reads the cursor in screen pixels (`GetCursorPos`, mouse
   messages from `PeekMessageA`) and sets it (`SetCursorPos`, `ClipCursor`).
   Hooks in the import table of `Revenant.exe` map these between the 4:3
   area and the 640x480 frame. Wine's DirectInput clips the cursor to the
   640x480 game window when it takes the mouse; this clip is mapped to the
   4:3 area too. Without this, mouse messages stop at x=639 and y=479, so
   a click on the lower or right part of a scaled item went to the wrong
   place (seen in the VM).

It writes what it does to `C:\Revenant\dispmode_fix.log`. The log of one
scaled start in the Linux test VM (2026-10-09, `ForceFallback=1`, the
default fallback; the VM has no 960x600 mode, so the current mode
1280x800 was used; the vtable lines are left out). Not seen on the Mac yet.

```
=== dispmode_fix loaded ===
Settings: Fallback=960x600 ForceFallback=1 Stretch=1
Hook installed
DirectDrawCreate hook installed
Mouse hooks: GetCursorPos yes, SetCursorPos yes, ClipCursor yes, PeekMessageA yes
DirectDrawCreate: 0x00000000
DirectDrawCreate: 0x00000000
CDSEW: 640x480x16
  ForceFallback: 640x480 treated as failed
  Failed(-1), trying 960x600x32 IN-PLACE
  Failed(-2), trying the current mode 1280x800
  current mode OK (in-place)
  Scaling on: frame 640x480
Primary surface (v4) 00DC2444
Frame: 640x480, 16 bits, caps 0x840
Blt (v4) with the primary: sent to the frame (first: dst 0,0-1280,800 flags 0x600)
Present: primary 1280x800
Clip 0,0-640,480 mapped to 107,0-1173,800
PeekMessageA: mouse message mapped (first: 0x200 at 450,479)
GetCursorPos: mapped (first: 450,500)
```

Settings: an optional `C:\Revenant\dispmode_fix.ini`, section
`[dispmode_fix]`:

| Key | What it does |
|---|---|
| `Fallback=960x600` | The first fallback mode (default 960x600). |
| `Stretch=0` | No scaling: the old result (the picture in the top-left part). |
| `FrameMemory=video` | The frame surface in video memory (`any`: Wine chooses). The default, system memory, used less CPU in the test VM. |
| `ForceFallback=1` | Test only: treat the 640x480 request as failed, for a test machine that has a 640x480 mode (the Linux test VM). |
| `Debug=1` | Log the mouse button messages and the frames per second. |

Tests of the scaling (2026-10-09, Linux test VM, `ForceFallback=1` and
`Fallback=1280x800`, so the VM has the same problem as the Mac): the intro
videos, the main menu, the load screen and a loaded save filled the 4:3
area (1066x800 at x=107) with no picture in the top-left part. The cursor
lit "Load Game" when the X pointer was on the scaled item, and the clicks on
"Load Game" (menu) and "Load Game" (button) worked. In the game, 24.4 frames
per second in all three cases below. CPU of `Revenant.exe` in the game (VM
cores, software OpenGL, from the `ps` CPU time in whole seconds over 30
seconds, so about +-0.03; the values are not Mac values):

| Case | Cores |
|---|---|
| 640x480 mode (no fallback) | 0.30 |
| 1280x800 fallback, no scaling (`Stretch=0`, the old Mac result) | 0.35 |
| 1280x800 fallback, scaling, frame in system memory (default) | 0.40 |
| 1280x800 fallback, scaling, frame in video memory | 0.60 |

The first version drew the black bars at every frame. In Wine each write to
the primary is a present, so this made three presents per frame, and some
VM screenshots were black. Now the bars are drawn for a new primary and then
at every 64th frame.

On the Mac (2026-10-09): installed in the real prefixes of both Wine
versions (the old file is kept as `_inmm.dll.bak-2026-03` next to it). The
user started both apps and reported that the picture fills the screen
(the logs show the 4:3 area, 800x600 at x=80 on the 960x600 display).
Wine 8 needed one more change:
it keeps the primary surface at the desktop size (1470x956) after the
change to 960x600, and the Mac shows only the top-left 960x600 part of it.
The first build centered the picture on 1470x956, so the picture was too
big and cut at the right and the bottom. Now the fix uses the display mode
that was set when it is smaller than the primary. The Wine 8 log:

```
CDSEW: 640x480x16
  Failed(-2), trying 960x600x32 IN-PLACE
  960x600 OK (in-place)
  Scaling on: frame 640x480
Present: primary 1470x956, display mode 960x600
Clip 0,0-640,480 mapped to 80,0-880,600
```

Not tested on the Mac yet: the CPU, and a switch to another app and back
during the game. The Wine 11 app was checked with the first build; the
Wine 8 change does nothing there (primary and display mode are both
960x600), and the VM test passed with it.

What did not work (tested 2026-10-09 on the Mac, athei, a copy of the
prefix):

- `HKCU\Software\Wine\X11 Driver` `EmulateModeset=y` (the name says X11,
  but Wine reads it for the Mac driver too). The display stays at its
  native mode and the game gets 640x480. A 4:3 area fills the screen
  height, but it stays black, with the default Direct3D renderer and with
  `renderer=gl`. The Mac driver of Wine 11 has no scaled drawing path for
  OpenGL; the X11 driver has one (`offscreen` in
  `dlls/winex11.drv/opengl.c`). Wine 8 does not have this setting.
- `HKCU\Software\Wine\Direct3D` `renderer=gdi`, with or without
  `EmulateModeset`: the game stops with `DirectX Error DDERR_OUTOFMEMORY in
  file d:\revenant\DirectDraw.cpp at line 333`.
- cnc-ddraw does not work for Revenant (it stops with `DDERR_GENERIC`
  at line 226, because cnc-ddraw has no Direct3D).

Why the native app could always fill the screen (checked 2026-10-06): it
draws each 640x480 frame into its own buffer and lets the Mac scale that
buffer to the window. Wine passes the game's mode change to the Mac, and the
Mac has no 640x480 mode.

### 2. Miles sound (`mss32.dll`)

See "The Miles fix" in `common/wine/README.md`.

## The 3D figures

The GOG `revenant.ini` has `Software3D=Yes`. The game then draws the 3D
figures with its own software renderer, and the sides that face away from
the light are black. The Wine version shows the same picture. The native
port reads this value as `No` and draws the figures with Direct3D.

## Problems

- `DDERR_GENERIC`: the display-mode fix did not load. Look at
  `dispmode_fix.log`. `_inmm.dll` (installed 2026-10-09: 59904 bytes,
  SHA-256 `e1c4ca3b...7a7c`; the file of 2026-03, 112358 bytes, SHA-256
  `c6f4161b...9494`, is kept as `_inmm.dll.bak-2026-03`) and
  `_inmm_real.dll` (86016 bytes, SHA-256 `7ff2f56d...67aa`) must be in
  `C:\Revenant`.
- The picture is small or cut: look for "Scaling on" and "Present:" in
  `dispmode_fix.log`. To go back to the old result, copy
  `_inmm.dll.bak-2026-03` to `_inmm.dll`, or set `Stretch=0` in
  `dispmode_fix.ini`.
- The game hangs at the start with no sound: the Miles fix is missing.
- No window in front: use Cmd+Tab or Mission Control to find the Wine
  window.
- `Launcher.exe` (Run Revenant, then Start Game in its Options window)
  writes `revenant.ini` again (through the link: `Settings/revenant.ini` of
  the version), with the values of that window: with the device "DirectDraw
  HAL" it sets `Software3D=No`, removes the empty lines and adds keys
  (`DisplayMode`, `Detail` and more). Keep a copy of `revenant.ini` before
  you use the launcher (seen on 2026-10-05; the file was put back).
- Revenant in the Linux test VM crashed in `smackw32.dll` without a sound
  device (see `linux-test-vm/README.md`).

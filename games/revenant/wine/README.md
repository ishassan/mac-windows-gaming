# Revenant on Wine (reference version)

The original Windows Revenant (GOG, version 1.22) on Homebrew Wine. It is a
reference for the native port ([../native-mac](../native-mac/README.md)).
The parts that all Wine versions share (Wine, folder layout, the Miles fix,
test methods) are in [../../../common/wine](../../../common/wine/README.md).

## Files here

| File | What it is |
|---|---|
| `app/Info.plist`, `app/launcher.sh` | The Wine app (`common/wine/make-app.sh`). |
| `prefix.reg` | The registry settings of the prefix. Import: `wine regedit /S prefix.reg` (with `WINEPREFIX`). |
| `play_revenant.sh` | Starts the Wine version from a terminal (put it in `~/Games/Revenant/Wine`). It does what the app does, but it does not add the links to `Game Data` and it starts Wine in the background. |
| `dispmode-fix/` | Source of the display-mode fix (`dispmode_fix.c`, `_inmm.def`). |

## Layout on the Mac

```
~/Games/Revenant/
  Revenant (Native).app
  Game Data/                     the GOG game, the saves (Save/), Curmap/, revenant.ini
  Wine/
    Revenant (Wine).app
    wineprefix_cx/               the Wine prefix
      drive_c/Revenant/          C:\Revenant: the Wine-only files and a link to each Game Data item
    revenant-launcher.log
```

The native app and Wine use the same game files and the same saves
(`Save/`), current map (`Curmap/`) and settings (`revenant.ini`).

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

1. Install Wine: `brew install --cask wine-crossover`.
2. Put the GOG game in `~/Games/Revenant/Game Data` (the files that the
   native port also needs).
3. Make the prefix:
   `WINEPREFIX="$HOME/Games/Revenant/Wine/wineprefix_cx" wineboot -i`.
4. Settings (with the same `WINEPREFIX`): `wine regedit /S games/revenant/wine/prefix.reg`.
   It sets `ddraw=builtin` (the Wine DirectDraw, not a replacement
   `ddraw.dll`) and the Mac driver value `ForceOpenGLBackingStore=y`.
5. Make `wineprefix_cx/drive_c/Revenant` and copy the Wine-only files above
   from the GOG install into it. Rename the GOG `_inmm.dll` to
   `_inmm_real.dll`.
6. Build the display-mode fix (needs `brew install mingw-w64`) and copy it
   into `C:\Revenant`:

   ```
   cd games/revenant/wine/dispmode-fix
   i686-w64-mingw32-gcc -shared -o _inmm.dll dispmode_fix.c _inmm.def -luser32 -lkernel32 -O2
   ```

7. Patch Miles: `common/wine/patch-miles.py "<prefix>/drive_c/Revenant/mss32.dll"`.
8. Make the app:
   `common/wine/make-app.sh games/revenant/wine/app "$HOME/Games/Revenant/Wine" "$HOME/Games/Revenant/Game Data/Revenant.icns"`.
9. Start the app. At each start it adds a link for each new `Game Data`
   item.

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
export to `_inmm_real.<name>` (the GOG DLL). When it loads, it hooks
`ChangeDisplaySettingsExW`. When a mode change fails, it tries again with
960x600 and 32-bit color. It writes what it does to
`C:\Revenant\dispmode_fix.log`. A good start shows:

```
=== dispmode_fix loaded ===
Hook installed
CDSEW: 640x480x16
  Failed(-2), trying 960x600x32 IN-PLACE
  960x600 OK (in-place)
```

Result: the 640x480 picture is in the top-left part of the screen, with
black at the right and the bottom.

### 2. Miles sound (`mss32.dll`)

See "The Miles fix" in `common/wine/README.md`.

## The 3D figures

The GOG `revenant.ini` has `Software3D=Yes`. The game then draws the 3D
figures with its own software renderer, and the sides that face away from
the light are black. The Wine version shows the same picture. The native
port reads this value as `No` and draws the figures with Direct3D.

## Problems

- `DDERR_GENERIC`: the display-mode fix did not load. Look at
  `dispmode_fix.log`. `_inmm.dll` (the installed file of 2026-03: 112358
  bytes, SHA-256 `c6f4161b...9494`) and `_inmm_real.dll` (86016 bytes,
  SHA-256 `7ff2f56d...67aa`) must be in `C:\Revenant`.
- The game hangs at the start with no sound: the Miles fix is missing.
- No window in front: use Cmd+Tab or Mission Control to find the Wine
  window.
- `Launcher.exe` (Run Revenant, then Start Game in its Options window)
  writes `revenant.ini` in `Game Data` again, with the values of that
  window: with the device "DirectDraw HAL" it sets `Software3D=No`, removes
  the empty lines and adds keys (`DisplayMode`, `Detail` and more). The
  native app reads the same file. Keep a copy of `revenant.ini` before you
  use the launcher (seen on 2026-10-05; the file was put back).
- Revenant in the Linux test VM crashed in `smackw32.dll` without a sound
  device (see `linux-test-vm/README.md`).

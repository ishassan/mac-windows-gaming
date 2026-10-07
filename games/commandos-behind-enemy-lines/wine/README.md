# Commandos: Behind Enemy Lines on Wine (reference version)

The original Windows Commandos (GOG) on Wine. It is a reference
for the native port ([../native-mac](../native-mac/README.md)). The parts
that all Wine versions share (Wine, folder layout, the Miles fix, test
methods) are in [../../../common/wine](../../../common/wine/README.md).

## Files here

| File | What it is |
|---|---|
| `app/Info.plist`, `app/launcher.sh` | The Wine app (`common/wine/make-app.sh`). |
| `prefix.reg` | The registry settings of the prefix. Import: `wine regedit /S prefix.reg` (with `WINEPREFIX`). |

## Layout on the Mac

```
~/Games/Commandos Behind Enemy Lines/
  Commandos Behind Enemy Lines (Native).app
  Game Data/                     the GOG game; saves in User/Pyro Studios/Commandos/OUTPUT
  Wine/
    Commandos Behind Enemy Lines (Wine).app
    wineprefix/                  the Wine prefix
      drive_c/GOG Games/Commandos/   the Wine-only files and a link to each Game Data item
    wine-launcher.log
```

Saves: the game keeps them in `My Documents\Pyro Studios`. In the prefix,
My Documents (`drive_c/users/<user>/Documents`) is a link to
`Game Data/User`, the folder that the native app uses. So both versions use
the same saves. The launcher makes this link before each start.

## Wine-only files (in `C:\GOG Games\Commandos`)

All except `ddraw.ini` come from the GOG install.

| File | What it does |
|---|---|
| `MSS32.DLL` | Miles sound, patched with `common/wine/patch-miles.py`. The original is `MSS32.DLL.orig`. |
| `ddraw.ini` | Settings of cnc-ddraw (see below). Keep it with the cnc-ddraw `ddraw.dll`. |
| `mpserver.exe`, `directplay.cmd` | Multiplayer server and DirectPlay setup. |
| `MSS16.DLL`, `mssb16.tsk` | Miles parts for Windows 95/98. |

## DirectDraw: cnc-ddraw

The prefix uses cnc-ddraw (https://github.com/FunkyFr3sh/cnc-ddraw), a
DirectDraw replacement, not the Wine DirectDraw:

- `ddraw.dll` is in the prefix's `windows/syswow64` (the 32-bit system
  folder). The installed file (2026-10-05): 358400 bytes, SHA-256
  `90babe01...bd13`, version info "cnc-ddraw", "DirectDraw replacement",
  file version 6.0.0.0. It was copied on 2026-10-05 from the old Porting
  Kit (Wineskin) app of Commandos. Which cnc-ddraw release this is was not
  checked. For a new setup, get `ddraw.dll` from the cnc-ddraw releases
  page.
- DLL override: `ddraw=native,builtin`.
- Direct3D renderer: `gl`.
- Result: intro and main menu in full screen (Linux test VM, 2026-10-05).
- On the Mac, the GOG `ddraw.ini` (`fullscreen=false`) shows a window. Since
  2026-10-07 the prefix's `ddraw.ini` has `fullscreen=true`: the picture
  fills the screen (athei Wine, 19.9 frames per second in the mission). The
  GOG file is kept next to it as `ddraw.ini.orig (window mode, 2026-10-07)`.

## Set up again

These steps repeat the result of the prefix of 2026-10-05 (the files and
the settings in it), not a recorded command history.

1. Install Wine: `common/wine/install-athei.sh` (the athei CrossOver 26.3
   build, see [`common/wine`](../../../common/wine/README.md)). In the steps
   below, `wine` is `"$HOME/Applications/Wine athei/wine/bin/wine"`, with
   `WINEDLLOVERRIDES="mscoree,mshtml="` set. (The installed prefix was made
   with Wine 8 and then updated by athei; a new prefix made with athei is
   not tested yet.)
2. Put the GOG game in `~/Games/Commandos Behind Enemy Lines/Game Data`.
3. Make the prefix:
   `WINEPREFIX="$HOME/Games/Commandos Behind Enemy Lines/Wine/wineprefix" wine wineboot -i`.
4. Settings (with the same `WINEPREFIX`): `wine regedit /S games/commandos-behind-enemy-lines/wine/prefix.reg`.
   It sets `ddraw=native,builtin`, the Direct3D renderer `gl`, and the Mac
   keys (Command as Ctrl, Option as Alt).
5. Put cnc-ddraw's `ddraw.dll` into `<prefix>/drive_c/windows/syswow64`.
6. Make `<prefix>/drive_c/GOG Games/Commandos` and copy the Wine-only files
   above from the GOG install into it.
7. Patch Miles:
   `common/wine/patch-miles.py "<prefix>/drive_c/GOG Games/Commandos/MSS32.DLL"`.
8. Make the app:
   `common/wine/make-app.sh games/commandos-behind-enemy-lines/wine/app "$HOME/Games/Commandos Behind Enemy Lines/Wine" "$HOME/Games/Commandos Behind Enemy Lines/Game Data/goggame-1207662193.ico"`.
9. Start the app. Start it before any other Wine program in this prefix:
   it sets the My Documents link first, else the game writes its saves
   into the Mac Documents folder (this happened once, on 2026-10-05).

Check (2026-10-05): in a new prefix in the Linux test VM, `prefix.reg`
imports with the same values as the installed prefix. `make-app.sh` makes an app that is the same as the
installed one (the signature excluded).

# Command & Conquer Generals Zero Hour on Wine (reference version)

The original Windows Zero Hour (the EA app / Origin copy) on Wine.
It is a reference for the native apps ([../native-mac](../native-mac/README.md),
[../open-source-port](../open-source-port/README.md)). This folder is for the
athei Wine (the default); [../wine-8](../wine-8/README.md) is the same setup
on Wine 8.
The parts that all Wine versions share (Wine, folder layout, test methods)
are in [../../../common/wine](../../../common/wine/README.md).

## Files here

| File | What it is |
|---|---|
| `app/Info.plist`, `app/launcher.sh` | The Wine app (`common/wine/make-app.sh`). The launcher is the same for both Wine versions: the name of the version folder picks the Wine. |
| `prefix.reg` | The registry settings of the prefix. Import: `wine regedit /S prefix.reg` (with `WINEPREFIX`). |

## Layout on the Mac

```
~/Games/Command and Conquer Generals Zero Hour/
├── Original Game Files
│   ├── Command and Conquer Generals             base game files (.big)
│   └── Command and Conquer Generals Zero Hour   Zero Hour files (.big)
├── Saves
│   ├── Zero Hour                  Zero Hour saves (shared with wine-8 and native-mac)
│   └── Generals                   base-game saves
├── native-mac                     our native port
├── open-source-port               GeneralsX (its own saves)
├── wine-11-athei
│   ├── Command & Conquer Generals Zero Hour (Wine).app
│   ├── Settings                   My Documents: options, maps, replays; Save → Saves
│   ├── wineprefix                 the Wine prefix
│   │   ├── drive_c/EA Games/Command and Conquer Generals             Wine-only files + links
│   │   └── drive_c/EA Games/Command and Conquer Generals Zero Hour   Wine-only files + links
│   └── wine-launcher.log
└── wine-8                         the same for Wine 8
```

- The app starts `game.dat`, the game program. `Generals.exe` is the EA app
  launcher, and it stops with "EA app is not installed".
- Both Wine game folders are real folders, with a link to each item in the
  matching `Original Game Files` folder.
- Settings and saves: My Documents in the prefix is a link to `Settings` of
  the version. In it, `Command and Conquer Generals Zero Hour Data/Save` is
  a link to `Saves/Zero Hour`, and `Command and Conquer Generals Data/Save`
  a link to `Saves/Generals`. The launcher makes these links before each
  start. The native port uses the same `Saves/Zero Hour`; GeneralsX keeps
  its own saves (its saves do not load in `game.dat`).
- The base game (without Zero Hour) can also run on Wine: its `game.dat` is
  in the base Wine game folder. There is no app for it.

## Wine-only files

All come from the EA app (Origin) install. They are in both Wine game
folders, except where noted.

| File | What it does |
|---|---|
| `game.dat` | The game program. |
| `mss32.dll`, `MSS/` | Miles sound (6.5c) and its plugins. Not patched: the game does not hang without the Miles fix. |
| `BINKW32.DLL` | Bink video player. |
| `Generals.exe`, `Core/` | The EA app launcher (it does not work on Wine; kept as part of the install). |
| `WorldBuilder.exe` | The map editor (needs MFC 4.2, see below). |
| `RedistInstallers/` | With `Options_Helper`. |
| `BrowserEngine.dll` (base game only), `P2XDLL.DLL`, `patchw32.dll`, `patchget.dat`, `dbghelp.dll`, `Generals.dat`, `generals.lcf`, `gp.info` (base game only), `SUN.INI` (base game only), `00000000.016`, `00000000.256` | Other files of the EA install: DLLs and data files of the online, patch and launcher parts (the use of each file was not checked). |
| `Generals.ico`, `GeneralsZH.ico` (Zero Hour), `Install_Final.bmp`, `launcher.bmp` | Icons, and the loading-screen and launcher images. |

## Settings in the prefix

- Registry, 32-bit view (`HKLM\Software\Wow6432Node\Electronic Arts\EA Games\...`):

  | Key | `InstallPath` | `Language` | `MapPackVersion` | `Version` |
  |---|---|---|---|---|
  | `Generals` | `C:\EA Games\Command and Conquer Generals\` | `english` | dword `0x10000` | dword `0x10004` |
  | `Command and Conquer Generals Zero Hour` | `C:\EA Games\Command and Conquer Generals Zero Hour\` | `english` | dword `0x10000` | dword `0x10004` |

- `HKCU\Software\Wine\Direct3D\renderer` = `gl`. With the default renderer,
  the screen stays black after the loading screen.
- `HKCU\Software\Wine\Mac Driver\LeftCommandIsCtrl` = `Y`.
- MFC 4.2 for `WorldBuilder.exe`: `winetricks mfc42` (Homebrew `winetricks`)
  put `mfc42.dll` and `mfc42u.dll` into the prefix's `windows/syswow64`
  (2026-10-05).

## Set up again

These steps repeat the result of the prefix of 2026-10-05 (the files and
the settings in it), not a recorded command history.

1. Install Wine: `common/wine/install-athei.sh` (the athei CrossOver 26.3
   build, see [`common/wine`](../../../common/wine/README.md)). In the steps
   below, `wine` is `"$HOME/Applications/Wine athei/wine/bin/wine"`, with
   `WINEDLLOVERRIDES="mscoree,mshtml="` set. (The installed prefix was made
   with Wine 8 and then updated by athei; a new prefix made with athei is
   not tested yet.) For the map editor also
   `brew install winetricks`.
2. Put the `.big` files of the base game and Zero Hour in the two
   `Original Game Files` folders (the native apps need them too).
3. Make the prefix:
   `WINEPREFIX="$HOME/Games/Command and Conquer Generals Zero Hour/wine-11-athei/wineprefix" wine wineboot -i`.
4. Settings (with the same `WINEPREFIX`): `wine regedit /S games/command-and-conquer-generals-zero-hour/wine-11-athei/prefix.reg`.
   It sets the two EA Games keys above, the Direct3D renderer `gl`,
   `LeftCommandIsCtrl` and `EmulateModeset` (full screen on athei).
5. Make the two Wine game folders under `<prefix>/drive_c/EA Games` and copy
   the Wine-only files above from the install into them.
6. For the map editor: `winetricks mfc42` (with the same `WINEPREFIX`, and
   `WINE` set to the athei `wine`; done with Wine 8 on 2026-10-05, not
   tested with athei).
7. Make the app:
   `common/wine/make-app.sh games/command-and-conquer-generals-zero-hour/wine-11-athei/app "$HOME/Games/Command and Conquer Generals Zero Hour/wine-11-athei" "<Zero Hour Wine game folder>/GeneralsZH.ico"`.
8. Start the app. It sets the My Documents and Save links and adds the
   links to `Original Game Files` before it starts the game.

Check (2026-10-05): in a new prefix in the Linux test VM, `prefix.reg`
imports with the same values as the installed prefix. `make-app.sh` makes an app with the same `Info.plist`
and launcher as the installed one. The icon is different: the installed
icon was made in another way that was not recorded.

## Results

| Test | Result |
|---|---|
| Mac, 2026-10-05 | The loading screen and the 3D battle behind the main menu show. Loading takes about 2 minutes. The menu buttons and a game were not tested. |
| Linux test VM, 2026-10-05 | 3D shell map behind the main menu. |
| `WorldBuilder.exe` in the Linux test VM, 2026-10-05 | Starts (MFC42 works). After EA's license dialog (accepted in the VM copy), the editor opens with the 3D terrain view and its tool windows. On the Mac the dialog still comes up once. |
| Mac, speed, 2026-10-06 | Menu scene at 1280x800, CPU cores and median frames per second: native 0.46 cores (about 70 fps); Wine 8 2.22 cores, 21.8 fps; Wine 8 with `csmt=0` 1.18 cores, 22.6 fps; Wine 11 1.76 cores, 15.5 fps; Wine 11 with `csmt=0` 1.20 cores, 15.2 fps. Test copy of the prefix only; the real prefix keeps the default. |
| Mac, Wine 11 (patched Wine Stable), 2026-10-06 | The ground draws white like snow, with `csmt` on and off. Do not use plain Wine 11 for this game (the athei build draws the ground correctly). Cause: in the Wine 11 Direct3D code (a texture stage has no texture when the ground is drawn); the game makes and fills the ground texture the same way on both Wines. In the Linux test VM (Wine 11.16, Mesa OpenGL) the ground is correct, so the bug shows only on the Mac. Wine 11.16 on the Mac also draws it white, so the cause is the Mac side (Apple's OpenGL or the Mac display driver of Wine), not the Wine version. See "Wine 8 and Wine 11 compared" in `common/wine/README.md`. |
| Mac, saves, 2026-10-06 | The Windows save "GLA 5" loads ($6900); GeneralsX with the save fix loads the same save with the same state. |
| Mac, saves, 2026-10-08 | Our native port (`game.dat` recompiled) lists the Windows saves of `Saves/Zero Hour` and loads them. A save made by GeneralsX gives "Error loading game" in it, so GeneralsX keeps its own saves. |

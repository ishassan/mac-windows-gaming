# Command & Conquer Generals Zero Hour on Wine 8 (to compare)

The same Windows version as [../wine-11-athei](../wine-11-athei/README.md),
on Wine 8: the Homebrew cask `wine-crossover` 23.7.1 (CrossOver 23.7.1, Wine
8.0.1, command `/opt/homebrew/bin/wine`). The athei Wine is the default; this
version is kept to compare the two. The setup, the Wine-only files and the
registry settings are the ones of `../wine-11-athei`.

## Files here

| File | What it is |
|---|---|
| `app/Info.plist` | The app name "Command & Conquer Generals Zero Hour (Wine 8)". |
| `app/launcher.sh` | A link to `../../wine-11-athei/app/launcher.sh`: one script for both versions. In the `wine-8` folder it starts Wine 8. |

## Layout on the Mac

```
~/Games/Command and Conquer Generals Zero Hour/wine-8/
├── Command & Conquer Generals Zero Hour (Wine 8).app
├── wineprefix          the Wine 8 prefix
├── Settings            the settings of this version
└── wine-launcher.log
```

- `wineprefix`: a copy of the prefix as it was before the move to athei
  (2026-10-07). Do not open it with the athei Wine: athei updates a prefix,
  and the update cannot be undone.
- `Settings/`: My Documents of this version (options, maps, replays; a copy of the wine-11-athei one of 2026-10-08). Its `Save` folders are links to the shared `Saves`.
- Full screen works without `EmulateModeset` on Wine 8 (the setting in `../wine-11-athei/prefix.reg` is for athei). On Wine 8 the full-screen picture is a window layer at 1280x800.
- The game files (`Original Game Files`) and the saves (`Saves`) are shared
  with the other versions.

## Make the app

```sh
common/wine/make-app.sh games/command-and-conquer-generals-zero-hour/wine-8/app "$HOME/Games/Command and Conquer Generals Zero Hour/wine-8" "$HOME/Games/Command and Conquer Generals Zero Hour/wine-8/wineprefix/drive_c/EA Games/Command and Conquer Generals Zero Hour/GeneralsZH.ico"
```

# Revenant on Wine 8 (to compare)

The same Windows version as [../wine-11-athei](../wine-11-athei/README.md),
on Wine 8: the Homebrew cask `wine-crossover` 23.7.1 (CrossOver 23.7.1, Wine
8.0.1, command `/opt/homebrew/bin/wine`). The athei Wine is the default; this
version is kept to compare the two. The setup, the Wine-only files and the
registry settings are the ones of `../wine-11-athei`.

## Files here

| File | What it is |
|---|---|
| `app/Info.plist` | The app name "Revenant (Wine 8)". |
| `app/launcher.sh` | A link to `../../wine-11-athei/app/launcher.sh`: one script for both versions. In the `wine-8` folder it starts Wine 8. |

## Layout on the Mac

```
~/Games/Revenant/wine-8/
├── Revenant (Wine 8).app
├── wineprefix          the Wine 8 prefix
├── Settings            the settings of this version
└── wine-launcher.log
```

- `wineprefix`: a copy of the prefix as it was before the move to athei
  (2026-10-07). Do not open it with the athei Wine: athei updates a prefix,
  and the update cannot be undone.
- `Settings/`: `revenant.ini` and `Curmap/` of this version (a copy of the shared ones of 2026-10-08).
- The game files (`Original Game Files`) and the saves (`Saves`) are shared
  with the other versions.

## Make the app

```sh
common/wine/make-app.sh games/revenant/wine-8/app "$HOME/Games/Revenant/wine-8" "$HOME/Games/Revenant/Original Game Files/Revenant.icns"
```

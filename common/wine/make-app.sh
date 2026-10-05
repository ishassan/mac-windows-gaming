#!/bin/bash
# Make the Wine app of a game ("<Game> (Wine).app") from the files in its
# repository folder <game>/wine/app:
#   Info.plist   the app name (CFBundleDisplayName), program name
#                (CFBundleExecutable) and icon name (CFBundleIconFile)
#   launcher.sh  the program: it sets WINEPREFIX, the Wine user folders and
#                the links to Game Data, and starts the game
#
# Usage: common/wine/make-app.sh <game>/wine/app <output folder> [icon file]
#   output folder  the game's Wine folder, for example
#                  "$HOME/Games/Revenant/Wine". The launcher finds the Wine
#                  prefix next to the app and the game in ../Game Data.
#   icon file      .icns, or an image that sips can read (.ico, .png). The
#                  game's README tells which file to use. Without it, the app
#                  has no icon.
#
# The app gets an ad-hoc signature (codesign -s -). A launcher that you edit
# inside the app needs a new signature: codesign -s - --force "<app>".
set -euo pipefail

SRC="${1:?usage: make-app.sh <game>/wine/app <output folder> [icon file]}"
OUT="${2:?usage: make-app.sh <game>/wine/app <output folder> [icon file]}"
ICON="${3:-}"
PLIST="$SRC/Info.plist"
[ -f "$PLIST" ] && [ -f "$SRC/launcher.sh" ] || { echo "$SRC must hold Info.plist and launcher.sh" >&2; exit 1; }
[ -d "$OUT" ] || { echo "output folder not found: $OUT" >&2; exit 1; }

NAME="$(plutil -extract CFBundleDisplayName raw "$PLIST")"
EXE="$(plutil -extract CFBundleExecutable raw "$PLIST")"
ICON_NAME="$(plutil -extract CFBundleIconFile raw "$PLIST")"
APP="$OUT/$NAME.app"

# Replace only an app that is a Wine app of this kind (a script as program).
if [ -e "$APP" ]; then
    if ! file -b "$APP/Contents/MacOS/$EXE" 2>/dev/null | grep -q "shell script"; then
        echo "$APP exists and is not a Wine app made by this script: not replaced" >&2
        exit 1
    fi
    rm -rf "$APP"
fi

mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$PLIST" "$APP/Contents/Info.plist"
cp "$SRC/launcher.sh" "$APP/Contents/MacOS/$EXE"
chmod 755 "$APP/Contents/MacOS/$EXE"

if [ -n "$ICON" ]; then
    case "$ICON" in
        *.icns) cp "$ICON" "$APP/Contents/Resources/$ICON_NAME.icns" ;;
        *)
            tmp=$(mktemp -d)
            sips -s format png "$ICON" --out "$tmp/icon.png" > /dev/null
            sips -s format icns "$tmp/icon.png" --out "$APP/Contents/Resources/$ICON_NAME.icns" > /dev/null
            rm -rf "$tmp" ;;
    esac
fi

codesign --force --sign - "$APP"
echo "made $APP"

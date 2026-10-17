#!/bin/bash
# Make the three Medal of Honor apps (Allied Assault, Spearhead, Breakthrough)
# from the OpenMoHAA release for macOS and the files in app/. The result is in
# build/ of this folder. This script does not install the apps: see install.sh.
#
# Usage: make-app.sh [GOG installer .exe]
#   With the GOG installer (setup_medal_of_honor_*.exe, with its .bin parts in
#   the same folder), the apps get the game icons from it. Without it, the
#   apps have no icon.
# Needs: curl, ditto, codesign; innoextract (Homebrew) and sips for the icons.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/build"
VERSION=0.82.1
ZIP="openmohaa-v$VERSION-macos-multiarch-arm64-x86_64.zip"
URL="https://github.com/openmoh/openmohaa/releases/download/v$VERSION/$ZIP"
SHA256=dd590e887078912e0bd80ad3e9d414ef7611faada064e7e49b4c3eafa59e579a
INSTALLER="${1:-}"

mkdir -p "$OUT"
if [[ ! -f "$OUT/$ZIP" ]]; then
    curl -fsSL -o "$OUT/$ZIP.part" "$URL"
    mv "$OUT/$ZIP.part" "$OUT/$ZIP"
fi
echo "$SHA256  $OUT/$ZIP" | shasum -a 256 -c - >/dev/null || { echo "ERROR: wrong checksum: $OUT/$ZIP" >&2; exit 1; }
rm -rf "$OUT/openmohaa" "$OUT/icons"
mkdir -p "$OUT/openmohaa" "$OUT/icons"
ditto -x -k "$OUT/$ZIP" "$OUT/openmohaa"
# The zip does not keep the exec bit.
chmod +x "$OUT/openmohaa/openmohaa"

if [[ -n "$INSTALLER" ]]; then
    innoextract -s -d "$OUT/icons" -I app/gfw_high.ico -I app/gfw_high_spearhead.ico \
        -I app/gfw_high_breakthrough.ico "$INSTALLER"
    for i in gfw_high gfw_high_spearhead gfw_high_breakthrough; do
        sips -s format icns "$OUT/icons/app/$i.ico" --out "$OUT/icons/$i.icns" >/dev/null
    done
fi

make_app() { # $1 app name, $2 target game (com_target_game), $3 icon name
    local app="$OUT/$1.app"
    rm -rf "$app"
    mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
    cp -p "$OUT/openmohaa/openmohaa" "$OUT"/openmohaa/*.dylib "$app/Contents/MacOS/"
    sed -e "s/@APP_NAME@/$1/g" -e "s/@TARGET_GAME@/$2/g" "$HERE/app/start.sh" > "$app/Contents/MacOS/start"
    chmod +x "$app/Contents/MacOS/start"
    sed -e "s/@APP_NAME@/$1/g" -e "s/@TARGET_GAME@/$2/g" -e "s/@VERSION@/$VERSION/g" \
        "$HERE/app/Info.plist" > "$app/Contents/Info.plist"
    [[ -f "$OUT/icons/$3.icns" ]] && cp "$OUT/icons/$3.icns" "$app/Contents/Resources/game.icns"
    codesign --force --deep -s - "$app" 2>/dev/null
    codesign --verify --deep "$app"
    echo "$app"
}
make_app "Medal of Honor Allied Assault" 0 gfw_high
make_app "Medal of Honor Allied Assault Spearhead" 1 gfw_high_spearhead
make_app "Medal of Honor Allied Assault Breakthrough" 2 gfw_high_breakthrough

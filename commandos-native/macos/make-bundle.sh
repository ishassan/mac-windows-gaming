#!/bin/bash
# Make the app bundle "Commandos Behind Enemy Lines (Native).app" from build/Commandos.
#
# Usage: macos/make-bundle.sh [output folder]     (default: build)
#
# The bundle contains the arm64 program, the SDL2 (sdl2-compat) and SDL3
# libraries from the conda env, and an icon made from the GOG icon in the
# game folder. The game data stays outside the bundle: the program looks in
# $COMMANDOS_DATA, else in ~/Games/Commandos Behind Enemy Lines/Game Data.
# The bundle gets an ad-hoc signature (codesign -s -). No system settings change.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/env.sh

OUT="${1:-build}"
NAME="Commandos Behind Enemy Lines (Native).app"
APP="$OUT/$NAME"
DATA="${COMMANDOS_DATA:-$HOME/Games/Commandos Behind Enemy Lines/Game Data}"
ICO="$DATA/goggame-1207662193.ico"

[ -x build/Commandos ] || { echo "build/Commandos not found: run make first" >&2; exit 1; }
[ -d "$OUT" ] || { echo "output folder not found: $OUT" >&2; exit 1; }

# Replace only a bundle that this script made before.
if [ -e "$APP" ]; then
    if [ ! -f "$APP/Contents/MacOS/Commandos" ] || [ ! -f "$APP/Contents/Frameworks/libSDL3.dylib" ]; then
        echo "$APP exists and was not made by this script: not replaced" >&2
        exit 1
    fi
    rm -rf "$APP"
fi

mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
cp macos/Info.plist "$APP/Contents/Info.plist"
cp build/Commandos "$APP/Contents/MacOS/Commandos"

# sdl2-compat loads SDL3 from "@loader_path/libSDL3.dylib".
cp "$CONDA_PREFIX/lib/libSDL2-2.0.0.dylib" "$APP/Contents/Frameworks/libSDL2-2.0.0.dylib"
cp "$CONDA_PREFIX/lib/libSDL3.0.dylib" "$APP/Contents/Frameworks/libSDL3.dylib"
chmod u+w "$APP/Contents/Frameworks/"*.dylib
install_name_tool -delete_rpath "$CONDA_PREFIX/lib" \
                  -add_rpath "@executable_path/../Frameworks" \
                  "$APP/Contents/MacOS/Commandos"

if [ -f "$ICO" ]; then
    tmp=$(mktemp -d)
    sips -s format png "$ICO" --out "$tmp/icon.png" > /dev/null
    sips -s format icns "$tmp/icon.png" --out "$APP/Contents/Resources/Commandos.icns" > /dev/null
    rm -rf "$tmp"
else
    echo "warning: $ICO not found, the bundle has no icon" >&2
fi

codesign --force --sign - "$APP/Contents/Frameworks/libSDL3.dylib" "$APP/Contents/Frameworks/libSDL2-2.0.0.dylib"
codesign --force --sign - "$APP"
echo "made $APP"

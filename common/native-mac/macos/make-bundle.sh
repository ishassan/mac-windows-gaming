#!/bin/bash
# Make the app bundle "$APP_NAME.app" from build/$GAME_NAME of a game folder.
#
# Run in a game folder. Usage: ../../../common/native-mac/macos/make-bundle.sh [output folder]
# (default output folder: build)
#
# Uses from the game folder: game.conf (GAME_NAME, APP_NAME, APP_ICON_SOURCE,
# GAME_DIR_DEFAULT) and macos/Info.plist.
#
# The bundle contains the arm64 program, the SDL2 (sdl2-compat), SDL3 and
# FreeType (with libpng and zlib) libraries from the conda env, the libraries
# of EXTRA_DYLIBS and DXVK_LIB_DIR (game.conf), and an icon
# made from the icon file in the
# game folder (APP_ICON_SOURCE: .ico or .icns). The game data stays outside
# the bundle. The bundle gets an ad-hoc signature (codesign -s -). No system
# settings change.
set -euo pipefail
. "$(dirname "$0")/../tools/game-conf.sh"
. "$COMMON/tools/env.sh"

OUT="${1:-build}"
APP="$OUT/$APP_NAME.app"
PREFIX_VAR="$(echo "$GAME_NAME" | tr '[:lower:]' '[:upper:]')_DATA"
DATA="${!PREFIX_VAR:-$HOME/$GAME_DIR_DEFAULT}"
ICON="$DATA/$APP_ICON_SOURCE"

[ -x "build/$GAME_NAME" ] || { echo "build/$GAME_NAME not found: run make first" >&2; exit 1; }
[ -d "$OUT" ] || { echo "output folder not found: $OUT" >&2; exit 1; }

# Replace only a bundle that this script made before.
if [ -e "$APP" ]; then
    if [ ! -f "$APP/Contents/MacOS/$GAME_NAME" ] || [ ! -f "$APP/Contents/Frameworks/libSDL3.dylib" ]; then
        echo "$APP exists and was not made by this script: not replaced" >&2
        exit 1
    fi
    rm -rf "$APP"
fi

mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
cp macos/Info.plist "$APP/Contents/Info.plist"
cp "build/$GAME_NAME" "$APP/Contents/MacOS/$GAME_NAME"

# sdl2-compat loads SDL3 from "@loader_path/libSDL3.dylib".
cp "$CONDA_PREFIX/lib/libSDL2-2.0.0.dylib" "$APP/Contents/Frameworks/libSDL2-2.0.0.dylib"
cp "$CONDA_PREFIX/lib/libSDL3.0.dylib" "$APP/Contents/Frameworks/libSDL3.dylib"
# FreeType (GDI text) and the libraries it loads
for lib in libfreetype.6.dylib libpng16.16.dylib libz.1.dylib; do
    cp "$CONDA_PREFIX/lib/$lib" "$APP/Contents/Frameworks/$lib"
done
# Other conda libraries the program uses (EXTRA_DYLIBS in game.conf, names in
# $CONDA_PREFIX/lib), and the @rpath libraries that they use in turn. The conda
# libraries have the rpath @loader_path/, so they find each other in Frameworks.
todo="${EXTRA_DYLIBS:-}"
while [ -n "$todo" ]; do
    next=""
    for lib in $todo; do
        # the C++ library of macOS is used, not the conda one (see below)
        [ "$lib" = "libc++.1.dylib" ] && continue
        [ -e "$APP/Contents/Frameworks/$lib" ] && continue
        cp "$CONDA_PREFIX/lib/$lib" "$APP/Contents/Frameworks/$lib"
        next="$next $(otool -L "$CONDA_PREFIX/lib/$lib" | awk 'NR > 1 && $1 ~ /^@rpath\// { sub("@rpath/", "", $1); print $1 }')"
    done
    todo="$next"
done
# Conda libraries link @rpath/libc++.1.dylib (the conda C++ library). Use the
# macOS one: two C++ libraries in one process do not work together.
for f in "$APP/Contents/Frameworks/"*.dylib; do
    if otool -L "$f" | grep -q "@rpath/libc++.1.dylib"; then
        chmod u+w "$f"
        install_name_tool -change @rpath/libc++.1.dylib /usr/lib/libc++.1.dylib "$f" 2>/dev/null
    fi
done
# Direct3D 8 (DXVK_LIB_DIR in game.conf): DXVK, the Vulkan loader and MoltenVK,
# as the community port builds them (lib/ of its app: no rpaths outside the app).
# The runtime loads them from Frameworks (runtime/d3d8/WinApi-d3d8.c).
if [ -n "${DXVK_LIB_DIR:-}" ]; then
    for lib in libvulkan.1.dylib libMoltenVK.dylib libdxvk_d3d9.0.dylib libdxvk_d3d8.0.dylib; do
        [ -f "$DXVK_LIB_DIR/$lib" ] || { echo "$DXVK_LIB_DIR/$lib not found: build the community port first" >&2; exit 1; }
        cp "$DXVK_LIB_DIR/$lib" "$APP/Contents/Frameworks/$lib"
    done
    cat > "$APP/Contents/Resources/MoltenVK_icd.json" <<'JSON'
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "../Frameworks/libMoltenVK.dylib",
        "api_version": "1.4.0",
        "is_portability_driver": true
    }
}
JSON
fi
chmod u+w "$APP/Contents/Frameworks/"*.dylib
install_name_tool -delete_rpath "$CONDA_PREFIX/lib" \
                  -add_rpath "@executable_path/../Frameworks" \
                  "$APP/Contents/MacOS/$GAME_NAME"

if [ -f "$ICON" ]; then
    case "$ICON" in
        *.icns) cp "$ICON" "$APP/Contents/Resources/$GAME_NAME.icns" ;;
        *)
            tmp=$(mktemp -d)
            sips -s format png "$ICON" --out "$tmp/icon.png" > /dev/null
            sips -s format icns "$tmp/icon.png" --out "$APP/Contents/Resources/$GAME_NAME.icns" > /dev/null
            rm -rf "$tmp" ;;
    esac
else
    echo "warning: $ICON not found, the bundle has no icon" >&2
fi

codesign --force --sign - "$APP/Contents/Frameworks/"*.dylib
codesign --force --sign - "$APP"
echo "made $APP"

#!/bin/bash
# Make the three Medal of Honor apps (Allied Assault, Spearhead, Breakthrough)
# from our fork of OpenMoHAA (the submodule openmohaa/, branch "custom") and
# the files in app/. The result is in build/ of this folder. This script does
# not install the apps: see install.sh.
#
# The engine (openmohaa, game.dylib, cgame.dylib) is built from the fork. The
# libraries SDL2, OpenAL and curl come from the official release for macOS:
# they are built for the Mac with no other dependencies, and the engine links
# them by @rpath (SDL2) or loads them at run time (OpenAL, curl).
#
# Usage: make-app.sh [GOG installer .exe]
#   With the GOG installer (setup_medal_of_honor_*.exe, with its .bin parts in
#   the same folder), the apps get the game icons from it. Without it, the
#   apps have no icon.
# Needs: the conda env of the repository (environment.yml: cmake, ninja, sdl2,
#   openal-soft, bison, flex), Xcode command line tools (clang), curl, ditto,
#   codesign; innoextract (Homebrew) and sips for the icons.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SRC="$HERE/openmohaa"
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
rm -rf "$OUT/release" "$OUT/openmohaa" "$OUT/icons"
mkdir -p "$OUT/release" "$OUT/openmohaa" "$OUT/icons"
ditto -x -k "$OUT/$ZIP" "$OUT/release"
cp -p "$OUT"/release/libSDL2-2.0.0.dylib "$OUT"/release/libopenal.1.dylib "$OUT"/release/libcurl.4.dylib "$OUT/openmohaa/"

if [[ ! -f "$SRC/CMakeLists.txt" ]]; then
    echo "ERROR: $SRC is empty. Run: git submodule update --init" >&2
    exit 1
fi
. "$ROOT/common/native-mac/tools/env.sh"
echo "Source: $(git -C "$SRC" branch --show-current 2>/dev/null || true) $(git -C "$SRC" log --oneline -1)"
export MACOSX_DEPLOYMENT_TARGET=11.0
[[ -f "$OUT/cmake/build.ninja" ]] || cmake -S "$SRC" -B "$OUT/cmake" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_INSTALL_PREFIX="$OUT/install" \
    -DSDL2_DIR="$CONDA_PREFIX/lib/cmake/SDL2" \
    -DOPENAL_LIBRARY="$CONDA_PREFIX/lib/libopenal.dylib" \
    -DOPENAL_INCLUDE_DIR="$CONDA_PREFIX/include/AL" \
    -DCURL_ROOT="$CONDA_PREFIX" \
    -DBISON_EXECUTABLE="$CONDA_PREFIX/bin/bison" -DFLEX_EXECUTABLE="$CONDA_PREFIX/bin/flex" \
    -DPRODUCT_VERSION_STAGE=custom > "$OUT/cmake-configure.log"
cmake --build "$OUT/cmake" -j"$(( ($(sysctl -n hw.logicalcpu) + 1) / 2 ))" > "$OUT/cmake-build.log" \
    || { tail -20 "$OUT/cmake-build.log"; exit 1; }
cmake --install "$OUT/cmake" > /dev/null
cp -p "$OUT"/install/lib/openmohaa/openmohaa "$OUT"/install/lib/openmohaa/game.dylib \
    "$OUT"/install/lib/openmohaa/cgame.dylib "$OUT/openmohaa/"

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

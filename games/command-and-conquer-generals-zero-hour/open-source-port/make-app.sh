#!/bin/bash
# Build the native Zero Hour app from our fork (the submodule GeneralsX/, normally the branch
# "custom") and apply the local changes of README.md ("Local changes to the app", steps 1 to 4).
# The result is "build/Command & Conquer Generals Zero Hour.app" in this folder.
# This script does not install the app. See README.md for the install steps.
#
# Usage: games/command-and-conquer-generals-zero-hour/open-source-port/make-app.sh
# Needs: the conda env of the repository (environment.yml), git, Xcode command line tools.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SRC="$HERE/GeneralsX"
GAME="Command and Conquer Generals Zero Hour"
APP_NAME="Command & Conquer Generals Zero Hour"
OUT="$HERE/build"
# vcpkg (the library builder of the port): a pinned commit of the official repository, in the
# ignored build/ folder of the repository root.
VCPKG_COMMIT=13465a7b726f171350defa5368b901a52f7a6af5

. "$ROOT/common/native-mac/tools/env.sh"
export VCPKG_ROOT="$ROOT/build/vcpkg"
export VULKAN_SDK="$CONDA_PREFIX"   # Vulkan loader and MoltenVK from the conda env

if [[ ! -f "$SRC/CMakeLists.txt" ]]; then
    echo "ERROR: $SRC is empty. Run: git submodule update --init" >&2
    exit 1
fi

if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
    git clone -q https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
    git -C "$VCPKG_ROOT" checkout -q "$VCPKG_COMMIT"
    "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
fi

cd "$SRC"
echo "Source: $(git branch --show-current 2>/dev/null || true) $(git log --oneline -1)"
[[ -f build/macos-vulkan/build.ninja ]] || cmake --preset macos-vulkan
cmake --build build/macos-vulkan --target z_generals sage_patch -j"$(( ($(sysctl -n hw.logicalcpu) + 1) / 2 ))"
mkdir -p "$OUT"
./scripts/build/macos/bundle-macos-zh.sh > "$OUT/bundle.log" 2>&1 || { tail -20 "$OUT/bundle.log"; exit 1; }

# Unpack the bundle, then apply the local changes.
rm -rf "$OUT/stage"
mkdir -p "$OUT/stage"
unzip -q macos-arm64-GeneralsXZH.zip -d "$OUT/stage"
rm -f macos-arm64-GeneralsXZH.zip
C="$OUT/stage/GeneralsXZH.app/Contents"

# Step 2: Info.plist
plutil -replace CFBundleName -string "$APP_NAME" "$C/Info.plist"
plutil -replace CFBundleDisplayName -string "$APP_NAME" "$C/Info.plist"
plutil -replace CFBundleIdentifier -string "local.games.command-and-conquer-generals-zero-hour" "$C/Info.plist"
plutil -replace CFBundleIconFile -string "$GAME.png" "$C/Info.plist"

# Step 3: file names
mv "$C/Resources/bin/GeneralsXZH" "$C/Resources/bin/$GAME"
mv "$C/Resources/generalsx-zh_icon.png" "$C/Resources/$GAME.png"
rm "$C/MacOS/GeneralsXZH"
ln -s run.sh "$C/MacOS/$GAME"

# Step 4: run.sh finds the game files in "Original Game Files" of the game folder, keeps the DXVK
# cache and log in the Settings folder next to the app, and starts the renamed binary.
python3 - "$C/MacOS/run.sh" "$GAME" "$HERE/run-local.sh" <<'EOF'
import re, sys
path, game = sys.argv[1], sys.argv[2]
LOCAL_BLOCK = open(sys.argv[3]).read()
text = open(path).read()
block = re.compile(
    r'# GeneralsX @bugfix BenderAI 01/04/2026 Select default Zero Hour asset path.*?\n'
    r'(?=\n# Backward compatibility for existing runtime readers)', re.S)
local = LOCAL_BLOCK
text, count = block.subn(lambda m: local, text)
if count != 1:
    sys.exit("run.sh: the asset path block was not found; compare run.sh with README.md step 4")
old = '"${BIN_DIR}/GeneralsXZH"'
if text.count(old) != 1:
    sys.exit("run.sh: the binary line was not found")
text = text.replace(old, '"${BIN_DIR}/%s"' % game)
open(path, 'w').write(text)
EOF

# Step 5: the font cache goes to the Settings folder (through the GeneralsX link in
# ~/Library/Application Support), not to a "var" folder in the game folder.
python3 - "$C/Resources/fontconfig/fonts.conf" <<'EOF'
import sys
path = sys.argv[1]
text = open(path).read()
old = '<cachedir>./../../var/cache/fontconfig</cachedir>'
if text.count(old) != 1:
    sys.exit("fonts.conf: the cache folder line was not found; compare it with README.md step 5")
text = text.replace(old, '<cachedir>~/Library/Application Support/GeneralsX/fontconfig-cache</cachedir>')
open(path, 'w').write(text)
EOF

# Build only (a downloaded release does not need this): keep the app self-contained.
# - MoltenVK from the conda env links @rpath/libc++.1.dylib (the conda libc++). Use the libc++
#   of macOS. Do not copy the conda one into the app: the launcher puts lib/ in
#   DYLD_LIBRARY_PATH, and then every library would load it in place of the system one.
# - Remove the rpaths to folders outside the app (build folders and the conda env), so the app
#   never loads a library from them.
L="$C/Resources/lib"
install_name_tool -change @rpath/libc++.1.dylib /usr/lib/libc++.1.dylib "$L/libMoltenVK.dylib"
codesign -f -s - "$L/libMoltenVK.dylib" 2>/dev/null
for f in "$C/Resources/bin/$GAME" "$L"/*.dylib; do
    [[ -L "$f" ]] && continue
    changed=0
    while IFS= read -r r; do
        install_name_tool -delete_rpath "$r" "$f" 2>/dev/null
        changed=1
    done < <(otool -l "$f" | awk '$1 == "cmd" && $2 == "LC_RPATH" { getline; getline; print $2 }' | grep '^/' || true)
    [[ $changed == 1 ]] && codesign -f -s - "$f" 2>/dev/null
done
# Check: every @rpath library must be in the app.
for f in "$C/Resources/bin/$GAME" "$L"/*.dylib; do
    id="$(otool -D "$f" | tail -n +2)"
    for d in $(otool -L "$f" | awk -v id="$id" 'NR > 1 && $1 ~ /^@rpath\// && $1 != id { sub("@rpath/", "", $1); print $1 }'); do
        [[ -e "$L/$d" ]] || { echo "ERROR: $(basename "$f") needs $d, which is not in the app" >&2; exit 1; }
    done
done

# Step 1: the app name
rm -rf "$OUT/$APP_NAME.app"
mv "$OUT/stage/GeneralsXZH.app" "$OUT/$APP_NAME.app"
rm -rf "$OUT/stage"
echo "Made: $OUT/$APP_NAME.app"

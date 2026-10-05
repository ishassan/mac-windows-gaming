#!/bin/sh
# Install the official LDC (D compiler) release into the active conda env.
# Needed only to build the llasm tool. Removing the env removes LDC too.
set -e
: "${CONDA_PREFIX:?run ". common/native-mac/tools/env.sh" first}"
VER=1.43.0
NAME=ldc2-$VER-osx-arm64
SHA=5e332349c784d4e0b1c4622284803880f89d97ef1ef97c6b25aa4d6b094c5fed
DEST="$CONDA_PREFIX/opt"
if [ -x "$DEST/$NAME/bin/ldc2" ]; then echo "LDC $VER already installed"; exit 0; fi
mkdir -p "$DEST"
TMP=$(mktemp -d)
curl -fsSL -o "$TMP/$NAME.tar.xz" "https://github.com/ldc-developers/ldc/releases/download/v$VER/$NAME.tar.xz"
echo "$SHA  $TMP/$NAME.tar.xz" | shasum -a 256 -c -
tar -xJf "$TMP/$NAME.tar.xz" -C "$DEST"
rm -rf "$TMP"
"$DEST/$NAME/bin/ldc2" --version | head -1

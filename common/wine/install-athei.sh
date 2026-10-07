#!/bin/bash
# Install the default Wine of the Wine apps: the athei CrossOver 26.3 build
# (Wine 11, github.com/athei/wine-build), the x87sidecar math helper
# (github.com/athei/x87sidecar), and qos.dylib (common/wine/qos.c).
#
# Usage: common/wine/install-athei.sh [folder]
#   folder  default: "$HOME/Applications/Wine athei". The Wine apps look there.
#
# Result:
#   <folder>/wine/            the Wine build (bin/wine, bin/wineserver, lib/)
#   <folder>/x87sidecar       the math helper (ROSETTA_X87_PATH)
#   <folder>/qos.dylib        thread QoS fix (DYLD_INSERT_LIBRARIES)
#   <folder>/VERSION          the release names and checksums
#
# The downloads must match the sha256 values below (pinned releases; scanned
# for malicious code on 2026-10-07, see common/wine/README.md). The script
# stops and changes nothing when a value does not match. An existing folder is
# moved to the Trash, not deleted.
set -euo pipefail

DEST="${1:-$HOME/Applications/Wine athei}"
WINE_TAG="cx-26.3.0-7"
WINE_URL="https://github.com/athei/wine-build/releases/download/$WINE_TAG/wine-$WINE_TAG-macos-x86_64.tar.xz"
WINE_SHA="4009323ede6aa430563d5451c13a735221df0f91c394a7d99c3c3426a0b3454e"
X87_TAG="v1.8.0"
X87_URL="https://github.com/athei/x87sidecar/releases/download/$X87_TAG/x87sidecar.tar.xz"
X87_SHA="f2548854f24818e3755e327097f31479e1ffd71c39ee432003d3ea4053528173"
HERE="$(cd "$(dirname "$0")" && pwd)"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fetch() {
    local url="$1" sha="$2" out="$3" got
    echo "download $url"
    curl -fL --retry 3 -o "$out" "$url"
    got="$(shasum -a 256 "$out" | cut -d' ' -f1)"
    if [ "$got" != "$sha" ]; then
        echo "sha256 mismatch for $url: $got (expected $sha)" >&2
        exit 1
    fi
}

fetch "$WINE_URL" "$WINE_SHA" "$TMP/wine.tar.xz"
fetch "$X87_URL" "$X87_SHA" "$TMP/x87.tar.xz"

mkdir -p "$TMP/new"
tar -xJf "$TMP/wine.tar.xz" -C "$TMP/new"
tar -xJf "$TMP/x87.tar.xz" -C "$TMP/new"
[ -x "$TMP/new/wine/bin/wine" ] && [ -x "$TMP/new/x87sidecar" ] || { echo "unexpected archive layout" >&2; exit 1; }
clang -arch x86_64 -arch arm64 -dynamiclib -O2 -o "$TMP/new/qos.dylib" "$HERE/qos.c"
xattr -cr "$TMP/new"
cat > "$TMP/new/VERSION" <<EOF
wine $WINE_TAG sha256 $WINE_SHA
x87sidecar $X87_TAG sha256 $X87_SHA
qos.dylib from common/wine/qos.c
installed $(date +%Y-%m-%d)
EOF

if [ -e "$DEST" ]; then
    mv "$DEST" "$HOME/.Trash/$(basename "$DEST") (replaced $(date +%Y-%m-%d %H%M%S))"
fi
mkdir -p "$(dirname "$DEST")"
mv "$TMP/new" "$DEST"
echo "installed in $DEST"
"$DEST/x87sidecar" --probe 2>/dev/null || true

#!/bin/sh
# Get the M-HT/SR static recompiler (MIT license) at a known commit.
# It goes into build/vendor/SR in the repository root, which is not tracked by git.
set -e
SR_COMMIT=ac690ddf3010bc3d2c875cb1b5e8da4f53a8d5b3
cd "$(dirname "$0")/../../.."
mkdir -p build/vendor
if [ ! -d build/vendor/SR/.git ]; then
    git clone https://github.com/M-HT/SR.git build/vendor/SR
fi
git -C build/vendor/SR fetch -q origin
git -C build/vendor/SR checkout -q "$SR_COMMIT"
echo "M-HT/SR at $SR_COMMIT"

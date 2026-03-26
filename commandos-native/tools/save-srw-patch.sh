#!/bin/sh
# Save the local changes in build/vendor/SR/SRW to tools/patches/srw.patch.
# SConstruct and SR_defs.h are changed by build-tools.sh with sed, so they
# are not part of the patch.
set -e
cd "$(dirname "$0")/.."
git -C build/vendor/SR diff -- SRW ':!SRW/SConstruct' ':!SRW/SR_defs.h' > tools/patches/srw.patch
grep -c '^@@' tools/patches/srw.patch | sed 's/^/hunks: /'

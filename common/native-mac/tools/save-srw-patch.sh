#!/bin/sh
# Save the local changes in build/vendor/SR/SRW to common/native-mac/tools/patches/srw.patch,
# and those in build/vendor/SR/llasm to patches/llasm.patch.
# SConstruct and SR_defs.h are changed by build-tools.sh with sed, so they
# are not part of the patch.
set -e
cd "$(dirname "$0")/../../.."
git -C build/vendor/SR diff -- SRW ':!SRW/SConstruct' ':!SRW/SR_defs.h' > common/native-mac/tools/patches/srw.patch
grep -c '^@@' common/native-mac/tools/patches/srw.patch | sed 's/^/hunks: /'
git -C build/vendor/SR diff -- llasm > common/native-mac/tools/patches/llasm.patch
grep -c '^@@' common/native-mac/tools/patches/llasm.patch | sed 's/^/llasm hunks: /'

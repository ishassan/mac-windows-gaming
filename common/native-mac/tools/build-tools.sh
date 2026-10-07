#!/bin/sh
# Build the host tools: SRW (llasm output) and llasm.
# Output: build/bin in the repository root (shared by all games).
set -e
: "${CONDA_PREFIX:?run ". common/native-mac/tools/env.sh" first}"
LDC2="$CONDA_PREFIX/opt/ldc2-1.43.0-osx-arm64/bin/ldc2"
[ -x "$LDC2" ] || { echo "run common/native-mac/tools/install-ldc.sh first"; exit 1; }
COMMON="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$COMMON/../.." && pwd)"
"$COMMON/tools/fetch-sr.sh"
mkdir -p "$ROOT/build/bin"
SR="$ROOT/build/vendor/SR"

# llasm (D language), with the local changes in patches/llasm.patch
( cd "$SR/llasm"
  git checkout -q -- .
  if [ -s "$COMMON/tools/patches/llasm.patch" ]; then git -C .. apply "$COMMON/tools/patches/llasm.patch"; fi
  "$LDC2" --O2 llasm.d --of="$ROOT/build/bin/llasm" )

# SRW with llasm output
( cd "$SR/SRW"
  git checkout -q -- .
  # Local SRW changes: new instructions, ordinal imports,
  # and the SRW_KEEP_GOING diagnostic mode.
  git -C .. apply "$COMMON/tools/patches/srw.patch"
  # Apple clang (called as g++) does not accept -static-libgcc.
  sed -i '' "s/if env\['CXX'\] == 'g++':/if False:/" SConstruct
  sed -i '' 's/#define OUTPUT_TYPE  OUT_X86/#define OUTPUT_TYPE  OUT_LLASM/' SR_defs.h
  cp "$COMMON/tools/host-include/malloc.h" udis86-1.7.2/malloc.h
  scons -Q -c >/dev/null 2>&1 || true
  scons -Q -j8
  cp SRW.exe "$ROOT/build/bin/SRW" 2>/dev/null || cp SRW "$ROOT/build/bin/SRW" )
ls -la "$ROOT/build/bin"

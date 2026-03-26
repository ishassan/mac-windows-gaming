#!/bin/sh
# Build the host tools: SRW (llasm output) and llasm. Output: build/bin.
set -e
: "${CONDA_PREFIX:?activate the commandos-native conda env first}"
LDC2="$CONDA_PREFIX/opt/ldc2-1.43.0-osx-arm64/bin/ldc2"
[ -x "$LDC2" ] || { echo "run tools/install-ldc.sh first"; exit 1; }
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
./tools/fetch-sr.sh
mkdir -p build/bin
SR=build/vendor/SR

# llasm (D language)
( cd $SR/llasm && "$LDC2" --O2 llasm.d --of="$ROOT/build/bin/llasm" )

# SRW with llasm output
( cd $SR/SRW
  git checkout -q -- .
  # Local SRW changes for this game: new instructions, ordinal imports,
  # and the SRW_KEEP_GOING diagnostic mode.
  git -C .. apply "$ROOT/tools/patches/srw.patch"
  # Apple clang (called as g++) does not accept -static-libgcc.
  sed -i '' "s/if env\['CXX'\] == 'g++':/if False:/" SConstruct
  sed -i '' 's/#define OUTPUT_TYPE  OUT_X86/#define OUTPUT_TYPE  OUT_LLASM/' SR_defs.h
  cp "$ROOT/tools/host-include/malloc.h" udis86-1.7.2/malloc.h
  scons -Q -c >/dev/null 2>&1 || true
  scons -Q -j8
  cp SRW.exe "$ROOT/build/bin/SRW" 2>/dev/null || cp SRW "$ROOT/build/bin/SRW" )
ls -la build/bin

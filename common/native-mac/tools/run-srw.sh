#!/bin/bash
# Run SRW on the game exe and stage its output for the llasm build.
# Run in a game folder (the Makefile does this).
# Input: build/srw/$GAME_EXE. Output: build/gen/*.llasm, *.llinc.
set -e
. "$(dirname "$0")/game-conf.sh"
. "$COMMON/tools/env.sh"
cd build/srw
rm -f ./*.sci jump_tables.txt data_in_text.txt not_relocations.txt     # no stale files from an earlier run
cp "$GAME_ROOT"/srw/SR.cfg "$GAME_ROOT"/srw/llasm/*.sci .
for f in jump_tables.txt data_in_text.txt not_relocations.txt; do
    if [ -f "$GAME_ROOT/srw/$f" ]; then cp "$GAME_ROOT/srw/$f" .; fi
done
# GEN_RELOCS_OPTIONS (game.conf): extra options, for example --strict-data-code
python "$COMMON/tools/gen_relocs.py" ${GEN_RELOCS_OPTIONS:-} "$GAME_EXE" > /dev/null
# SRW_KEEP_GOING: untranslatable instructions become traps (see common/native-mac/README.md)
SRW_KEEP_GOING=1 "$HOSTBIN/SRW" "$GAME_EXE" "$GAME_LLASM.llasm" > srw.out 2> srw.err
grep "^Error" srw.err > srw-traps.txt || true
echo "SRW traps: $(wc -l < srw-traps.txt | tr -d ' ')"
python "$COMMON/tools/compact_source_llasm.py"
# Exported data names of the exe (C++ names such as "?X@@3U...@@A"): the game
# does not need its exports, and LLVM does not accept these names.
sed -i '' -E '/^dlabel _?\?[^ ]*@[^ ]* global$/d' seg0*_data.llinc
# SRW can leave flags uncomputed that a jump target reads (see check_flags.py)
python "$COMMON/tools/check_flags.py" seg01_code.llinc
mkdir -p ../gen
cp "$GAME_LLASM.llasm" seg0*.llinc ../gen/
cp ../gen/rt-llasm/*.llinc ../gen/
python "$COMMON/tools/gen_extern.py" "$GAME_EXE" "$GAME_ROOT/srw/llasm/external_procedures.sci" "$GAME_LLASM.llasm" > ../gen/extern.llinc

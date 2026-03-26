#!/bin/sh
# Run SRW on the game exe and stage its output for the llasm build.
# Input: build/srw/comandos.exe. Output: build/gen/*.llasm, *.llinc.
set -e
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
. tools/env.sh
cd build/srw
python "$ROOT/tools/gen_relocs.py" comandos.exe > /dev/null
cp "$ROOT"/srw/SR.cfg "$ROOT"/srw/llasm/*.sci .
# SRW_KEEP_GOING: untranslatable instructions become traps (see README)
SRW_KEEP_GOING=1 "$ROOT/build/bin/SRW" comandos.exe Comandos.llasm > srw.out 2> srw.err
grep "^Error" srw.err > srw-traps.txt || true
echo "SRW traps: $(wc -l < srw-traps.txt | tr -d ' ')"
python "$ROOT/srw/compact_source_llasm.py"
mkdir -p ../gen
cp Comandos.llasm seg0*.llinc ../gen/
cp "$ROOT"/runtime/llasm/*.llinc ../gen/
python "$ROOT/tools/gen_extern.py" comandos.exe "$ROOT/srw/llasm/external_procedures.sci" Comandos.llasm > ../gen/extern.llinc

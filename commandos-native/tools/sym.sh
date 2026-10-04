#!/bin/bash
# Print the recompiled-code symbol at a host address from a "host 0x..." log line.
# Usage: tools/sym.sh 0x100483f48
cd "$(dirname "$0")/.." || exit 1
nm -n build/Commandos | python3 -c '
import sys
t = int(sys.argv[1], 16); best = None
for line in sys.stdin:
    p = line.split()
    if len(p) == 3 and int(p[0], 16) <= t: best = (int(p[0], 16), p[2])
print("%s +0x%x" % (best[1], t - best[0]))' "$1"

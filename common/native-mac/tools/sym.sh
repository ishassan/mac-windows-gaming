#!/bin/bash
# Print the recompiled-code symbol at a host address from a "host 0x..." log line.
# Run in a game folder. Usage: sym.sh 0x100483f48
. "$(dirname "$0")/game-conf.sh"
nm -n "build/$GAME_NAME" | python3 -c '
import sys
t = int(sys.argv[1], 16); best = None
for line in sys.stdin:
    p = line.split()
    if len(p) == 3 and int(p[0], 16) <= t: best = (int(p[0], 16), p[2])
print("%s +0x%x" % (best[1], t - best[0]))' "$1"

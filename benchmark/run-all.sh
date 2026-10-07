#!/bin/bash
# Run a set of benchmark runs in rounds. In each round every chosen game runs
# once on every chosen version, one after the other, so a slow period of the
# Mac hits all versions. At the end, the real saves and settings are compared
# with the state that setup.sh recorded.
#
# Usage: benchmark/run-all.sh [rounds] [versions] [games] [modes]
#   rounds    default 2 (two rounds were enough on 2026-10-07: values within 0.03 cores)
#   versions  default "w8 athei"; also "native"
#   games     default "cmd rev gen"
#   modes     default "fs"; "fs win" adds Generals in a window (other games: fs only)
# Example: benchmark/run-all.sh 2 "athei native" "cmd rev gen"
#
# The games take the screen (full screen) while it runs: do not use the Mac.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
W="${BENCH_WORK:-$HOME/Library/Caches/mac-windows-gaming-bench}"
ROUNDS="${1:-2}"; VERSIONS="${2:-w8 athei}"; GAMES="${3:-cmd rev gen}"; MODES="${4:-fs}"
[ -f "$W/saves-before.txt" ] || { echo "run benchmark/setup.sh first" >&2; exit 1; }

for r in $(seq 1 "$ROUNDS"); do
    for g in $GAMES; do
        for m in $MODES; do
            [ "$m" = win ] && [ "$g" != gen ] && continue
            for v in $VERSIONS; do
                [ "$v" = native ] && [ "$m" = win ] && continue
                python3 "$HERE/bench.py" "$g" "$v" "$m" "$r"
            done
        done
    done
done

"$HERE/saves-state.sh" > "$W/saves-after.txt"
if diff -q "$W/saves-before.txt" "$W/saves-after.txt" >/dev/null; then
    echo "saves and settings: unchanged"
else
    echo "saves and settings: CHANGED (see diff $W/saves-before.txt $W/saves-after.txt)"
fi
echo "ALL DONE"

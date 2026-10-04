#!/bin/bash
# Run build/$GAME_NAME for at most N seconds (default 20). Output goes to build/run.log.
# Run in a game folder. Usage: run-game.sh [seconds] [log lines]
. "$(dirname "$0")/game-conf.sh"
cd build || exit 1
secs=${1:-20}
"./$GAME_NAME" > run.log 2>&1 &
pid=$!
for _ in $(seq 1 "$secs"); do
    sleep 1
    kill -0 $pid 2>/dev/null || break
done
kill $pid 2>/dev/null && echo "still running after ${secs}s, stopped"
wait $pid
echo "exit=$?"
tail -${2:-30} run.log

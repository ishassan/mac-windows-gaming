#!/bin/bash
# Run build/Commandos under lldb for at most N seconds (default 30).
# On a crash, print the native backtrace and registers.
# Output goes to build/lldb.log. Environment variables pass through.
cd "$(dirname "$0")/../build" || exit 1
secs=${1:-30}
cmd=$(mktemp)
cat > "$cmd" <<'LLDB'
process handle SIGTERM -s false -p true
run
bt 25
register read
quit
LLDB
lldb -b -s "$cmd" ./Commandos > lldb.log 2>&1 &
pid=$!
for _ in $(seq 1 "$secs"); do
    sleep 1
    kill -0 $pid 2>/dev/null || break
done
pkill -f "$(pwd)/Commandos" 2>/dev/null
sleep 1
kill $pid 2>/dev/null
rm -f "$cmd"
grep -v "Connection\]\|linkd\|WindowTab\|^unimplemented\|Application\]" lldb.log | tail -${2:-60}

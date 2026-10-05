#!/bin/bash
# gtest.sh <name> <seconds> <command...>
# Run a game on the in-memory screen :99 and save a screenshot of the whole
# screen every 5 seconds to /tmp/shots/<name>/NNN.png (NNN = second).
# Input: KEYS=<file> with lines "<second> <xdotool command>", for example
# "20 key Escape" or "50 mousemove 432 260" and "52 click 1".
# At the end: the window list (windows.txt), then all Wine programs stop.
name=$1; secs=$2; shift 2
out=/tmp/shots/$name; rm -rf "$out"; mkdir -p "$out"
export DISPLAY=:99 HODLL=libwow64fex.dll WINEDLLOVERRIDES="mscoree=;mshtml="
"$@" >"$out/run.log" 2>&1 &
for ((t=0; t<=secs; t++)); do
  if [ -n "$KEYS" ] && [ -f "$KEYS" ]; then
    awk -v t=$t '$1==t {$1=""; print}' "$KEYS" | while read -r k; do xdotool $k; echo "$t $k" >>"$out/keys.log"; done
  fi
  if (( t % 5 == 0 )); then import -window root "$out/$(printf %03d $t).png" 2>/dev/null; fi
  sleep 1
done
xwininfo -root -tree | grep '"' | grep -vE 'Default IME|has no name' >"$out/windows.txt"
pkill -f 'wineserver' 2>/dev/null; sleep 2
pkill -f 'wine' 2>/dev/null
echo "done $name: $out"

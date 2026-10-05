#!/bin/bash
# start-x.sh: start the in-memory screen :99 (Xorg with the dummy driver).
# Xvfb has only one size; the games change the display mode (Revenant
# 640x480, Generals 1280x800), so the dummy driver with a mode list is used.
if ! pgrep -x Xorg >/dev/null; then
  sudo sh -c 'Xorg :99 -noreset -nolisten tcp -config /opt/games-vm/guest/dummy.conf -logfile /tmp/Xorg.99.log >/dev/null 2>&1 &'
  for i in 1 2 3 4 5 6 7 8 9 10; do DISPLAY=:99 xdpyinfo >/dev/null 2>&1 && break; sleep 1; done
fi
DISPLAY=:99 xrandr -s 1024x768 2>/dev/null
DISPLAY=:99 xdpyinfo >/dev/null 2>&1 && echo "screen :99 ready"

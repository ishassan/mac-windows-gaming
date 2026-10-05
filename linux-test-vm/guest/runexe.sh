#!/bin/bash
# runexe.sh <prefix> <folder under drive_c> <exe> [args]: start a Windows
# program from its own folder (as the Wine apps do).
export WINEPREFIX="$1"
cd "$1/drive_c/$2" || exit 1
shift 2
exec /opt/games-vm/guest/hw.sh wine "$@"

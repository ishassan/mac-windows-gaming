#!/bin/bash
# Make the work folder for the benchmark: an APFS copy (fast, small) of each
# game folder of ~/Games, with every link pointed at the copy, so a test run
# never writes into ~/Games. Run it again before each new benchmark: it moves
# an old work folder to the Trash first.
#
# Usage: benchmark/setup.sh [work folder]   (default: see BENCH_WORK below)
#
# Needs: the layout of ~/Games/README.md (Original Game Files, Saves, and the
# version folders native-mac, wine-11-athei, wine-8; the main version at the
# top, the others in other-versions/), and i686-w64-mingw32-gcc
# (Homebrew mingw-w64) for the in-Wine input helpers.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${1:-${BENCH_WORK:-$HOME/Library/Caches/mac-windows-gaming-bench}}"
G="$HOME/Games"
CMD="Commandos Behind Enemy Lines"
REV="Revenant"
GEN="Command and Conquer Generals Zero Hour"
# vd <game> <version>: the folder of a version, relative to ~/Games. The main
# version (native-mac; Generals: open-source-port) is at the top of the game
# folder, the others are in other-versions/.
vd() {
    if [ "$2" = native-mac ] && [ "$1" != "$GEN" ]; then echo "$1/$2"; else echo "$1/other-versions/$2"; fi
}

for g in "$CMD" "$REV" "$GEN"; do
    for v in wine-11-athei wine-8; do
        [ -d "$G/$(vd "$g" $v)/wineprefix" ] || { echo "not found: $G/$(vd "$g" $v)/wineprefix" >&2; exit 1; }
    done
done
command -v i686-w64-mingw32-gcc >/dev/null || { echo "i686-w64-mingw32-gcc not found (brew install mingw-w64)" >&2; exit 1; }

if [ -e "$WORK" ]; then
    mv "$WORK" "$HOME/.Trash/$(basename "$WORK") $(date +%Y-%m-%d-%H%M%S)"
fi
mkdir -p "$WORK/runs"
for g in "$CMD" "$REV" "$GEN"; do
    cp -cRp "$G/$g" "$WORK/$g"
done
python3 "$HERE/relink.py" "$WORK"

# The same game settings for every version: the settings of wine-11-athei
# (Generals: High detail, 1280x800; Commandos: the cnc-ddraw settings).
# Revenant: full screen (the installed value is a window).
ZH="Command and Conquer Generals Zero Hour Data"
for v in wine-8 native-mac; do
    mkdir -p "$WORK/$(vd "$GEN" $v)/Settings/$ZH"
    cp "$WORK/$(vd "$GEN" wine-11-athei)/Settings/$ZH/Options.ini" "$WORK/$(vd "$GEN" $v)/Settings/$ZH/Options.ini"
    mkdir -p "$WORK/$(vd "$REV" $v)/Settings"
    cp "$WORK/$(vd "$REV" wine-11-athei)/Settings/revenant.ini" "$WORK/$(vd "$REV" $v)/Settings/revenant.ini"
done
cp "$WORK/$(vd "$CMD" wine-11-athei)/Settings/ddraw.ini" "$WORK/$(vd "$CMD" wine-8)/Settings/ddraw.ini"
for v in wine-11-athei wine-8 native-mac; do
    perl -pi -e 's/^Windowed=Yes(\r?)$/Windowed=No$1/' "$WORK/$(vd "$REV" $v)/Settings/revenant.ini"
done

# In-Wine input helpers and the scripts, in the Wine game folders.
i686-w64-mingw32-gcc -O2 -o "$WORK/cinput.exe" "$HERE/helpers/cinput.c"
i686-w64-mingw32-gcc -O2 -o "$WORK/input.exe" "$HERE/helpers/input.c"
for v in wine-11-athei wine-8; do
    C="$WORK/$(vd "$CMD" $v)/wineprefix/drive_c/GOG Games/Commandos"
    cp "$WORK/cinput.exe" "$C/cinput.exe"
    cp "$HERE/scripts/commandos.txt" "$C/bench-script.txt"
    R="$WORK/$(vd "$REV" $v)/wineprefix/drive_c/Revenant"
    cp "$WORK/input.exe" "$R/input.exe"
    cp "$HERE/scripts/revenant.txt" "$R/bench-script.txt"
done

# Before-state of the real saves and settings (run-all.sh compares it at the end).
"$HERE/saves-state.sh" > "$WORK/saves-before.txt"
echo "work folder ready: $WORK ($(wc -l < "$WORK/saves-before.txt" | tr -d ' ') save and settings files recorded)"

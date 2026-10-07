#!/bin/bash
# Make the work folder for the benchmark: APFS copies (fast, small) of the
# game data and of the Wine prefixes, with every link pointed at the copies,
# so a test run never writes into ~/Games. Run it again before each new
# benchmark: it moves an old work folder to the Trash first.
#
# Usage: benchmark/setup.sh [work folder]   (default: see BENCH_WORK below)
#
# Needs: the layout of ~/Games/README.md (Game Data/, Wine/<prefix>, the
# Wine 8 prefix copy "<prefix> (Wine 8)"), and i686-w64-mingw32-gcc
# (Homebrew mingw-w64) for the in-Wine input helpers.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${1:-${BENCH_WORK:-$HOME/Library/Caches/mac-windows-gaming-bench}}"
G="$HOME/Games"
CMD="$G/Commandos Behind Enemy Lines"
REV="$G/Revenant"
GEN="$G/Command and Conquer Generals Zero Hour"

for d in "$CMD/Wine/wineprefix" "$CMD/Wine/wineprefix (Wine 8)" "$REV/Wine/wineprefix_cx" \
         "$REV/Wine/wineprefix_cx (Wine 8)" "$GEN/Wine/wineprefix" "$GEN/Wine/wineprefix (Wine 8)"; do
    [ -d "$d" ] || { echo "not found: $d" >&2; exit 1; }
done
command -v i686-w64-mingw32-gcc >/dev/null || { echo "i686-w64-mingw32-gcc not found (brew install mingw-w64)" >&2; exit 1; }

if [ -e "$WORK" ]; then
    mv "$WORK" "$HOME/.Trash/$(basename "$WORK") $(date +%Y-%m-%d-%H%M%S)"
fi
mkdir -p "$WORK/data/gen" "$WORK/runs"

# Game data copies. The games write settings, maps and logs here.
cp -cRp "$CMD/Game Data" "$WORK/data/cmd-Game Data"
cp -cRp "$REV/Game Data" "$WORK/data/rev-Game Data"
cp -cRp "$GEN/Game Data" "$WORK/data/gen/Game Data"
cp -cRp "$GEN/Old Windows Saves" "$WORK/data/gen/Old Windows Saves"
# The native Generals port gets the same settings (High, 1280x800) in its own copy.
cp -cRp "$GEN/Old Windows Saves" "$WORK/data/gen/native-docs"
# Revenant: full screen for the Wine version (the installed value is a window).
perl -pi -e 's/^Windowed=Yes(\r?)$/Windowed=No$1/' "$WORK/data/rev-Game Data/revenant.ini"

# Prefix copies: athei uses the live prefix, Wine 8 its own copy.
cp -cRp "$CMD/Wine/wineprefix" "$WORK/pfx-cmd-athei"
cp -cRp "$CMD/Wine/wineprefix (Wine 8)" "$WORK/pfx-cmd-w8"
cp -cRp "$REV/Wine/wineprefix_cx" "$WORK/pfx-rev-athei"
cp -cRp "$REV/Wine/wineprefix_cx (Wine 8)" "$WORK/pfx-rev-w8"
cp -cRp "$GEN/Wine/wineprefix" "$WORK/pfx-gen-athei"
cp -cRp "$GEN/Wine/wineprefix (Wine 8)" "$WORK/pfx-gen-w8"

python3 "$HERE/relink.py" "$WORK"

# In-Wine input helpers and the scripts, in the Wine game folders.
i686-w64-mingw32-gcc -O2 -o "$WORK/cinput.exe" "$HERE/helpers/cinput.c"
i686-w64-mingw32-gcc -O2 -o "$WORK/input.exe" "$HERE/helpers/input.c"
for w in athei w8; do
    cp "$WORK/cinput.exe" "$WORK/pfx-cmd-$w/drive_c/GOG Games/Commandos/cinput.exe"
    cp "$HERE/scripts/commandos.txt" "$WORK/pfx-cmd-$w/drive_c/GOG Games/Commandos/bench-script.txt"
    cp "$WORK/input.exe" "$WORK/pfx-rev-$w/drive_c/Revenant/input.exe"
    cp "$HERE/scripts/revenant.txt" "$WORK/pfx-rev-$w/drive_c/Revenant/bench-script.txt"
done

# Before-state of the real saves and settings (bench.py compares it after each run).
"$HERE/saves-state.sh" > "$WORK/saves-before.txt"
echo "work folder ready: $WORK ($(wc -l < "$WORK/saves-before.txt" | tr -d ' ') save and settings files recorded)"

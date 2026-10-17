#!/bin/bash
# Take the game data out of the GOG Windows installer, with no Windows program,
# and sort it into a game folder (see README.md, "Game files"):
#   Original Game Files/   main, mainta, maintt, the readme files, two bitmaps
#   Saves/<game>/          one folder for each game; the save that comes with
#                          the GOG game goes to Saves/Allied Assault
#   the Trash              the Windows-only files, as
#                          "Medal of Honor Allied Assault Windows files (<date>)"
# All files are checked by SHA-1 after the moves.
#
# Usage: extract-game-files.sh <setup_medal_of_honor_*.exe> <game folder>
#   The .bin parts of the installer must be in the same folder as the .exe.
#   The game folder must not have "Original Game Files" yet.
#   MOH_TRASH=<folder> puts the Windows-only files there instead (for tests).
# Needs: innoextract (Homebrew).
set -euo pipefail

INSTALLER="$1"
GAME="$2"
A="$GAME/.extract/app"
TRASH="${MOH_TRASH:-$HOME/.Trash/Medal of Honor Allied Assault Windows files ($(date +%F))}"
SUMS="$(mktemp)"

[[ -e "$GAME/Original Game Files" ]] && { echo "ERROR: $GAME/Original Game Files exists" >&2; exit 1; }
[[ -e "$TRASH" ]] && { echo "ERROR: $TRASH exists" >&2; exit 1; }
mkdir -p "$GAME/.extract"
innoextract -s -d "$GAME/.extract" -I app "$INSTALLER"
(cd "$A" && find . -type f -print0 | xargs -0 shasum -a 1) | awk '{print $1}' | sort > "$SUMS"

mkdir -p "$GAME/Original Game Files" "$GAME/Saves/Allied Assault" "$GAME/Saves/Spearhead" \
    "$GAME/Saves/Breakthrough" "$TRASH"
if [[ -d "$A/main/save" ]]; then
    for f in "$A/main/save"/*; do mv "$f" "$GAME/Saves/Allied Assault/"; done
fi
for d in main mainta maintt; do
    rmdir "$A/$d/save" 2>/dev/null || true
    # The Windows game code; OpenMoHAA has its own (game.dylib, cgame.dylib).
    mkdir -p "$TRASH/$d"
    for f in "$A/$d"/*.dll; do mv "$f" "$TRASH/$d/"; done
done
# The Windows programs, DLLs, Miles sound drivers, server .bat files, icons,
# and empty files of the installer.
for f in "$A"/*.exe "$A"/*.dll "$A"/*.bat "$A"/*.ico "$A"/sm.0*; do mv "$f" "$TRASH/"; done
mv "$A/snddrivers" "$TRASH/"
for f in "$A"/*; do mv "$f" "$GAME/Original Game Files/"; done
find "$GAME/.extract" -type d -empty -delete

{ (cd "$GAME/Original Game Files" && find . -type f -print0 | xargs -0 shasum -a 1)
  (cd "$GAME/Saves/Allied Assault" && find . -type f -print0 | xargs -0 shasum -a 1)
  (cd "$TRASH" && find . -type f -print0 | xargs -0 shasum -a 1); } | awk '{print $1}' | sort \
  | cmp -s - "$SUMS" || { echo "ERROR: the files after the moves are not the extracted files" >&2; exit 1; }
echo "$(wc -l < "$SUMS") files: Original Game Files, Saves, and the Trash: $TRASH"
rm -f "$SUMS"

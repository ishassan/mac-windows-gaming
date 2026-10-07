#!/bin/bash
# Print the state of the real saves and settings: the list of ~/Documents and the
# sha256 of every save and settings file in ~/Games (the prefixes excluded).
# setup.sh keeps it as saves-before.txt; run-all.sh compares it at the end.
set -euo pipefail
cd "$HOME"
find Documents -maxdepth 1 | sort
find Games -path "*/wineprefix*" -prune -o -type f \( -path "*/User/*" -o -path "*/Save/*" -o -path "*Saves/*" \
    -o -name "*.ini" -o -name "*.cfg" -o -name "*.CFG" \) -print0 | xargs -0 shasum -a 256 | sort -k2

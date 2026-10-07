#!/bin/bash
# Print the state of the real saves and settings: the list of ~/Documents and the
# sha256 of every file in the Saves and Settings folders of ~/Games (the prefixes
# and the app bundles excluded). setup.sh keeps it as saves-before.txt; run-all.sh
# compares it at the end.
set -euo pipefail
cd "$HOME"
find Documents -maxdepth 1 | sort
find Games \( -name wineprefix -o -name "*.app" \) -prune -o -type f \( -path "*/Saves/*" -o -path "*/Settings/*" \) -print0 \
    | xargs -0 shasum -a 256 | sort -k2

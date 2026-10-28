#!/bin/bash
# Install the apps from build/ (make-app.sh) into a game folder that has
# "Original Game Files" (extract-game-files.sh), and set up its Settings
# folder and shortcuts:
#   open-source-port/<app>.app            the three apps; an app that is there
#                                         already goes to the Trash first
#   open-source-port/Settings             fs_homepath of the apps
#   Settings/main/controller.cfg, mac-keys.cfg   copied only when missing, so
#                                         changes made in the game folder stay
#   Settings/<main|mainta|maintt>/save    links to Saves/<game>
#   <game folder>/<app name>              Finder aliases to the apps
#   ~/Games/game-shortcuts/Medal of Honor Allied Assault   alias to the main app
#
# Usage: install.sh <game folder>
# Needs: swiftc (Xcode command line tools), for the aliases (no Finder).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
GAME="$(cd "$1" && pwd)"
V="$GAME/open-source-port"
APPS=("Medal of Honor Allied Assault" "Medal of Honor Allied Assault Spearhead" "Medal of Honor Allied Assault Breakthrough")
[[ -d "$GAME/Original Game Files/main" ]] || { echo "ERROR: no Original Game Files/main in $GAME" >&2; exit 1; }

mkdir -p "$V/Settings"
for a in "${APPS[@]}"; do
    [[ -d "$HERE/build/$a.app" ]] || { echo "ERROR: run make-app.sh first" >&2; exit 1; }
    if [[ -e "$V/$a.app" ]]; then
        mv "$V/$a.app" "$HOME/.Trash/$a app before update ($(date +%F) $(date +%H%M%S))"
    fi
    ditto "$HERE/build/$a.app" "$V/$a.app"
done

link_save() { # $1 game folder name (main, mainta, maintt), $2 Saves folder
    mkdir -p "$V/Settings/$1" "$GAME/Saves/$2"
    [[ -e "$V/Settings/$1/save" || -L "$V/Settings/$1/save" ]] || ln -s "$GAME/Saves/$2" "$V/Settings/$1/save"
}
link_save main "Allied Assault"
link_save mainta Spearhead
link_save maintt Breakthrough
for f in controller.cfg mac-keys.cfg; do
    if [[ ! -e "$V/Settings/main/$f" ]]; then
        cp "$HERE/$f" "$V/Settings/main/$f"
    elif ! cmp -s "$HERE/$f" "$V/Settings/main/$f"; then
        echo "kept: $V/Settings/main/$f (it differs from the repository copy)"
    fi
done

MKALIAS="$HERE/build/mkalias"
[[ -x "$MKALIAS" ]] || swiftc -O -o "$MKALIAS" "$HERE/mkalias.swift"
for a in "${APPS[@]}"; do
    [[ -e "$GAME/$a" ]] || "$MKALIAS" "$V/$a.app" "$GAME/$a"
done
if [[ -d "$HOME/Games/game-shortcuts" && ! -e "$HOME/Games/game-shortcuts/${APPS[0]}" ]]; then
    "$MKALIAS" "$V/${APPS[0]}.app" "$HOME/Games/game-shortcuts/${APPS[0]}"
fi
echo "installed: $V"

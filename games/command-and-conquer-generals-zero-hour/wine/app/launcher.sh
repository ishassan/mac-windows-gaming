#!/bin/bash
# Command & Conquer Generals Zero Hour (Wine): start the original Zero Hour
# with Homebrew wine-crossover, to compare with the native app. Layout:
#   <game folder>/Wine/<this app>
#   <game folder>/Wine/wineprefix    the Wine prefix:
#     C:\EA Games\Command and Conquer Generals and
#     C:\EA Games\Command and Conquer Generals Zero Hour are real folders
#     with the Windows files that only Wine can use (game.dat, the DLLs,
#     MSS\, the EA launcher Generals.exe with Core\, WorldBuilder.exe, the
#     online and patch files) and a link to each item in the matching
#     Game Data folder.
#     My Documents is a link to "Old Windows Saves" (the Windows saves and
#     options), the EA Games registry keys are set, Direct3D renderer is gl.
#   <game folder>/Game Data/         the game files (shared with the native app)
# It starts game.dat, the game program. Generals.exe (the EA app launcher,
# which stops with "EA app is not installed") is not kept.

APP="$(cd "$(dirname "$0")/../.." && pwd)"
WINE_DIR="$(dirname "$APP")"
GAME_ROOT="$(cd "$WINE_DIR/.." && pwd)"
BASE_DIR="$GAME_ROOT/Game Data/Command and Conquer Generals"
ZH_DIR="$GAME_ROOT/Game Data/Command and Conquer Generals Zero Hour"
WINE_BASE_DIR="$WINE_DIR/wineprefix/drive_c/EA Games/Command and Conquer Generals"
WINE_ZH_DIR="$WINE_DIR/wineprefix/drive_c/EA Games/Command and Conquer Generals Zero Hour"
SAVES_DIR="$GAME_ROOT/Old Windows Saves"
LOG_FILE="$WINE_DIR/wine-launcher.log"

export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
export WINEPREFIX="$WINE_DIR/wineprefix"
export WINEDEBUG="-all"

fail() {
  osascript -e "display alert \"Generals Zero Hour (Wine)\" message \"$1\" as critical" >/dev/null 2>&1
  exit 1
}

WINE_BIN="$(command -v wine)" || fail "Wine was not found. Install it with: brew install --cask wine-crossover"
[ -d "$WINEPREFIX" ] || fail "The Wine prefix $WINEPREFIX was not found."
[ -d "$BASE_DIR" ] || fail "The folder $BASE_DIR was not found."
[ -d "$ZH_DIR" ] || fail "The folder $ZH_DIR was not found."
[ -f "$WINE_ZH_DIR/game.dat" ] || fail "game.dat was not found in $WINE_ZH_DIR."
[ -d "$SAVES_DIR" ] || fail "The saves folder $SAVES_DIR was not found."

WINE_USER_DIR="$WINEPREFIX/drive_c/users/${USER:-$(id -un)}"
mkdir -p "$WINE_USER_DIR"
if [ "$(readlink "$WINE_USER_DIR/Documents")" != "$SAVES_DIR" ]; then
  [ -L "$WINE_USER_DIR/Documents" ] && rm -f "${WINE_USER_DIR:?}/Documents"
  [ -e "$WINE_USER_DIR/Documents" ] && fail "$WINE_USER_DIR/Documents is a folder, not a link to Old Windows Saves. Move it away first."
  ln -s "$SAVES_DIR" "$WINE_USER_DIR/Documents"
fi
for folder in Desktop Downloads Music Pictures Videos; do
  [ -L "$WINE_USER_DIR/$folder" ] && rm -f "${WINE_USER_DIR:?}/${folder:?}"
  mkdir -p "$WINE_USER_DIR/$folder"
done

# Add a link for each Game Data item that is not in the Wine game folders.
link_items() {
  local item name
  mkdir -p "$2"
  for item in "$1"/*; do
    name="$(basename "$item")"
    [ -e "$2/$name" ] || [ -L "$2/$name" ] || ln -s "$item" "$2/$name"
  done
}
link_items "$BASE_DIR" "$WINE_BASE_DIR"
link_items "$ZH_DIR" "$WINE_ZH_DIR"

cd "$WINE_ZH_DIR" || fail "Cannot open $WINE_ZH_DIR."
echo "=== $(date) Generals Zero Hour (Wine)" >>"$LOG_FILE"
exec "$WINE_BIN" 'C:\EA Games\Command and Conquer Generals Zero Hour\game.dat' >>"$LOG_FILE" 2>&1 </dev/null

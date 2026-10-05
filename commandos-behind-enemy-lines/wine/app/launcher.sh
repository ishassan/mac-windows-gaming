#!/bin/bash
# Commandos Behind Enemy Lines (Wine): start the GOG comandos.exe with
# Homebrew wine-crossover, to compare with the native app. Layout:
#   Commandos Behind Enemy Lines/Wine/<this app>
#   Commandos Behind Enemy Lines/Wine/wineprefix   the Wine prefix
#     (cnc-ddraw ddraw.dll is in syswow64)
#   Commandos Behind Enemy Lines/Game Data/        the game
# C:\GOG Games\Commandos (wineprefix/drive_c/GOG Games/Commandos) is a real
# folder with the files that only Wine needs, and a link to each item in
# Game Data:
#   MSS32.DLL       Miles, patched for this Wine (SuspendThread/ResumeThread
#                   imports renamed, else the Miles timer thread hangs). It
#                   must be next to the exe: Miles stops with an error when
#                   it is in the Windows system folder.
#   MSS32.DLL.orig  the original Miles DLL
#   ddraw.ini       cnc-ddraw settings
#   mpserver.exe, directplay.cmd   multiplayer server and DirectPlay setup
#   MSS16.DLL, mssb16.tsk          Miles parts for Windows 95/98

APP="$(cd "$(dirname "$0")/../.." && pwd)"
WINE_DIR="$(dirname "$APP")"
GAME_DIR="$(cd "$WINE_DIR/../Game Data" 2>/dev/null && pwd)"
LOG_FILE="$WINE_DIR/wine-launcher.log"

export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
export WINEPREFIX="$WINE_DIR/wineprefix"
export WINEDEBUG="-all"

fail() {
  osascript -e "display alert \"Commandos (Wine)\" message \"$1\" as critical" >/dev/null 2>&1
  exit 1
}

WINE_BIN="$(command -v wine)" || fail "Wine was not found. Install it with: brew install --cask wine-crossover"
[ -d "$WINEPREFIX" ] || fail "The Wine prefix $WINEPREFIX was not found."
[ -n "$GAME_DIR" ] && [ -f "$GAME_DIR/comandos.exe" ] || fail "comandos.exe was not found in $WINE_DIR/../Game Data."

# The game keeps its saves in My Documents\Pyro Studios. My Documents is a
# link to Game Data/User, the folder that the native app uses, so both
# versions use the same saves. The other Wine user folders are local
# folders, not links to the Mac home folders.
WINE_USER_DIR="$WINEPREFIX/drive_c/users/${USER:-$(id -un)}"
mkdir -p "$WINE_USER_DIR" "$GAME_DIR/User"
if [ "$(readlink "$WINE_USER_DIR/Documents")" != "$GAME_DIR/User" ]; then
  [ -L "$WINE_USER_DIR/Documents" ] && rm -f "$WINE_USER_DIR/Documents"
  [ -e "$WINE_USER_DIR/Documents" ] && fail "$WINE_USER_DIR/Documents is a folder, not a link to Game Data/User. Move it away first."
  ln -s "$GAME_DIR/User" "$WINE_USER_DIR/Documents"
fi
for folder in Desktop Downloads Music Pictures Videos; do
  [ -L "$WINE_USER_DIR/$folder" ] && rm -f "$WINE_USER_DIR/$folder"
  mkdir -p "$WINE_USER_DIR/$folder"
done

# Add a link for each Game Data item that is not in the Wine game folder.
WINE_GAME_DIR="$WINEPREFIX/drive_c/GOG Games/Commandos"
[ -f "$WINE_GAME_DIR/MSS32.DLL" ] || fail "MSS32.DLL was not found in $WINE_GAME_DIR."
for item in "$GAME_DIR"/*; do
  name="$(basename "$item")"
  [ -e "$WINE_GAME_DIR/$name" ] || [ -L "$WINE_GAME_DIR/$name" ] || ln -s "$item" "$WINE_GAME_DIR/$name"
done

cd "$WINE_GAME_DIR" || fail "Cannot open $WINE_GAME_DIR."
echo "=== $(date) Commandos (Wine)" >>"$LOG_FILE"
exec "$WINE_BIN" 'C:\GOG Games\Commandos\comandos.exe' >>"$LOG_FILE" 2>&1 </dev/null

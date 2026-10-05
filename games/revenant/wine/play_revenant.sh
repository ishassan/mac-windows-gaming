#!/usr/bin/env bash
# Launch Revenant on macOS from terminal, Spotlight, Raycast, or an app wrapper.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_FILE="$SCRIPT_DIR/revenant-launcher.log"
WINE_USER_DIR="$SCRIPT_DIR/wineprefix_cx/drive_c/users/${USER:-$(id -un)}"

export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:${PATH:-}"
export WINEPREFIX="$SCRIPT_DIR/wineprefix_cx"
export WINEDEBUG="-all"

WINE_BIN="${WINE_BIN:-$(command -v wine || true)}"
if [[ -z "$WINE_BIN" ]]; then
  echo "wine was not found in PATH." >&2
  exit 1
fi

ensure_local_wine_folder() {
  local folder="$1"
  local path="$WINE_USER_DIR/$folder"

  if [[ -L "$path" ]]; then
    rm -f "$path"
  fi

  mkdir -p "$path"
}

mkdir -p "$WINE_USER_DIR"
for folder in Desktop Documents Downloads Music Pictures Videos; do
  ensure_local_wine_folder "$folder"
done

# The game runs from C:\Revenant in the prefix: a folder with the files that
# only Wine needs and a link to each item in Game Data (see the comments in
# "Revenant (Wine).app/Contents/MacOS/Revenant").
cd "$SCRIPT_DIR/wineprefix_cx/drive_c/Revenant"
nohup "$WINE_BIN" "C:\\Revenant\\Revenant.exe" >>"$LOG_FILE" 2>&1 </dev/null &

echo "Revenant is starting..."
echo "Launcher log: $LOG_FILE"

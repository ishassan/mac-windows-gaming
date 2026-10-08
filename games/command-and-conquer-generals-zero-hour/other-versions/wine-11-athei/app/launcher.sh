#!/bin/bash
# Command & Conquer Generals Zero Hour (Wine): start the original Zero Hour
# on Wine, to compare with the native apps. The same script is the launcher
# of both Wine versions; the name of the version folder picks the Wine:
#   wine-11-athei   the athei CrossOver 26.3 Wine (the default)
#   wine-8          Wine 8 (Homebrew wine-crossover 23.7.1), kept to compare
# Layout (~/Games/README.md):
#   <game folder>/Original Game Files/   the game files of the base game and
#                                        Zero Hour (shared)
#   <game folder>/Saves/Zero Hour/       Zero Hour saves (shared with our native port)
#   <game folder>/Saves/Generals/        base-game saves
#   <game folder>/other-versions/<version>/<this app>
#   <game folder>/other-versions/<version>/wineprefix/  the Wine prefix:
#     C:\EA Games\Command and Conquer Generals and
#     C:\EA Games\Command and Conquer Generals Zero Hour are real folders
#     with the Windows files that only Wine can use (game.dat, the DLLs,
#     MSS\, the EA launcher Generals.exe with Core\, WorldBuilder.exe, the
#     online and patch files) and a link to each item in the matching
#     Original Game Files folder. The EA Games registry keys are set,
#     Direct3D renderer is gl.
#   <game folder>/other-versions/<version>/Settings/    My Documents: the options, maps and
#                                        replays of this version; the two
#                                        Save folders are links to Saves.
# It starts game.dat, the game program. Generals.exe (the EA app launcher,
# which stops with "EA app is not installed") is not used.

APP="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION_DIR="$(dirname "$APP")"
# The game folder is the nearest folder above with "Original Game Files": the main
# version is at the top of the game folder, the other versions are in other-versions/.
GAME_ROOT="$(dirname "$VERSION_DIR")"
while [ ! -d "$GAME_ROOT/Original Game Files" ] && [ "$GAME_ROOT" != / ]; do GAME_ROOT="$(dirname "$GAME_ROOT")"; done
BASE_DIR="$GAME_ROOT/Original Game Files/Command and Conquer Generals"
ZH_DIR="$GAME_ROOT/Original Game Files/Command and Conquer Generals Zero Hour"
SAVES="$GAME_ROOT/Saves"
SETTINGS="$VERSION_DIR/Settings"
WINE_BASE_DIR="$VERSION_DIR/wineprefix/drive_c/EA Games/Command and Conquer Generals"
WINE_ZH_DIR="$VERSION_DIR/wineprefix/drive_c/EA Games/Command and Conquer Generals Zero Hour"
LOG_FILE="$VERSION_DIR/wine-launcher.log"

export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
export WINEPREFIX="$VERSION_DIR/wineprefix"
export WINEDEBUG="-all"

fail() {
  osascript -e "display alert \"Generals Zero Hour (Wine)\" message \"$1\" as critical" >/dev/null 2>&1
  exit 1
}

case "$(basename "$VERSION_DIR")" in
  wine-8)
    WINE_BIN=/opt/homebrew/bin/wine
    [ -x "$WINE_BIN" ] || fail "Wine 8 was not found. Install it with: brew install --cask wine-crossover"
    ;;
  *)
    # The athei CrossOver 26.3 build (Wine 11) with the x87sidecar math
    # helper and the thread QoS fix. Install: common/wine/install-athei.sh.
    # The build has no Mono and no Gecko: without the override, Wine waits
    # for an install prompt that does not show.
    WINE_HOME="$HOME/Applications/Wine athei"
    WINE_BIN="$WINE_HOME/wine/bin/wine"
    [ -x "$WINE_BIN" ] || fail "Wine was not found in $WINE_HOME. Install it with common/wine/install-athei.sh from the mac-windows-gaming repository."
    export ROSETTA_X87_PATH="$WINE_HOME/x87sidecar"
    export DYLD_INSERT_LIBRARIES="$WINE_HOME/qos.dylib"
    export WINEDLLOVERRIDES="mscoree,mshtml="
    ;;
esac
[ -d "$WINEPREFIX" ] || fail "The Wine prefix $WINEPREFIX was not found."
[ -d "$BASE_DIR" ] || fail "The folder $BASE_DIR was not found."
[ -d "$ZH_DIR" ] || fail "The folder $ZH_DIR was not found."
[ -f "$WINE_ZH_DIR/game.dat" ] || fail "game.dat was not found in $WINE_ZH_DIR."
mkdir -p "$SAVES/Zero Hour" "$SAVES/Generals" \
         "$SETTINGS/Command and Conquer Generals Zero Hour Data" "$SETTINGS/Command and Conquer Generals Data"

# link_to <target> <link>: make <link> a link to <target>. A real file or
# folder at <link> stops the start, so that nothing is lost.
link_to() {
  [ "$(readlink "$2")" = "$1" ] && return
  [ -L "$2" ] && rm -f "$2"
  [ -e "$2" ] && fail "$2 is a real file or folder, not a link to $1. Move it away first."
  ln -s "$1" "$2"
}

# My Documents is the Settings folder of this version; its two Save folders
# are links to the shared saves. The other Wine user folders are local
# folders, not links to the Mac home folders. Wine 8 uses the Mac user name,
# the athei Wine uses "crossover": set up both, else My Documents of the
# athei Wine is the Mac Documents folder.
link_to "$SAVES/Zero Hour" "$SETTINGS/Command and Conquer Generals Zero Hour Data/Save"
link_to "$SAVES/Generals" "$SETTINGS/Command and Conquer Generals Data/Save"
for WINE_USER_DIR in "$WINEPREFIX/drive_c/users/${USER:-$(id -un)}" "$WINEPREFIX/drive_c/users/crossover"; do
  mkdir -p "$WINE_USER_DIR"
  link_to "$SETTINGS" "$WINE_USER_DIR/Documents"
  for folder in Desktop Downloads Music Pictures Videos; do
    [ -L "$WINE_USER_DIR/$folder" ] && rm -f "${WINE_USER_DIR:?}/${folder:?}"
    mkdir -p "$WINE_USER_DIR/$folder"
  done
  [ -L "$WINE_USER_DIR/Desktop/My Mac Desktop" ] && rm -f "$WINE_USER_DIR/Desktop/My Mac Desktop"
done

# Add a link for each item of Original Game Files that is not in the Wine game folders.
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
echo "=== $(date) Generals Zero Hour ($(basename "$VERSION_DIR"))" >>"$LOG_FILE"
exec "$WINE_BIN" 'C:\EA Games\Command and Conquer Generals Zero Hour\game.dat' >>"$LOG_FILE" 2>&1 </dev/null

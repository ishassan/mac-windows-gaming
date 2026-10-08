#!/bin/bash
# Commandos Behind Enemy Lines (Wine): start the GOG comandos.exe on Wine, to
# compare with the native app. The same script is the launcher of both Wine
# versions; the name of the version folder picks the Wine:
#   wine-11-athei   the athei CrossOver 26.3 Wine (the default)
#   wine-8          Wine 8 (Homebrew wine-crossover 23.7.1), kept to compare
# Layout (~/Games/README.md):
#   Commandos Behind Enemy Lines/Original Game Files/   the game (shared)
#   Commandos Behind Enemy Lines/Saves/                 saves and game options (shared)
#   Commandos Behind Enemy Lines/other-versions/<version>/<this app>
#   Commandos Behind Enemy Lines/other-versions/<version>/wineprefix/  the Wine prefix
#     (cnc-ddraw ddraw.dll is in syswow64)
#   Commandos Behind Enemy Lines/other-versions/<version>/Settings/ddraw.ini   cnc-ddraw settings
# C:\GOG Games\Commandos (wineprefix/drive_c/GOG Games/Commandos) is a real
# folder with the files that only Wine needs, and a link to each item in
# Original Game Files:
#   MSS32.DLL       Miles, patched for this Wine (SuspendThread/ResumeThread
#                   imports renamed, else the Miles timer thread hangs). It
#                   must be next to the exe: Miles stops with an error when
#                   it is in the Windows system folder.
#   MSS32.DLL.orig  the original Miles DLL
#   ddraw.ini       a link to Settings/ddraw.ini
#   mpserver.exe, directplay.cmd   multiplayer server and DirectPlay setup
#   MSS16.DLL, mssb16.tsk          Miles parts for Windows 95/98

APP="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION_DIR="$(dirname "$APP")"
# The game folder is the nearest folder above with "Original Game Files": the main
# version is at the top of the game folder, the other versions are in other-versions/.
GAME_ROOT="$(dirname "$VERSION_DIR")"
while [ ! -d "$GAME_ROOT/Original Game Files" ] && [ "$GAME_ROOT" != / ]; do GAME_ROOT="$(dirname "$GAME_ROOT")"; done
GAME_FILES="$GAME_ROOT/Original Game Files"
SAVES="$GAME_ROOT/Saves"
SETTINGS="$VERSION_DIR/Settings"
LOG_FILE="$VERSION_DIR/wine-launcher.log"

export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
export WINEPREFIX="$VERSION_DIR/wineprefix"
export WINEDEBUG="-all"

fail() {
  osascript -e "display alert \"Commandos (Wine)\" message \"$1\" as critical" >/dev/null 2>&1
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
[ -f "$GAME_FILES/comandos.exe" ] || fail "comandos.exe was not found in $GAME_FILES."
WINE_GAME_DIR="$WINEPREFIX/drive_c/GOG Games/Commandos"
[ -f "$WINE_GAME_DIR/MSS32.DLL" ] || fail "MSS32.DLL was not found in $WINE_GAME_DIR."
[ -f "$SETTINGS/ddraw.ini" ] || fail "ddraw.ini (the cnc-ddraw settings) was not found in $SETTINGS."
mkdir -p "$SAVES"

# link_to <target> <link>: make <link> a link to <target>. A real file or
# folder at <link> stops the start, so that nothing is lost.
link_to() {
  [ "$(readlink "$2")" = "$1" ] && return
  [ -L "$2" ] && rm -f "$2"
  [ -e "$2" ] && fail "$2 is a real file or folder, not a link to $1. Move it away first."
  ln -s "$1" "$2"
}

# The game keeps its saves and game options in
# My Documents\Pyro Studios\Commandos\OUTPUT: that folder is a link to the
# shared Saves folder, which the native app also uses. The other Wine user
# folders are local folders, not links to the Mac home folders. Wine 8 uses
# the Mac user name, the athei Wine uses "crossover": set up both, else My
# Documents of the athei Wine is the Mac Documents folder.
for WINE_USER_DIR in "$WINEPREFIX/drive_c/users/${USER:-$(id -un)}" "$WINEPREFIX/drive_c/users/crossover"; do
  for folder in Desktop Documents Downloads Music Pictures Videos; do
    [ -L "$WINE_USER_DIR/$folder" ] && rm -f "$WINE_USER_DIR/$folder"
    mkdir -p "$WINE_USER_DIR/$folder"
  done
  [ -L "$WINE_USER_DIR/Desktop/My Mac Desktop" ] && rm -f "$WINE_USER_DIR/Desktop/My Mac Desktop"
  mkdir -p "$WINE_USER_DIR/Documents/Pyro Studios/Commandos"
  link_to "$SAVES" "$WINE_USER_DIR/Documents/Pyro Studios/Commandos/OUTPUT"
done

link_to "$SETTINGS/ddraw.ini" "$WINE_GAME_DIR/ddraw.ini"
# Add a link for each item of Original Game Files that is not in the Wine game folder.
for item in "$GAME_FILES"/*; do
  name="$(basename "$item")"
  [ -e "$WINE_GAME_DIR/$name" ] || [ -L "$WINE_GAME_DIR/$name" ] || ln -s "$item" "$WINE_GAME_DIR/$name"
done

cd "$WINE_GAME_DIR" || fail "Cannot open $WINE_GAME_DIR."
echo "=== $(date) Commandos ($(basename "$VERSION_DIR"))" >>"$LOG_FILE"
exec "$WINE_BIN" 'C:\GOG Games\Commandos\comandos.exe' >>"$LOG_FILE" 2>&1 </dev/null

#!/bin/bash
# Revenant (Wine): start the GOG Revenant.exe on Wine, to compare with the
# native app. The same script is the launcher of both Wine versions; the
# name of the version folder picks the Wine:
#   wine-11-athei   the athei CrossOver 26.3 Wine (the default)
#   wine-8          Wine 8 (Homebrew wine-crossover 23.7.1), kept to compare
# Layout (~/Games/README.md):
#   Revenant/Original Game Files/      the game (shared)
#   Revenant/Saves/                    the saves (shared; the game's Save folder)
#   Revenant/other-versions/<version>/<this app>
#   Revenant/other-versions/<version>/wineprefix/     the Wine prefix
#   Revenant/other-versions/<version>/Settings/       revenant.ini and Curmap (the work files of the current map)
# C:\Revenant (wineprefix/drive_c/Revenant) is a real folder with the files
# that only Wine needs, the links Save, revenant.ini and Curmap, and a link
# to each item in Original Game Files:
#   _inmm.dll, _inmm_real.dll   GOG CD-audio and display-mode helpers
#   mss32.dll                   Miles, patched for this Wine (SuspendThread/
#                               ResumeThread imports renamed, else the Miles
#                               timer thread hangs); original: mss32.dll.orig
#   mp3dec.asi, *.m3d           Miles decoders and 3D sound providers
#   smackw32.dll                Smacker video player
#   libvorbis.dll, libvorbisfile.dll, libogg.dll   Ogg music for _inmm_real.dll
#   Launcher.exe, wh32LIB.DLL   the game's Windows launcher
#   ipxwrapper.dll, wsock32.dll, mswsock.dll, ipxconfig.exe, ipx_reg.cmd,
#   directplay.cmd              IPX network play (multiplayer)
#   __redist\                   DirectX and other Windows installers
# The DLLs must be next to the exe (Miles stops with an error when it is in
# the Windows system folder).

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
  osascript -e "display alert \"Revenant (Wine)\" message \"$1\" as critical" >/dev/null 2>&1
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
[ -f "$GAME_FILES/Revenant.exe" ] || fail "Revenant.exe was not found in $GAME_FILES."
WINE_GAME_DIR="$WINEPREFIX/drive_c/Revenant"
[ -f "$WINE_GAME_DIR/mss32.dll" ] || fail "mss32.dll was not found in $WINE_GAME_DIR."
mkdir -p "$SAVES" "$SETTINGS/Curmap"

# link_to <target> <link>: make <link> a link to <target>. A real file or
# folder at <link> stops the start, so that nothing is lost.
link_to() {
  [ "$(readlink "$2")" = "$1" ] && return
  [ -L "$2" ] && rm -f "$2"
  [ -e "$2" ] && fail "$2 is a real file or folder, not a link to $1. Move it away first."
  ln -s "$1" "$2"
}

# The Wine user folders are local folders, not links to the Mac home
# folders (the game does not use them). Wine 8 uses the Mac user name, the
# athei Wine uses "crossover": set up both.
for WINE_USER_DIR in "$WINEPREFIX/drive_c/users/${USER:-$(id -un)}" "$WINEPREFIX/drive_c/users/crossover"; do
  for folder in Desktop Documents Downloads Music Pictures Videos; do
    [ -L "$WINE_USER_DIR/$folder" ] && rm -f "$WINE_USER_DIR/$folder"
    mkdir -p "$WINE_USER_DIR/$folder"
  done
  [ -L "$WINE_USER_DIR/Desktop/My Mac Desktop" ] && rm -f "$WINE_USER_DIR/Desktop/My Mac Desktop"
done

# The game writes its saves, its settings and the current map into its own
# folder: these are links to the shared saves and to this version's settings.
link_to "$SAVES" "$WINE_GAME_DIR/Save"
link_to "$SETTINGS/revenant.ini" "$WINE_GAME_DIR/revenant.ini"
link_to "$SETTINGS/Curmap" "$WINE_GAME_DIR/Curmap"
# Add a link for each item of Original Game Files that is not in the Wine game folder.
for item in "$GAME_FILES"/*; do
  name="$(basename "$item")"
  [ -e "$WINE_GAME_DIR/$name" ] || [ -L "$WINE_GAME_DIR/$name" ] || ln -s "$item" "$WINE_GAME_DIR/$name"
done

cd "$WINE_GAME_DIR" || fail "Cannot open $WINE_GAME_DIR."
echo "=== $(date) Revenant ($(basename "$VERSION_DIR"))" >>"$LOG_FILE"
exec "$WINE_BIN" 'C:\Revenant\Revenant.exe' >>"$LOG_FILE" 2>&1 </dev/null

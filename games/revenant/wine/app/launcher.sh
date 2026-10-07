#!/bin/bash
# Revenant (Wine): start the GOG Revenant.exe with the athei CrossOver 26.3 Wine
# (the same setup as play_revenant.sh). Layout:
#   Revenant/Wine/Revenant (Wine).app   this app
#   Revenant/Wine/wineprefix_cx         the Wine prefix
#   Revenant/Game Data/                 the game
# C:\Revenant (wineprefix_cx/drive_c/Revenant) is a real folder with the
# files that only Wine needs, and a link to each item in Game Data:
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
WINE_DIR="$(dirname "$APP")"
GAME_DIR="$(cd "$WINE_DIR/../Game Data" 2>/dev/null && pwd)"
LOG_FILE="$WINE_DIR/revenant-launcher.log"

export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
export WINEPREFIX="$WINE_DIR/wineprefix_cx"
export WINEDEBUG="-all"

fail() {
  osascript -e "display alert \"Revenant (Wine)\" message \"$1\" as critical" >/dev/null 2>&1
  exit 1
}

# Wine: the athei CrossOver 26.3 build (Wine 11) with the x87sidecar math
# helper and the thread QoS fix. Install: common/wine/install-athei.sh.
# The build has no Mono and no Gecko: without the override, Wine waits for
# an install prompt that does not show.
WINE_HOME="$HOME/Applications/Wine athei"
WINE_BIN="$WINE_HOME/wine/bin/wine"
[ -x "$WINE_BIN" ] || fail "Wine was not found in $WINE_HOME. Install it with common/wine/install-athei.sh from the mac-windows-gaming repository."
export ROSETTA_X87_PATH="$WINE_HOME/x87sidecar"
export DYLD_INSERT_LIBRARIES="$WINE_HOME/qos.dylib"
export WINEDLLOVERRIDES="mscoree,mshtml="
[ -d "$WINEPREFIX" ] || fail "The Wine prefix $WINEPREFIX was not found."
[ -n "$GAME_DIR" ] && [ -f "$GAME_DIR/Revenant.exe" ] || fail "Revenant.exe was not found in $WINE_DIR/../Game Data."

# As play_revenant.sh: the Wine user folders are local folders, not links
# to the Mac home folders.
# The Wine user folder: Wine 8 uses the Mac user name, the athei CrossOver
# Wine uses "crossover". Set up both, else My Documents of the athei Wine
# is a link to the Mac Documents folder.
for WINE_USER_DIR in "$WINEPREFIX/drive_c/users/${USER:-$(id -un)}" "$WINEPREFIX/drive_c/users/crossover"; do
  mkdir -p "$WINE_USER_DIR"
  for folder in Desktop Documents Downloads Music Pictures Videos; do
    [ -L "$WINE_USER_DIR/$folder" ] && rm -f "$WINE_USER_DIR/$folder"
    mkdir -p "$WINE_USER_DIR/$folder"
  done
done

# Add a link for each Game Data item that is not in the Wine game folder.
WINE_GAME_DIR="$WINEPREFIX/drive_c/Revenant"
[ -f "$WINE_GAME_DIR/mss32.dll" ] || fail "mss32.dll was not found in $WINE_GAME_DIR."
for item in "$GAME_DIR"/*; do
  name="$(basename "$item")"
  [ -e "$WINE_GAME_DIR/$name" ] || [ -L "$WINE_GAME_DIR/$name" ] || ln -s "$item" "$WINE_GAME_DIR/$name"
done

cd "$WINE_GAME_DIR" || fail "Cannot open $WINE_GAME_DIR."
echo "=== $(date) Revenant (Wine)" >>"$LOG_FILE"
exec "$WINE_BIN" 'C:\Revenant\Revenant.exe' >>"$LOG_FILE" 2>&1 </dev/null

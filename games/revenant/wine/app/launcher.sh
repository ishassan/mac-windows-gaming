#!/bin/bash
# Revenant (Wine): start the GOG Revenant.exe with Homebrew wine-crossover
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

WINE_BIN="$(command -v wine)" || fail "Wine was not found. Install it with: brew install --cask wine-crossover"
[ -d "$WINEPREFIX" ] || fail "The Wine prefix $WINEPREFIX was not found."
[ -n "$GAME_DIR" ] && [ -f "$GAME_DIR/Revenant.exe" ] || fail "Revenant.exe was not found in $WINE_DIR/../Game Data."

# As play_revenant.sh: the Wine user folders are local folders, not links
# to the Mac home folders.
WINE_USER_DIR="$WINEPREFIX/drive_c/users/${USER:-$(id -un)}"
mkdir -p "$WINE_USER_DIR"
for folder in Desktop Documents Downloads Music Pictures Videos; do
  [ -L "$WINE_USER_DIR/$folder" ] && rm -f "$WINE_USER_DIR/$folder"
  mkdir -p "$WINE_USER_DIR/$folder"
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

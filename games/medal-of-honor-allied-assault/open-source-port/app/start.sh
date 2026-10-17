#!/bin/bash
# @APP_NAME@: start the game with OpenMoHAA (a native Mac program, in this folder)
# and the game files, settings and saves of this game folder:
#   <game folder>/Original Game Files   the GOG game data (main, mainta, maintt)
#   <game folder>/open-source-port/Settings   settings (fs_homepath);
#                                       Settings/<game>/save links to Saves/
#   <game folder>/Saves                 the saves
# com_target_game: 0 Allied Assault, 1 Spearhead, 2 Breakthrough.
DIR="$(cd "$(dirname "$0")" && pwd)"
APP="$(cd "$DIR/../.." && pwd)"
VERSION_DIR="$(dirname "$APP")"
GAME_ROOT="$(dirname "$VERSION_DIR")"
while [ ! -d "$GAME_ROOT/Original Game Files" ] && [ "$GAME_ROOT" != / ]; do GAME_ROOT="$(dirname "$GAME_ROOT")"; done
exec "$DIR/openmohaa" +set com_target_game @TARGET_GAME@ \
  +set fs_basepath "$GAME_ROOT/Original Game Files" \
  +set fs_homepath "$VERSION_DIR/Settings" \
  +set in_joystick 1 +set in_joystickUseAnalog 1 \
  +exec controller.cfg +exec mac-keys.cfg "$@"

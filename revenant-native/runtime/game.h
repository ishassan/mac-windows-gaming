/*
 *  Revenant: values for the shared runtime (see common/runtime/game-info.h).
 *  MIT license.
 */

#ifndef GAME_H
#define GAME_H

#define GAME_TITLE        "Revenant"
#define GAME_SHORT_NAME   "Revenant"
#define GAME_ENV_PREFIX   "REVENANT_"
#define GAME_MODULE_PATH  "C:\\Revenant.exe"
#define GAME_DATA_DEFAULT "Games/Revenant"
#define GAME_DATA_CHECK   "resources.rvr"
#define GAME_CONFIG_FILE  "Revenant-native.cfg"
/* CD audio tracks (AIL_redbook_*, WinApi-mss32-redbook.c): track n is this file */
#define GAME_CD_TRACK_FILE "Music\\Track%02d.ogg"
/* Start in full screen at the desktop size (Cmd+Return switches to a window) */
#define GAME_DISPLAY_MODE 1

#endif

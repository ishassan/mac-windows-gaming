/*
 *  Commandos: Behind Enemy Lines: values for the shared runtime
 *  (see common/native-mac/runtime/game-info.h). MIT license.
 */

#ifndef GAME_H
#define GAME_H

#define GAME_TITLE        "Commandos: Behind Enemy Lines"
#define GAME_SHORT_NAME   "Commandos"
#define GAME_ENV_PREFIX   "COMMANDOS_"
#define GAME_MODULE_PATH  "C:\\comandos.exe"
#define GAME_DATA_DEFAULT "Games/Commandos Behind Enemy Lines/Game Data"
#define GAME_DATA_CHECK   "WARGAME.DIR"
#define GAME_CONFIG_FILE  "Commandos.cfg"
/* Miles: the first AIL_waveOutOpen after AIL_set_preference(0, ...) fails
 * (see WinApi-mss32.c) */
#define GAME_MSS_FAIL_FIRST_WAVEOUT 1
/* Start in full screen at the desktop size (Cmd+Return switches to a window) */
#define GAME_DISPLAY_MODE 1

#endif

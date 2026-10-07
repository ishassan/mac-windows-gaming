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
#define GAME_DATA_DEFAULT "Games/Commandos Behind Enemy Lines/Original Game Files"
#define GAME_DATA_CHECK   "WARGAME.DIR"
/* Layout of the game folder (~/Games/README.md): the original game files are
 * shared by all versions, the saves are shared (Saves), and the settings of
 * this version are in native-mac/Settings. Paths are relative to the game
 * files folder. */
#define GAME_CONFIG_FILE  "../native-mac/Settings/Commandos.cfg"
/* My Documents is C:\User (the default of the runtime). The game keeps its
 * saves and its game options (COMANDO.CFG, USER.CFG) together in
 * My Documents\Pyro Studios\Commandos\OUTPUT: that folder is the shared
 * Saves folder. The rest of My Documents goes to Settings. */
#define GAME_PATH_REDIRECTS \
    "User/Pyro Studios/Commandos/OUTPUT", "../Saves", \
    "User", "../native-mac/Settings/Documents"
/* Miles: the first AIL_waveOutOpen after AIL_set_preference(0, ...) fails
 * (see WinApi-mss32.c) */
#define GAME_MSS_FAIL_FIRST_WAVEOUT 1
/* Start in full screen at the desktop size (Cmd+Return switches to a window) */
#define GAME_DISPLAY_MODE 1

#endif

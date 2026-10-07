/*
 *  Revenant: values for the shared runtime (see common/native-mac/runtime/game-info.h).
 *  MIT license.
 */

#ifndef GAME_H
#define GAME_H

#define GAME_TITLE        "Revenant"
#define GAME_SHORT_NAME   "Revenant"
#define GAME_ENV_PREFIX   "REVENANT_"
#define GAME_MODULE_PATH  "C:\\Revenant.exe"
#define GAME_DATA_DEFAULT "Games/Revenant/Original Game Files"
#define GAME_DATA_CHECK   "resources.rvr"
/* Layout of the game folder (~/Games/README.md): the original game files are
 * shared by all versions, the saves are shared (Saves), and the settings of
 * this version are in native-mac/Settings. Paths are relative to the game
 * files folder. */
#define GAME_CONFIG_FILE  "../native-mac/Settings/Revenant-native.cfg"
/* The game writes its settings (revenant.ini), the work files of the
 * current map (Curmap), a start log and a save picture into its own folder,
 * and the saves into Save. */
#define GAME_PATH_REDIRECTS \
    "Save", "../Saves", \
    "revenant.ini", "../native-mac/Settings/revenant.ini", \
    "Curmap", "../native-mac/Settings/Curmap", \
    "revboot.log", "../native-mac/Settings/revboot.log", \
    "ss.bmp", "../native-mac/Settings/ss.bmp"
/* CD audio tracks (AIL_redbook_*, WinApi-mss32-redbook.c): track n is this file */
#define GAME_CD_TRACK_FILE "Music\\Track%02d.ogg"
/* Start in full screen at the desktop size (Cmd+Return switches to a window) */
#define GAME_DISPLAY_MODE 1
/* revenant.ini values that the port replaces (section, key, value).
 * Software3D=Yes (the GOG default) selects the game's own 3D renderer
 * ("Blue"): it lights the 3D figures in 32 steps from one light, so the sides
 * away from the light are black. With No, the game draws them with Direct3D,
 * here the port's renderer (WinApi-d3d.c), as with a 3D card. */
#define GAME_INI_OVERRIDES { "Options", "Software3D", "No" }

#endif

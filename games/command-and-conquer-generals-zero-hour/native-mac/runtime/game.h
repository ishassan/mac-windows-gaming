/*
 *  Command & Conquer Generals Zero Hour: values for the shared runtime
 *  (see common/native-mac/runtime/game-info.h). MIT license.
 */

#ifndef GAME_H
#define GAME_H

#define GAME_TITLE        "Command & Conquer Generals Zero Hour"
#define GAME_SHORT_NAME   "Generals Zero Hour"
#define GAME_ENV_PREFIX   "GENERALSZH_"
#define GAME_MODULE_PATH  "C:\\game.dat"
#define GAME_DATA_DEFAULT "Games/Command and Conquer Generals Zero Hour/Original Game Files/Command and Conquer Generals Zero Hour"
#define GAME_DATA_CHECK   "INIZH.big"
#define GAME_CONFIG_FILE  "../../native-mac/Settings/GeneralsZH-native.cfg"
/* Layout of the game folder (~/Games/README.md): the game folder of the
 * runtime is the Zero Hour folder in Original Game Files. "My Documents"
 * (options, maps, replays) is native-mac/Settings, and the saves are the
 * shared Saves/Zero Hour (the same saves as the Wine versions; GeneralsX
 * saves do not load in this exe). GENERALSZH_DOCUMENTS=<folder> gives
 * another My Documents (the saves are then in its own Save folder). */
#define GAME_DOCUMENTS_PATH "C:\\..\\..\\native-mac\\Settings"
#define GAME_PATH_REDIRECTS \
    "../../native-mac/Settings/Command and Conquer Generals Zero Hour Data/Save", "../../Saves/Zero Hour"
/* The registry keys of the EA install (as in wine-11-athei/prefix.reg). The base game
 * files are in the folder next to the Zero Hour folder. */
#define GAME_REGISTRY_VALUES \
    { "HKLM\\Software\\Electronic Arts\\EA Games\\Generals", "InstallPath", 1, "C:\\..\\Command and Conquer Generals\\" }, \
    { "HKLM\\Software\\Electronic Arts\\EA Games\\Generals", "Language", 1, "english" }, \
    { "HKLM\\Software\\Electronic Arts\\EA Games\\Generals", "MapPackVersion", 4, "0x10000" }, \
    { "HKLM\\Software\\Electronic Arts\\EA Games\\Generals", "Version", 4, "0x10004" }, \
    { "HKLM\\Software\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour", "InstallPath", 1, "C:\\" }, \
    { "HKLM\\Software\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour", "Language", 1, "english" }, \
    { "HKLM\\Software\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour", "MapPackVersion", 4, "0x10000" }, \
    { "HKLM\\Software\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour", "Version", 4, "0x10004" }

/* Miles: one 3D provider (2D positional sound) for the unit sounds */
#define GAME_MSS_3D_PROVIDER 1

/* Miles end-of-sample callbacks on the main thread (from Sleep and
   PeekMessageA), not on a second thread: the callback changes the lists
   of the audio manager */
#define GAME_MSS_EOS_MAIN_THREAD 1

/* WSAStartup succeeds (sockets still fail): the skirmish player name is
   the host name from gethostname */
#define GAME_WSA_STARTUP_OK 1

#endif

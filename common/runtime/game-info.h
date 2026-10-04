/*
 *  Per-game values for the shared runtime.
 *  MIT license, see the README.md of the repository.
 *
 *  Each game folder has runtime/game.h with these macros:
 *    GAME_TITLE         window title
 *    GAME_SHORT_NAME    title of message boxes
 *    GAME_ENV_PREFIX    prefix of the environment variables ("COMMANDOS_")
 *    GAME_MODULE_PATH   Windows path of the exe that GetModuleFileName gives
 *    GAME_DATA_DEFAULT  default game folder, relative to $HOME
 *    GAME_DATA_CHECK    a file that must be in the game folder
 *    GAME_CONFIG_FILE   configuration file of the runtime (in the game folder)
 */

#ifndef NATIVE_GAME_INFO_H
#define NATIVE_GAME_INFO_H

#include "game.h"

#ifdef __cplusplus
extern "C"
#endif
/* getenv(GAME_ENV_PREFIX name): game_getenv("TRACE_MSG") reads COMMANDOS_TRACE_MSG */
const char *game_getenv(const char *name);

#endif

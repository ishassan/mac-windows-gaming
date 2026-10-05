/*
 *  Native port: program start (shared by all games).
 *
 *  Based on Game-Main.c from M-HT/SR (Septerra Core port), MIT license.
 *  See README.md for the license text.
 */

#define _FILE_OFFSET_BITS 64
#include "game-info.h"
#include <SDL.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "Game-Config.h"
#include "Game-Memory.h"
#include "platform.h"
#include "WinApi.h"
#include "ptr32.h"

extern "C" void InputScript_Start(void);
extern "C" void x86_install_crash_handler(void);

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

extern "C" {
/* llasm/asm-llasm.c: run the original program entry point (CRT start-up) */
extern int CCALL EntryPoint_asm(void);
}
extern thread_local uint32_t current_SEH_frame;
void Winapi_InitTicks(void);

extern "C" const char *game_getenv(const char *name)
{
    char key[128];
    snprintf(key, sizeof(key), "%s%s", GAME_ENV_PREFIX, name);
    return getenv(key);
}

/* Folder with the game files (the exe is not needed, only the data). */
static int change_to_data_folder(int argc, char *argv[])
{
    const char *path = game_getenv("DATA");
    char buf[4096], msg[4400];
    struct stat st;

    if (argc > 1 && argv[1][0] != '-')
    {
        path = argv[1];
    }
    if (path == NULL)
    {
        const char *home = getenv("HOME");
        snprintf(buf, sizeof(buf), "%s/" GAME_DATA_DEFAULT, home ? home : ".");
        path = buf;
    }
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode) || chdir(path) != 0)
    {
        snprintf(msg, sizeof(msg), "Game folder not found:\n%s\n\nSet " GAME_ENV_PREFIX "DATA to the folder with " GAME_DATA_CHECK ", or give the folder as the first argument.", path);
        eprintf("Error: %s\n", msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GAME_SHORT_NAME, msg, NULL);
        return -1;
    }
    if (stat(GAME_DATA_CHECK, &st) != 0)
    {
        snprintf(msg, sizeof(msg), GAME_DATA_CHECK " not found in:\n%s", path);
        eprintf("Error: %s\n", msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GAME_SHORT_NAME, msg, NULL);
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (sizeof(PTR32(void)) != 4)
    {
        eprintf("Error: the program was not compiled for 32-bit guest pointers\n");
        return 1;
    }

#ifdef PTROFS_64BIT
    if (0 != initialize_pointer_offset())
    {
        eprintf("Error initializing pointer offset\n");
        return 1;
    }
#endif

    if (0 != x86_init_malloc())
    {
        eprintf("Error initializing memory allocator\n");
        return 1;
    }

    if (change_to_data_folder(argc, argv) != 0)
    {
        return 1;
    }

    tzset();

    /* <P>BACKGROUND=1: no window on the screen and no sound, so that a test
     * does not take the screen, the keyboard or the speakers from the user.
     * SDL draws into memory (its "dummy" video driver). <P>DUMP and the
     * "shot" script command still save the game's frames. */
    const int background = (game_getenv("BACKGROUND") != NULL);
    if (background)
    {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
        SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
    }

    if (SDL_Init(SDL_INIT_NOPARACHUTE))
    {
        eprintf("Error: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    InputScript_Start();
    x86_install_crash_handler();

    atexit(SDL_Quit);

    ReadConfiguration(argc, argv);
    if (background) Display_Mode = 0;   /* a window, not full screen */

    /* empty SEH chain: fs:[0] = -1 */
    current_SEH_frame = 0xffffffff;

    Winapi_InitTicks();

    return EntryPoint_asm();
}

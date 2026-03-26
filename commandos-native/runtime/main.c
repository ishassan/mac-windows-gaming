/*
 *  Commandos: Behind Enemy Lines, native port: program start.
 *
 *  Based on Game-Main.c from M-HT/SR (Septerra Core port), MIT license.
 *  See README.md for the license text.
 */

#define _FILE_OFFSET_BITS 64
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

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

extern "C" {
/* llasm/asm-llasm.c: run the original program entry point (CRT start-up) */
extern int CCALL EntryPoint_asm(void);
}
extern uint32_t current_SEH_frame;
void Winapi_InitTicks(void);

/* Folder with the game files (comandos.exe is not needed, only the data). */
static int change_to_data_folder(int argc, char *argv[])
{
    const char *path = getenv("COMMANDOS_DATA");
    char buf[4096];
    struct stat st;

    if (argc > 1 && argv[1][0] != '-')
    {
        path = argv[1];
    }
    if (path == NULL)
    {
        const char *home = getenv("HOME");
        snprintf(buf, sizeof(buf), "%s/Games/Commandos Behind Enemy Lines/Game Data", home ? home : ".");
        path = buf;
    }
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode) || chdir(path) != 0)
    {
        eprintf("Error: game folder not found: %s\n", path);
        return -1;
    }
    if (stat("WARGAME.DIR", &st) != 0)
    {
        eprintf("Error: WARGAME.DIR not found in %s\n", path);
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

    if (SDL_Init(SDL_INIT_NOPARACHUTE))
    {
        eprintf("Error: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    atexit(SDL_Quit);

    ReadConfiguration(argc, argv);

    /* empty SEH chain: fs:[0] = -1 */
    current_SEH_frame = 0xffffffff;

    Winapi_InitTicks();

    return EntryPoint_asm();
}

/*
 *  Commandos port: scripted input for automatic tests.
 *  With COMMANDOS_SCRIPT=<file>, a thread sends SDL input events at given
 *  times after start-up. Each line of the file is:
 *      <ms> move <x> <y>       mouse moves to game position x, y
 *      <ms> click <x> <y>      left button down and up at x, y
 *      <ms> rclick <x> <y>     right button down and up at x, y
 *      <ms> key <name>         key down and up (SDL key name, e.g. Return;
 *                              Ctrl+S, Shift+Tab, Alt+X hold a modifier)
 *      <ms> shot <name>        save the next frame as $COMMANDOS_DUMP/<name>.bmp
 *      <ms> quit               stop the program
 *  Positions are in game pixels. Lines that start with "#" are comments.
 *  MIT license, see README.md.
 */

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void Display_RequestShot(const char *name);

static FILE *script;

static void push_motion(int x, int y, Uint32 state)
{
    SDL_Event e;
    static int last_x, last_y;
    memset(&e, 0, sizeof(e));
    e.type = SDL_MOUSEMOTION;
    e.motion.windowID = 0;   /* no window: the renderer does not scale it */
    e.motion.state = state;
    e.motion.x = x;
    e.motion.y = y;
    e.motion.xrel = x - last_x;
    e.motion.yrel = y - last_y;
    last_x = x;
    last_y = y;
    SDL_PushEvent(&e);
}

static void push_button(int x, int y, Uint8 button, int pressed)
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = pressed ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    e.button.windowID = 0;
    e.button.button = button;
    e.button.state = pressed ? SDL_PRESSED : SDL_RELEASED;
    e.button.clicks = 1;
    e.button.x = x;
    e.button.y = y;
    SDL_PushEvent(&e);
}

static Uint16 current_mods;   /* modifier keys held by the script */

static void push_key(const char *name, int pressed)
{
    SDL_Event e;
    SDL_Keycode key = SDL_GetKeyFromName(name);
    Uint16 mod = 0;
    if (key == SDLK_UNKNOWN)
    {
        fprintf(stderr, "input script: unknown key %s\n", name);
        return;
    }
    memset(&e, 0, sizeof(e));
    e.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.sym = key;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    switch (key)
    {
        case SDLK_LCTRL:  mod = KMOD_LCTRL; break;
        case SDLK_LSHIFT: mod = KMOD_LSHIFT; break;
        case SDLK_LALT:   mod = KMOD_LALT; break;
        default: break;
    }
    if (pressed) current_mods |= mod;
    else current_mods &= ~mod;
    e.key.keysym.mod = current_mods;
    SDL_PushEvent(&e);
}

static int script_thread(void *unused)
{
    char line[256], action[32], arg[128];
    Uint32 start = SDL_GetTicks();
    unsigned int ms;
    int x, y;

    while (fgets(line, sizeof(line), script))
    {
        if (line[0] == '#' || sscanf(line, "%u %31s", &ms, action) != 2) continue;
        while (SDL_GetTicks() - start < ms) SDL_Delay(5);

        if (strcmp(action, "move") == 0 && sscanf(line, "%*u %*s %d %d", &x, &y) == 2)
        {
            push_motion(x, y, 0);
        }
        else if ((strcmp(action, "click") == 0 || strcmp(action, "rclick") == 0) && sscanf(line, "%*u %*s %d %d", &x, &y) == 2)
        {
            Uint8 button = (action[0] == 'r') ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
            push_motion(x, y, 0);
            SDL_Delay(50);
            push_button(x, y, button, 1);
            SDL_Delay(200);   /* the game checks input about 20 times a second */
            push_button(x, y, button, 0);
        }
        else if (strcmp(action, "key") == 0 && sscanf(line, "%*u %*s %127s", arg) == 1)
        {
            /* "Ctrl+S", "Shift+Tab": hold the modifier around the key */
            char *key = arg, *plus = strchr(arg, '+');
            const char *mod = NULL;
            if (plus != NULL && plus != arg)
            {
                *plus = 0;
                mod = (strcmp(arg, "Ctrl") == 0) ? "Left Ctrl" : (strcmp(arg, "Shift") == 0) ? "Left Shift" : (strcmp(arg, "Alt") == 0) ? "Left Alt" : arg;
                key = plus + 1;
                push_key(mod, 1);
                SDL_Delay(100);
            }
            push_key(key, 1);
            SDL_Delay(250);
            push_key(key, 0);
            if (mod != NULL)
            {
                SDL_Delay(100);
                push_key(mod, 0);
            }
        }
        else if (strcmp(action, "shot") == 0 && sscanf(line, "%*u %*s %127s", arg) == 1)
        {
            Display_RequestShot(arg);
        }
        else if (strcmp(action, "quit") == 0)
        {
            fprintf(stderr, "input script: quit\n");
            fflush(stderr);
            _Exit(0);
        }
        else
        {
            fprintf(stderr, "input script: bad line: %s", line);
        }
    }
    return 0;
}

extern "C" void InputScript_Start(void)
{
    const char *path = getenv("COMMANDOS_SCRIPT");
    if (path == NULL) return;
    script = fopen(path, "r");
    if (script == NULL)
    {
        fprintf(stderr, "input script: cannot open %s\n", path);
        return;
    }
    SDL_DetachThread(SDL_CreateThread(script_thread, "input-script", NULL));
}

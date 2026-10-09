/**
 *  Port addition: Cmd+Q does not quit the game. It is easy to press by
 *  mistake instead of Cmd+1 (a key that many games use), and the progress
 *  since the last save is then lost. The Quit item of the app menu, Quit in
 *  the Dock and the game's own menu still quit.
 *
 *  SDL makes the app menu with Cocoa when it opens the video. Its Quit item
 *  has the key Cmd+Q, which macOS handles before the game sees the key.
 *  MacQuitKey_Remove takes the key off that item. WinApi-user32.c calls it
 *  when a window is made and when a window gets the focus, until the item is
 *  found, and it drops the Cmd+Q key events, so the game does not get them.
 *  The calls go through the Objective-C runtime, so this file stays C++.
 */

#include "game-info.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#if defined(__APPLE__)
#include <objc/runtime.h>
#include <objc/message.h>
#endif

extern "C" void MacQuitKey_Remove(void)
{
#if defined(__APPLE__)
    static int done;
    if (done) return;
    const char *driver = SDL_GetCurrentVideoDriver();
    if (driver == NULL || strcmp(driver, "cocoa") != 0) return;

    typedef id (*send_id)(id, SEL);
    typedef id (*send_id_long)(id, SEL, long);
    typedef long (*send_long)(id, SEL);
    typedef SEL (*send_sel)(id, SEL);
    typedef void (*send_void_id)(id, SEL, id);
    id app = ((send_id)objc_msgSend)((id)objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    id menu = (app != NULL) ? ((send_id)objc_msgSend)(app, sel_registerName("mainMenu")) : NULL;
    if (menu == NULL) return;
    /* [[NSString alloc] init]: the empty string, no key */
    id no_key = ((send_id)objc_msgSend)(((send_id)objc_msgSend)((id)objc_getClass("NSString"), sel_registerName("alloc")), sel_registerName("init"));
    SEL terminate = sel_registerName("terminate:");
    long top_count = ((send_long)objc_msgSend)(menu, sel_registerName("numberOfItems"));
    for (long i = 0; i < top_count; i++)
    {
        id top = ((send_id_long)objc_msgSend)(menu, sel_registerName("itemAtIndex:"), i);
        id submenu = ((send_id)objc_msgSend)(top, sel_registerName("submenu"));
        if (submenu == NULL) continue;
        long count = ((send_long)objc_msgSend)(submenu, sel_registerName("numberOfItems"));
        for (long j = 0; j < count; j++)
        {
            id item = ((send_id_long)objc_msgSend)(submenu, sel_registerName("itemAtIndex:"), j);
            if (((send_sel)objc_msgSend)(item, sel_registerName("action")) == terminate)
            {
                ((send_void_id)objc_msgSend)(item, sel_registerName("setKeyEquivalent:"), no_key);
                done = 1;
            }
        }
    }
    if (done && game_getenv("TRACE_MSG")) fprintf(stderr, "Cmd+Q taken off the Quit menu item\n");
#endif
}

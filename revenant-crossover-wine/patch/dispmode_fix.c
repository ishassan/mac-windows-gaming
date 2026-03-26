#include <windows.h>
#include <stdio.h>

static void hook_log(const char *fmt, ...) {
    FILE *f = fopen("C:\\Revenant\\dispmode_fix.log", "a");
    if (f) { va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap); fprintf(f, "\n"); fclose(f); }
}

static void *g_target = NULL;
static unsigned char g_orig_bytes[16], g_hook_bytes[16];
static int g_hook_size = 5;

typedef LONG (WINAPI *PFN_CDSEW)(LPCWSTR, DEVMODEW*, HWND, DWORD, LPVOID);

static LONG WINAPI Hooked(LPCWSTR dev, DEVMODEW *dm, HWND hwnd, DWORD flags, LPVOID lp) {
    DWORD op; LONG result;
    VirtualProtect(g_target, g_hook_size, PAGE_EXECUTE_READWRITE, &op);
    memcpy(g_target, g_orig_bytes, g_hook_size);
    VirtualProtect(g_target, g_hook_size, op, &op);

    if (!dm) { result = ((PFN_CDSEW)g_target)(dev, dm, hwnd, flags, lp); goto rehook; }

    hook_log("CDSEW: %lux%lux%lu", (unsigned long)dm->dmPelsWidth, (unsigned long)dm->dmPelsHeight, (unsigned long)dm->dmBitsPerPel);
    result = ((PFN_CDSEW)g_target)(dev, dm, hwnd, flags, lp);
    if (result == DISP_CHANGE_SUCCESSFUL) { hook_log("  OK"); goto rehook; }

    hook_log("  Failed(%ld), trying 960x600x32 IN-PLACE", result);
    /* Modify the DEVMODE in-place so Wine's ddraw sees the new mode */
    dm->dmPelsWidth=960; dm->dmPelsHeight=600; dm->dmBitsPerPel=32;
    dm->dmFields |= DM_PELSWIDTH|DM_PELSHEIGHT|DM_BITSPERPEL;
    result = ((PFN_CDSEW)g_target)(dev, dm, hwnd, flags, lp);
    if (result == DISP_CHANGE_SUCCESSFUL) { hook_log("  960x600 OK (in-place)"); goto rehook; }

    hook_log("  Faking success"); result = DISP_CHANGE_SUCCESSFUL;
rehook:
    VirtualProtect(g_target, g_hook_size, PAGE_EXECUTE_READWRITE, &op);
    memcpy(g_target, g_hook_bytes, g_hook_size);
    VirtualProtect(g_target, g_hook_size, op, &op);
    return result;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID p) {
    if (r == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        hook_log("=== dispmode_fix loaded ===");
        HMODULE u = GetModuleHandleA("user32.dll");
        if (!u) u = LoadLibraryA("user32.dll");
        g_target = (void*)GetProcAddress(u, "ChangeDisplaySettingsExW");
        memcpy(g_orig_bytes, g_target, g_hook_size);
        g_hook_bytes[0] = 0xE9;
        *(DWORD*)(g_hook_bytes+1) = (DWORD)(void*)Hooked - ((DWORD)g_target+5);
        DWORD op;
        VirtualProtect(g_target, g_hook_size, PAGE_EXECUTE_READWRITE, &op);
        memcpy(g_target, g_hook_bytes, g_hook_size);
        VirtualProtect(g_target, g_hook_size, op, &op);
        hook_log("Hook installed");
    }
    return TRUE;
}

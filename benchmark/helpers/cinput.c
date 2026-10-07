/* cinput.exe <script>: start C:\GOG Games\Commandos\comandos.exe, then replay a script
   ("<ms> key <Escape|Return|Down|Up|Left|Right>", "<ms> quit"), times from the process
   start. Lines it does not know are skipped. Build: i686-w64-mingw32-gcc -O2 -o cinput.exe cinput.c */
#include <windows.h>
#include <stdio.h>
#include <string.h>
static WORD vk_of(const char *n) {
    if (!strcmp(n, "Escape")) return VK_ESCAPE; if (!strcmp(n, "Return")) return VK_RETURN;
    if (!strcmp(n, "Down")) return VK_DOWN; if (!strcmp(n, "Up")) return VK_UP;
    if (!strcmp(n, "Left")) return VK_LEFT; if (!strcmp(n, "Right")) return VK_RIGHT; return 0; }
int main(int argc, char **argv) {
    STARTUPINFOA si = { sizeof(si) }; PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, "\"C:\\GOG Games\\Commandos\\comandos.exe\"", NULL, NULL, FALSE, 0, NULL, "C:\\GOG Games\\Commandos", &si, &pi)) return 1;
    DWORD t0 = GetTickCount(); FILE *f = fopen(argv[1], "r"); char line[256];
    while (fgets(line, sizeof line, f)) {
        unsigned ms; char cmd[32], arg[64] = "";
        if (line[0] == '#' || sscanf(line, "%u %31s %63s", &ms, cmd, arg) < 2) continue;
        while (GetTickCount() - t0 < ms) Sleep(5);
        if (!strcmp(cmd, "key")) { WORD vk = vk_of(arg); UINT sc = MapVirtualKeyA(vk, 0);
            DWORD ext = (vk == VK_DOWN || vk == VK_UP || vk == VK_LEFT || vk == VK_RIGHT) ? KEYEVENTF_EXTENDEDKEY : 0;
            keybd_event(vk, sc, ext, 0); Sleep(40); keybd_event(vk, sc, ext | KEYEVENTF_KEYUP, 0); }
        else if (!strcmp(cmd, "quit")) { TerminateProcess(pi.hProcess, 0); break; }
    }
    return 0;
}

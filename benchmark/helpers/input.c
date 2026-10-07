/* input.exe <script>: start C:\Revenant\Revenant.exe, then replay a script
   ("<ms> key ...", "<ms> click x y", "<ms> quit"; "key" always sends Escape),
   times from the process start. Build: i686-w64-mingw32-gcc -O2 -o input.exe input.c */
#include <windows.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    STARTUPINFOA si = { sizeof(si) }; PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, "C:\\Revenant\\Revenant.exe", NULL, NULL, FALSE, 0, NULL, "C:\\Revenant", &si, &pi)) return 1;
    DWORD t0 = GetTickCount();
    FILE *f = fopen(argv[1], "r"); char line[256];
    while (fgets(line, sizeof line, f)) {
        unsigned ms; char cmd[32], arg[64] = ""; int x = 0, y = 0;
        if (line[0] == '#' || sscanf(line, "%u %31s", &ms, cmd) < 2) continue;
        while (GetTickCount() - t0 < ms) Sleep(5);
        if (!strcmp(cmd, "key")) { keybd_event(VK_ESCAPE, 0x01, 0, 0); Sleep(30); keybd_event(VK_ESCAPE, 0x01, KEYEVENTF_KEYUP, 0); }
        else if (!strcmp(cmd, "click") && sscanf(line, "%u %31s %d %d", &ms, cmd, &x, &y) == 4) {
            SetCursorPos(x, y); Sleep(30);
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0); Sleep(50); mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        } else if (!strcmp(cmd, "quit")) { TerminateProcess(pi.hProcess, 0); break; }
    }
    return 0;
}

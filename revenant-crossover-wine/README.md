# How to Run Revenant (1999) on Mac Apple Silicon

Revenant is a 1999 action RPG by Cinematix Studios, originally released for Windows 95/98. The GOG version is patched to run on Windows XP through Windows 8, but there is no Mac version. If you try running it through Wine, Whisky, CrossOver, Porting Kit, Heroic, or any other Windows compatibility layer on a Mac with Apple Silicon, you will get this error:

```
Fatal Error: Direct Error DDERR_GENERIC in file d:\revenant\DirectDraw.cpp at line 333
```

This guide explains what causes this error and provides a step-by-step fix that gets the game running on Mac Apple Silicon.

## The Root Cause

The game uses DirectDraw (part of DirectX 6) to set the display mode to **640x480 at 16-bit color**. On Windows, this works because the OS can switch to virtually any resolution. On macOS Apple Silicon, the smallest display resolution available is **960x600** (and only as a HiDPI mode). The 640x480 mode simply does not exist.

When Wine translates the DirectDraw `SetDisplayMode(640, 480, 16)` call, it eventually calls `ChangeDisplaySettingsExW` in the Windows API. macOS rejects this with `DISP_CHANGE_BADMODE` because 640x480 is not a supported display mode. Wine's DirectDraw returns `DDERR_GENERIC` to the game, and the game shows the fatal error and exits.

**The fix:** We build a small custom DLL that hooks `ChangeDisplaySettingsExW`. When the game requests 640x480x16 and macOS rejects it, the hook redirects the request to 960x600x32 (the smallest mode macOS supports). The display mode change succeeds, and the game runs.

## What You Need

- A Mac with Apple Silicon (M1, M2, M3, M4, etc.)
- macOS 13 (Ventura) or later
- [Homebrew](https://brew.sh/) installed
- The GOG version of Revenant (purchased from [GOG.com](https://www.gog.com/en/game/revenant))
- The GOG offline installer: `setup_revenant_1.22l_(21853).exe`

## Step 1: Install Required Tools

Open Terminal and install the tools we need:

```bash
brew install innoextract mingw-w64
brew tap gcenx/wine
brew install --cask gcenx/wine/wine-crossover
```

This installs:
- **innoextract**: Extracts GOG installers without running them (no Windows needed)
- **mingw-w64**: Cross-compiler that builds Windows DLLs from macOS
- **Wine Crossover 23.7.1**: A macOS-optimized Wine build that can run Windows programs via Rosetta 2

When Wine Crossover installs, it will suggest installing Rosetta 2 if you have not already. Accept the prompt or run:

```bash
softwareupdate --install-rosetta --agree-to-license
```

## Step 2: Extract the Game

The GOG installer is a Windows executable (Inno Setup format). We extract it directly without running it:

```bash
mkdir -p ~/Games
innoextract --gog ~/Downloads/setup_revenant_1.22l_\(21853\).exe -d ~/Games/Revenant
```

This extracts the game files to `~/Games/Revenant/`. The main game files (Revenant.exe, DLLs, modules, music, etc.) are at the top level of this directory.

Copy the default config file to the game root:

```bash
cp ~/Games/Revenant/__support/app/revenant.ini ~/Games/Revenant/revenant.ini
```

## Step 3: Create the Display Mode Fix DLL

This is the key step. We build a custom DLL that intercepts the display mode change call and redirects it from 640x480 (unsupported) to 960x600 (supported).

### How it works

The game imports a DLL called `_inmm.dll` (a multimedia/audio library). We replace it with our own `_inmm.dll` that:

1. Forwards all 178 multimedia functions to the original (renamed to `_inmm_real.dll`)
2. On load, hooks `ChangeDisplaySettingsExW` in `user32.dll`
3. When the hook detects a failed display mode change, it retries with 960x600x32

### Create the source files

Create a directory for the build:

```bash
mkdir -p ~/Games/Revenant/ddraw_patch
```

Create the hook source code at `~/Games/Revenant/ddraw_patch/dispmode_fix.c`:

```c
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
```

### Generate the forwarding definition file

The proxy DLL needs to forward all 178 multimedia functions to the original `_inmm_real.dll`. Run this script to generate the `.def` file automatically:

```bash
cd ~/Games/Revenant

python3 << 'PYEOF'
import struct

with open("_inmm.dll", "rb") as f:
    data = f.read()

pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
export_rva = struct.unpack_from("<I", data, pe_offset + 0x78)[0]
num_sections = struct.unpack_from("<H", data, pe_offset + 0x06)[0]
opt_header_size = struct.unpack_from("<H", data, pe_offset + 0x14)[0]
section_start = pe_offset + 0x18 + opt_header_size

def rva_to_offset(rva):
    for i in range(num_sections):
        sec_off = section_start + i * 40
        vaddr = struct.unpack_from("<I", data, sec_off + 12)[0]
        vsize = struct.unpack_from("<I", data, sec_off + 8)[0]
        raw_off = struct.unpack_from("<I", data, sec_off + 20)[0]
        if vaddr <= rva < vaddr + vsize:
            return rva - vaddr + raw_off
    return rva

exp_off = rva_to_offset(export_rva)
num_names = struct.unpack_from("<I", data, exp_off + 24)[0]
name_rva_table = struct.unpack_from("<I", data, exp_off + 32)[0]

with open("ddraw_patch/_inmm.def", "w") as f:
    f.write("LIBRARY _INMM\nEXPORTS\n")
    for i in range(num_names):
        name_rva = struct.unpack_from("<I", data, rva_to_offset(name_rva_table) + i * 4)[0]
        name_off = rva_to_offset(name_rva)
        name = b""
        while data[name_off] != 0:
            name += bytes([data[name_off]])
            name_off += 1
        f.write(f"    {name.decode()} = _inmm_real.{name.decode()}\n")

print(f"Generated _inmm.def with {num_names} forwarded exports")
PYEOF
```

### Compile the DLL

```bash
cd ~/Games/Revenant/ddraw_patch

i686-w64-mingw32-gcc -shared -o _inmm.dll dispmode_fix.c _inmm.def \
    -luser32 -lkernel32 -O2
```

This cross-compiles a 32-bit Windows DLL from macOS.

### Deploy the DLL

Rename the original `_inmm.dll` and replace it with our proxy:

```bash
cd ~/Games/Revenant
cp _inmm.dll _inmm_real.dll
cp ddraw_patch/_inmm.dll _inmm.dll
```

## Step 4: Create a Wine Prefix

Wine needs a "prefix" (a virtual Windows filesystem). Create one for Revenant:

```bash
export WINEPREFIX=~/Games/Revenant/wineprefix_cx
wineboot --init
```

Wait for the initialization to complete (you may see some fixme/error messages, these are normal).

Link the game directory into the Wine prefix:

```bash
ln -sf ~/Games/Revenant "$WINEPREFIX/drive_c/Revenant"
```

Set the DirectDraw DLL override to use Wine's built-in version:

```bash
wine reg add "HKCU\\Software\\Wine\\DllOverrides" /v ddraw /t REG_SZ /d "builtin" /f
```

## Step 5: Run the Game

```bash
export WINEPREFIX=~/Games/Revenant/wineprefix_cx
export WINEDEBUG=-all
cd ~/Games/Revenant
wine "C:\\Revenant\\Revenant.exe"
```

The game will start. You should see the intro cinematics followed by the main menu with New Game, Load Game, Multiplayer, Options, and Exit.

If the game window does not appear in the foreground, press **Cmd+Tab** to find the Wine window, or use Mission Control.

### Create a Launch Script

For convenience, create a reusable launch script:

```bash
cat > ~/Games/Revenant/play_revenant.sh << 'EOF'
#!/bin/bash
export WINEPREFIX=~/Games/Revenant/wineprefix_cx
export WINEDEBUG=-all
cd ~/Games/Revenant
wine "C:\\Revenant\\Revenant.exe"
EOF

chmod +x ~/Games/Revenant/play_revenant.sh
```

Now you can launch the game anytime with:

```bash
~/Games/Revenant/play_revenant.sh
```

## Known Limitations

- **Not true fullscreen.** The game renders at 640x480, but the smallest display mode macOS Apple Silicon supports is 960x600. The game content appears in the upper-left portion of the screen with black borders on the right and bottom. The game is fully playable in this state.
- **OpenGL framebuffer warning.** You may see a `GL_INVALID_FRAMEBUFFER_OPERATION` error in Wine debug logs. This is from Wine's initial Direct3D hardware check and does not affect gameplay, since the game uses its built-in software renderer.
- **Display mode reverts on exit.** When you quit the game, your display resolution returns to normal automatically.

## How It Works (Technical Details)

For those curious about what is happening under the hood:

1. **innoextract** extracts the GOG installer (an Inno Setup package) directly on macOS, bypassing the need to run the Windows installer.

2. **Wine Crossover 23.7.1** (based on Wine 8.0.1) runs the 32-bit Windows game executable via Rosetta 2 (Apple's x86-to-ARM translation layer). Wine translates Windows API calls (DirectDraw, DirectInput, Win32) to macOS equivalents.

3. **The display mode hook** is the core fix. The game calls `IDirectDraw4::SetDisplayMode(640, 480, 16)`. Wine's DirectDraw translates this to `ChangeDisplaySettingsExW`, which asks macOS to change the display resolution. macOS rejects 640x480 because Apple Silicon displays do not support resolutions below 960x600. Our hook intercepts this rejection and retries with 960x600x32, which macOS accepts. The game proceeds past the DirectDraw initialization and runs normally.

4. **The DLL proxy technique** (also known as DLL hijacking) is how we inject the hook. The game imports `_inmm.dll` for multimedia/audio. We rename the original to `_inmm_real.dll` and replace it with our proxy. The proxy forwards all 178 multimedia functions to the original while also installing the display mode hook on load. This is a standard, well-known technique for patching old Windows games.

5. **MinGW-w64** is a cross-compiler that builds Windows PE32 DLLs from macOS. The proxy DLL is compiled as a 32-bit x86 Windows DLL that runs inside Wine via Rosetta 2.

## Troubleshooting

**Game shows "Fatal Error: DDERR_GENERIC"**
The hook DLL is not loading. Verify that `_inmm.dll` in the game directory is the proxy (should be ~112KB) and `_inmm_real.dll` is the original (~86KB). Check `dispmode_fix.log` in the game directory for hook status.

**Game does not start at all**
Make sure Wine Crossover is installed and `wine --version` shows `wine-8.0.1 (CrossOverFOSS 23.7.1)`. Ensure Rosetta 2 is installed.

**No sound**
The proxy DLL forwards audio calls to `_inmm_real.dll`. If there is no sound, verify that `_inmm_real.dll` exists and is the original file from the GOG installation.

**Black screen with no content**
Try pressing Escape or clicking in the window. The game may be playing the intro cinematic. If the screen stays black, check `dispmode_fix.log` to confirm the hook is working.

**Want to verify the hook is working?**
After launching the game, check the log file:
```bash
cat ~/Games/Revenant/dispmode_fix.log
```
You should see:
```
=== dispmode_fix loaded ===
Hook installed
CDSEW: 640x480x16
  Failed(-2), trying 960x600x32 IN-PLACE
  960x600 OK (in-place)
```

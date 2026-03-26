# How to Run Revenant (1999) on Mac Apple Silicon with Porting Kit (Fullscreen)

## Introduction

Revenant is a 1999 action RPG by Cinematix Studios, originally released for Windows 95/98. The GOG version is patched to run on modern Windows, but there is no Mac version. When you attempt to run it through any Wine-based compatibility layer on a Mac with Apple Silicon, the game fails with:

```
Fatal Error: Direct Error DDERR_GENERIC in file d:\revenant\DirectDraw.cpp at line 333
```

macOS on Apple Silicon does not support the 640x480 display resolution. The smallest available mode is 960x600 (and only as a HiDPI mode). When the game calls `IDirectDraw4::SetDisplayMode(640, 480, 16)`, Wine translates this to `ChangeDisplaySettingsExW`, which macOS rejects with `DISP_CHANGE_BADMODE`. Wine's DirectDraw returns `DDERR_GENERIC` to the game, and the game exits.

This guide provides three approaches to fix this, ranging from a fullscreen scaling proxy to a simple virtual desktop fallback.

## What You Need

- A Mac with Apple Silicon (M1, M2, M3, M4, etc.), running macOS 13 (Ventura) or later
- [Homebrew](https://brew.sh/) with mingw-w64 installed: `brew install mingw-w64`
- [Porting Kit](https://www.portingkit.com/) app (free download from portingkit.com)
- The GOG version of Revenant: `setup_revenant_1.22l_(21853).exe` (purchased from [GOG.com](https://www.gog.com/en/game/revenant))

## Install via Porting Kit

1. Open Porting Kit.
2. Click "New Port" and select the GOG installer (`setup_revenant_1.22l_(21853).exe`).
3. Configure the port with these settings:
   - **Wine engine:** WS12WineCX64Bit23.7.1
   - **Operating system:** Windows XP
   - **Window driver:** Mac Driver
   - **Dependencies:** None
   - **Main executable:** `C:\GOG Games\Revenant\Revenant.exe`
4. Let the installer run to completion inside the wrapper.

## Approach 1 (Work in Progress): _inmm.dll Hook with Fullscreen Scaling

**Status: Not yet working.** The hook infrastructure is functional (DirectDraw objects are wrapped, presentation is intercepted), but GDI StretchDIBits output is invisible because Wine's wined3d uses OpenGL for presentation, and OpenGL overwrites GDI drawing on the same window. See "Current Issues" below for details and potential solutions.

This approach uses Wine's complete built-in DirectDraw and Direct3D (so gameplay works, including the 3D renderer and textures), while adding fullscreen scaling via a hook DLL. The game's 640x480 output is intended to be scaled to fill the entire screen.

### How it works

The game imports `_inmm.dll` (a multimedia/audio library). We replace it with a proxy that:

1. Forwards all 178 multimedia functions to the original (renamed to `_inmm_real.dll`)
2. Hooks `ChangeDisplaySettingsExW` to redirect 640x480 to 960x600 when macOS rejects it
3. Hooks `DirectDrawCreate` and `DirectDrawCreateEx` in Wine's ddraw.dll
4. Patches the IDirectDraw v1 vtable to intercept `QueryInterface`, wrapping the returned IDirectDraw4 with a proxy
5. The IDirectDraw4 proxy intercepts `SetDisplayMode` (resize window to fullscreen), `SetCooperativeLevel` (capture HWND), and `CreateSurface` (wrap primary surface)
6. The primary surface proxy intercepts `Blt`/`BltFast`/`Flip` to read from the source surface and present via StretchDIBits
7. All Direct3D interfaces (IDirect3D3, IDirect3DDevice3, etc.) pass through to Wine's complete implementation

### Current Issues

**GDI/OpenGL conflict (blocking):** Wine's wined3d renders through OpenGL. GDI StretchDIBits draws to a different buffer than OpenGL, so the scaled output is immediately overwritten (or never visible). The result is either a black screen or content stuck in the top-left corner depending on whether Blt is forwarded to Wine or not.

Potential solutions to investigate:
- **Overlay window:** Create a separate topmost window for our scaled presentation, drawn on top of the game window
- **Force GDI renderer:** Set `DirectDrawRenderer=gdi` in Wine's Direct3D registry to make Wine use GDI for presentation instead of OpenGL
- **OpenGL presentation:** Use OpenGL (textured quad) instead of GDI StretchDIBits for our scaled output, so we draw through the same pipeline Wine uses
- **Subclass + swapbuffers hook:** Hook Wine's OpenGL SwapBuffers and do our scaling in OpenGL after Wine's present

### Build

You need mingw-w64 and Python 3:

```bash
brew install mingw-w64 python3
```

Generate the forwarding definition file from the game's original `_inmm.dll`:

```bash
GAME_DIR=~/Applications/Revenant.app/Contents/SharedSupport/prefix/drive_c/GOG\ Games/Revenant

cd patch
python3 gen_inmm_def.py "$GAME_DIR/_inmm.dll"
```

Compile the enhanced proxy:

```bash
i686-w64-mingw32-gcc -shared -o _inmm.dll fullscreen_proxy.c _inmm.def \
    -lgdi32 -luser32 -lkernel32 -O2 -Wl,--enable-stdcall-fixup
```

### Deploy

```bash
GAME_DIR=~/Applications/Revenant.app/Contents/SharedSupport/prefix/drive_c/GOG\ Games/Revenant

# Rename the original _inmm.dll (keep as backup for audio forwarding)
cp "$GAME_DIR/_inmm.dll" "$GAME_DIR/_inmm_real.dll"

# Deploy our proxy
cp _inmm.dll "$GAME_DIR/_inmm.dll"
```

Set Wine to use its built-in DirectDraw (not a native override). Edit the Wine user registry:

```bash
nano ~/Applications/Revenant.app/Contents/SharedSupport/prefix/user.reg
```

Find the `[Software\\Wine\\DllOverrides]` section and ensure:

```
"ddraw"="builtin"
```

If it says `"ddraw"="native"` (from a previous revenant_ddraw.c setup), change it to `"builtin"`.

### Run

Launch the game from Porting Kit or double-click `Revenant.app` in `~/Applications/`.

The game renders at 640x480 and the hook DLL scales the output to fill the entire screen. Wine's complete DirectDraw and Direct3D handle all rendering, so menus, cinematics, and gameplay (including 3D textures) should all work.

### Troubleshooting

**Check the log file.** The hook DLL writes debug output to `fullscreen_proxy.log` in the game directory:

```bash
cat ~/Applications/Revenant.app/Contents/SharedSupport/prefix/drive_c/GOG\ Games/Revenant/fullscreen_proxy.log
```

You should see:
```
=== fullscreen_proxy loaded ===
CDSEW hook installed at 0x...
DirectDrawCreateEx hook installed at 0x...
CDSEW: 640x480x16
  Failed(-2), trying 960x600x32 IN-PLACE
  960x600 OK (in-place)
SetCooperativeLevel: hwnd=0x... flags=0x...
SetDisplayMode: 640x480x16
SetDisplayMode: window resized to 960x600
CreateSurface: wrapped primary 0x... -> 0x...
```

**"Fatal Error: DDERR_GENERIC" still appears**: The hook DLL is not loading. Verify `_inmm.dll` in the game directory is the proxy (should be larger than the original) and `_inmm_real.dll` exists alongside it.

**No fullscreen scaling (content in upper-left corner)**: The DirectDrawCreateEx hook did not fire. Check the log for "DirectDrawCreateEx hook installed" and "wrapped DD4". Ensure `"ddraw"="builtin"` in `user.reg`.

**Game crashes on New Game**: Check if the log shows D3D-related errors. The proxy forwards all D3D to Wine's implementation, so D3D should work. If it does not, try Approach 2 (which is identical in architecture but may need different Wine configuration).

## Approach 2: Proxy ddraw.dll

An alternative to the _inmm.dll hook: replace `ddraw.dll` itself with a proxy that loads Wine's built-in ddraw under a different name.

### How it works

1. Copy Wine's built-in `ddraw.dll` from the Wine prefix system directory to the game directory as `ddraw_wine.dll`
2. Our proxy `ddraw.dll` (in the game directory) loads `ddraw_wine.dll` and forwards all calls
3. The proxy intercepts `SetDisplayMode` and `Blt` to add fullscreen scaling
4. Wine override: `"ddraw"="native"` (so Wine loads our proxy instead of its own)

### Risk

Wine's built-in DLLs may not load correctly when renamed. If `LoadLibrary("ddraw_wine.dll")` fails because Wine's loader doesn't recognize the renamed module, this approach won't work. In that case, use Approach 1 instead.

### Implementation status

Not yet implemented. The architecture is documented here as a fallback option. The `fullscreen_proxy.c` could be adapted to this approach by adding exported DirectDraw functions and loading `ddraw_wine.dll` in DllMain.

## Approach 3: Virtual Desktop + Window Scaling

The simplest approach: use Wine's Virtual Desktop feature to create a virtual display at 640x480, then let macOS handle the window scaling.

### How it works

1. Enable Wine's Virtual Desktop in the Porting Kit wrapper settings
2. Set the virtual desktop resolution to 640x480
3. The game thinks it has a 640x480 display
4. macOS displays the virtual desktop window, which can be scaled by the user

### Steps

In the Porting Kit wrapper, open the Wine configuration (Wineskin Advanced > Config Utility > Display tab) and:
- Check "Emulate a virtual desktop"
- Set the resolution to 640x480

### Limitations

- The game window is 640x480 pixels within the virtual desktop, not true fullscreen
- Scaling depends on macOS window management (no smooth bilinear interpolation)
- The virtual desktop adds a window border

## Backup: Custom DirectDraw Replacement (revenant_ddraw.c)

The original approach in this repository. The file `patch/revenant_ddraw.c` is a complete DirectDraw replacement that manages pixel buffers at 640x480x16 (RGB565) and scales via GDI StretchDIBits.

### Status

Works for menus and cinematics (fullscreen scaled), but starting actual gameplay fails with "Unable to load tormar texture" because the Direct3D stubs (IDirect3D3, IDirect3DDevice3, IDirect3DViewport3) are incomplete. The game's 3D renderer requires a more complete D3D implementation than what the stubs provide.

### Build (for reference)

```bash
cd patch
i686-w64-mingw32-gcc -shared -o ddraw.dll revenant_ddraw.c ddraw_new.def \
    -lgdi32 -luser32 -lkernel32 -O2 -Wl,--enable-stdcall-fixup
```

Deploy to the game directory with `"ddraw"="native"` in `user.reg`.

### Why not DDrawCompat?

[DDrawCompat](https://github.com/narzoul/DDrawCompat) was reported to work with Revenant on native Windows ([issue #489](https://github.com/narzoul/DDrawCompat/issues/489)). However, DDrawCompat explicitly does not support Wine. It works by hooking WDDM kernel-mode display drivers (`D3DKMTQueryAdapterInfo`, `D3DKMTPresent`, etc.) which do not exist in Wine's architecture. Wine translates DirectDraw to OpenGL via `wined3d.dll`, which is an entirely different stack. The GitHub issue #489 was tested on native Windows, not through Wine.

## Technical Details

### RGB565 to RGB8888 Conversion

The game renders in 16-bit RGB565 format. Before presenting via GDI StretchDIBits, each pixel is converted to 32-bit RGB8888 using a precomputed 64KB lookup table. The conversion expands each channel to 8 bits with proper rounding:
- Red: 5 bits shifted left by 3, with the top 2 bits replicated into the bottom 2
- Green: 6 bits shifted left by 2, with the top 2 bits replicated into the bottom 2
- Blue: 5 bits shifted left by 3, with the top 2 bits replicated into the bottom 2

### Fullscreen Scaling with StretchDIBits

After pixel conversion, StretchDIBits scales the 640x480 image to fill the game window's client area. The stretch mode is set to HALFTONE, which provides smooth bilinear-like interpolation.

### COM Proxy Architecture (Approach 1)

The _inmm.dll hook wraps Wine's DirectDraw objects using the standard COM proxy pattern:

- **ProxyDD4** wraps `IDirectDraw4` (28 methods). Most methods forward to Wine's real implementation. Intercepted methods: `SetCooperativeLevel` (captures HWND), `SetDisplayMode` (resizes window to fullscreen), `CreateSurface` (wraps primary surface).
- **ProxySurface** wraps `IDirectDrawSurface4` (45 methods, primary surface only). Intercepted methods: `Blt`, `BltFast`, `Flip` (adds StretchDIBits scaling after forwarding to Wine), `GetAttachedSurface` (returns Wine's real back buffer, unwrapped, so D3D can operate on it directly).
- **D3D interfaces** (`IDirect3D3`, `IDirect3DDevice3`, `IDirect3DViewport3`) are never wrapped. They come directly from Wine's complete implementation via `QueryInterface`.

This design ensures Wine handles all D3D rendering while we only intercept the 2D presentation path.

### Inline Hook Mechanism

Both hooks (ChangeDisplaySettingsExW and DirectDrawCreateEx) use the same 5-byte inline hook technique:
1. Save the original 5 bytes of the target function
2. Overwrite with a JMP instruction to our hook function
3. On each call: restore original bytes, call the real function, re-install the hook

This is the same proven technique used in the [crossover-wine approach](../revenant-crossover-wine/).

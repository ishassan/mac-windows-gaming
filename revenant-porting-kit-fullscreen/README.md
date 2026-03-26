# How to Run Revenant (1999) on Mac Apple Silicon with Porting Kit (Fullscreen)

## Introduction

Revenant is a 1999 action RPG by Cinematix Studios, originally released for Windows 95/98. The GOG version is patched to run on modern Windows, but there is no Mac version. When you attempt to run it through any Wine-based compatibility layer on a Mac with Apple Silicon, the game fails with:

```
Fatal Error: Direct Error DDERR_GENERIC in file d:\revenant\DirectDraw.cpp at line 333
```

This guide uses Porting Kit (a Wineskin-based tool for macOS) with a custom DirectDraw replacement DLL that provides true fullscreen scaling. Instead of just working around the display mode error, this approach replaces Wine's DirectDraw entirely with a self-contained implementation that manages pixel buffers and scales the game's 640x480 output to fill the entire screen via GDI StretchDIBits.

## What You Need

- A Mac with Apple Silicon (M1, M2, M3, M4, etc.), running macOS 13 (Ventura) or later
- [Homebrew](https://brew.sh/) with mingw-w64 installed: `brew install mingw-w64`
- [Porting Kit](https://www.portingkit.com/) app (free download from portingkit.com)
- The GOG version of Revenant: `setup_revenant_1.22l_(21853).exe` (purchased from [GOG.com](https://www.gog.com/en/game/revenant))

## The Root Cause

macOS on Apple Silicon does not support the 640x480 display resolution. The smallest available mode is 960x600 (and only as a HiDPI mode). When the game calls `IDirectDraw4::SetDisplayMode(640, 480, 16)`, Wine translates this to `ChangeDisplaySettingsExW`, which macOS rejects with `DISP_CHANGE_BADMODE`. Wine's DirectDraw returns `DDERR_GENERIC` to the game, and the game exits with the fatal error shown above.

Instead of simply hooking the display mode change (as in the CrossOver Wine approach), this method replaces DirectDraw entirely with a custom implementation. The custom DLL manages its own pixel buffers at 640x480x16 (RGB565) and uses GDI StretchDIBits to scale the rendered frame to fill the entire screen.

## How the Custom ddraw.dll Works

The custom `ddraw.dll` (source: `patch/revenant_ddraw.c`) is a complete DirectDraw replacement, not a proxy or wrapper. Here is how it is structured:

- **Replaces Wine's DirectDraw completely.** The DLL exports `DirectDrawCreate`, `DirectDrawCreateEx`, and the other standard entry points. When the game loads `ddraw.dll`, it gets our implementation instead of Wine's.
- **Implements IDirectDraw4, IDirectDrawSurface4, IDirect3D3 (stub), IDirect3DDevice3 (stub).** These are the COM interfaces the game queries for during initialization.
- **Manages its own pixel buffers.** The primary surface and back buffer are allocated as plain memory buffers at 640x480x16 in RGB565 format. The game locks these surfaces, writes pixels, and unlocks them, just as it would with real DirectDraw surfaces.
- **Presents frames via StretchDIBits.** On every `Blt` call that targets the primary surface, the DLL converts the RGB565 pixel data to RGB8888 using a precomputed lookup table, then calls `StretchDIBits` to scale the 640x480 image to fill the game window. The HALFTONE stretch mode provides smooth scaling.
- **Provides D3D stubs with texture support.** The game queries `IDirectDraw4` for `IDirect3D3`, then creates a `IDirect3DDevice3` and `IDirect3DViewport3`. The D3D device reports texture capabilities (RGB565, up to 1024x1024) so the game's texture loading pipeline succeeds. The device tracks its render target surface and returns it via `GetRenderTarget`.
- **Implements GetDC/ReleaseDC.** The game uses `GetDC` on surfaces to draw text via GDI (for menus and UI). The custom implementation creates a temporary 32-bit DIB section, copies the RGB565 surface data into it, hands the DC to the game, then copies the modified pixels back on `ReleaseDC`.
- **Implements IDirect3DTexture2.** Surfaces can be queried for the texture interface. The implementation provides `GetHandle` (returns a handle based on the surface pointer) and `Load` (copies pixel data between texture surfaces).

## Step 1: Install via Porting Kit

1. Open Porting Kit.
2. Click "New Port" and select the GOG installer (`setup_revenant_1.22l_(21853).exe`).
3. Configure the port with these settings:
   - **Wine engine:** WS12WineCX64Bit23.7.1
   - **Operating system:** Windows XP
   - **Window driver:** Mac Driver
   - **Dependencies:** None
   - **Main executable:** `C:\GOG Games\Revenant\Revenant.exe`
4. Let the installer run to completion inside the wrapper.

## Step 2: Compile the Custom ddraw.dll

You need mingw-w64 to cross-compile a 32-bit Windows DLL from macOS. If you have not installed it yet:

```bash
brew install mingw-w64
```

Then build the DLL:

```bash
cd patch
i686-w64-mingw32-gcc -shared -o ddraw.dll revenant_ddraw.c ddraw_new.def \
    -lgdi32 -luser32 -lkernel32 -O2 -Wl,--enable-stdcall-fixup
```

This produces a 32-bit Windows PE DLL (`ddraw.dll`) that replaces Wine's built-in DirectDraw.

## Step 3: Deploy

Copy the compiled `ddraw.dll` to the game directory inside the Porting Kit wrapper:

```bash
cp ddraw.dll ~/Applications/Revenant.app/Contents/SharedSupport/prefix/drive_c/GOG\ Games/Revenant/
```

Next, set Wine to use our DLL instead of its built-in DirectDraw. Edit the Wine user registry:

```bash
nano ~/Applications/Revenant.app/Contents/SharedSupport/prefix/user.reg
```

Find the `[Software\\Wine\\DllOverrides]` section and add the following line:

```
"ddraw"="native"
```

This tells Wine to load our `ddraw.dll` from the game directory (native) instead of its own built-in implementation.

## Step 4: Run the Game

Launch the game from Porting Kit or double-click `Revenant.app` in `~/Applications/`.

The game renders at 640x480 and the custom DLL scales the output to fill the entire screen. The intro cinematics and main menu will display in fullscreen.

## Current State

The game launches, renders menus and cinematics in fullscreen, and gameplay loads successfully. The 3D viewport renders with characters, environments, and UI elements visible. The following fixes have been applied:

- **Color key transparency (partial).** The custom DLL's `Blt` and `BltFast` implementations now handle source color key transparency (`DDBLT_KEYSRC`, `DDBLT_KEYSRCOVERRIDE`, `DDBLTFAST_SRCCOLORKEY`). Surfaces store their color key via `SetColorKey`, and pixels matching the key (typically magenta 0xF81F) are skipped during blits. This eliminated most of the pink/magenta artifacts in UI elements (HUD, inventory panel edges).
- **Aspect ratio preservation.** The 4:3 game content is now displayed with correct proportions. On wider displays, black pillarbox bars appear on the left and right. On taller displays, letterbox bars appear on top and bottom.
- **Exit game cleanup.** `RestoreDisplayMode` and `SetCooperativeLevel(DDSCL_NORMAL)` now restore the window to its original position and size. The presentation pipeline is guarded against calls during shutdown. The `DllMain` detach handler also restores the window as a safety net.

The following fixes have been applied to address previously known issues:

- **Exit game partially fixed.** The initial exit freeze (caused by `present_frame()` being gated on `g_is_fullscreen`) was resolved by removing that guard and adding `SWP_ASYNCWINDOWPOS` to `SetWindowPos` in `restore_window()`. `DD4_RestoreDisplayMode` and `DD4_SetCooperativeLevel(DDSCL_NORMAL)` now post `WM_DISPLAYCHANGE` to the game window. Exit works on fresh launches but can still freeze after extended gameplay (see known issues).
- **Defensive surface creation.** `DD4_CreateSurface` now creates a fallback offscreen surface for any unrecognized caps instead of returning `DDERR_INVALIDCAPS`. `FlipToGDISurface` now presents the current primary frame instead of being a no-op.
- **D3D state storage.** The D3D device stub previously discarded all `SetRenderState` values and returned 0 from `GetRenderState`. The fix adds storage arrays for render states, light states, texture stage states, material data (`SetMaterial`/`GetMaterial`), and light data (`SetLight`/`GetLight`) so that Set/Get round-trips work correctly. This was a necessary correctness fix but did not resolve the lighting-after-load issue (see known issues).

The following issues remain:

- **Lighting changes after loading a saved game.** Saving and then loading a game results in noticeably darker lighting. Extensive log analysis ruled out the D3D state layer as the cause: the DLL is fully unloaded and reloaded on save/load (the game reinitializes DirectDraw from scratch), ambient light (`SetLightState state=2`) is always set to the same value (0xff5f5a55), and the game never calls `SetRenderState`/`GetRenderState` at all (only the older `SetLightState`/`GetLightState` API). The game reads `GetLightState state=1` (material handle) ~765K times per session, always returning 0x10 correctly. The game creates 231 materials and 64 lights per session via singletons (each `CreateMaterial`/`CreateLight` returns the same global object), but never calls `GetMaterial`/`GetLight` to read properties back. The game does pure software rendering via locked surfaces (no `BeginScene`/`EndScene` or `DrawPrimitive` calls), so the pixel data written to the backbuffer is entirely produced by the game executable's own lighting code. The darkness is in the game's own pixel output, not in the DLL's presentation pipeline. The root cause likely lies in the game executable's internal lighting state management during save/load, which is outside the DLL's control.
- **Pink/magenta halos around text.** Text rendered via GDI (`GetDC`/`ReleaseDC`) still shows magenta backgrounds around characters. Log analysis shows the game fills text surfaces with magenta (`colorfill 0xf81f`), draws text via GetDC, then blits the result WITHOUT any color key flags (`DDBLT_KEYSRC` is not set). The game's text blitting path simply does not request color key transparency, so the magenta pixels are copied as-is. This differs from art asset blits, which DO use `DDBLT_KEYSRC` and work correctly (14K+ transparent blits per session). Auto-applying magenta transparency to all magenta-filled surfaces was attempted but caused regressions in UI rendering (buttons, panels). A targeted fix would need to distinguish text-only surfaces from general UI surfaces, which requires deeper analysis of the game's surface allocation patterns.
- **Exit game freezes after extended play.** Pressing Escape and clicking "Exit Program" works on a fresh launch but can freeze after playing for a while. The exit sequence involves `RestoreDisplayMode` and `SetCooperativeLevel(DDSCL_NORMAL)` which restore the window, but something in the game's shutdown path may deadlock after extended gameplay. The initial exit freeze fix (removing the `g_is_fullscreen` guard from `present_frame` and adding `SWP_ASYNCWINDOWPOS`) resolved the immediate case but does not cover all scenarios.
- **No Flip-based rendering.** The game uses `Blt` (not `Flip`) to present frames. The fullscreen scaling triggers on every `Blt` to the primary surface. This is working as expected.

Note: Save game functionality works correctly. The save dialog only appears after any active character dialog finishes (this is normal game behavior, not a bug).

## Troubleshooting

**Check the log file.** The custom DLL writes detailed debug output to `revenant_ddraw.log` in the game directory. This is your first resource for diagnosing issues:

```bash
cat ~/Applications/Revenant.app/Contents/SharedSupport/prefix/drive_c/GOG\ Games/Revenant/revenant_ddraw.log
```

**"This game requires DirectX 6.0 or higher."** The IDirect3D3 stub is not being found by the game. Verify that `ddraw.dll` is in the game directory and that the DLL override in `user.reg` is set to `"ddraw"="native"`. If the override is missing or set to `"builtin"`, Wine will use its own DirectDraw and the game will not find our D3D stubs.

**No window appears but audio plays.** The `present_frame` function is not being called. Open the log and check whether `Blt` to the primary surface is happening. If there are no Blt log entries, the game may be using a different presentation path.

**Black screen with no content.** Try pressing Escape or clicking in the window. The game may be playing the intro cinematic. If the screen stays black, check the log for surface creation and Blt activity.

## Technical Details

### RGB565 to RGB8888 Conversion

The game renders in 16-bit RGB565 format (5 bits red, 6 bits green, 5 bits blue). Before presenting to the screen via GDI, each pixel must be converted to 32-bit RGB8888. The DLL precomputes a 64KB lookup table (`g_rgb565_lut[65536]`) at initialization. Each entry maps a 16-bit RGB565 value to its 32-bit equivalent. Conversion is O(1) per pixel: a single array lookup per pixel in the frame buffer.

The conversion expands each channel to 8 bits with proper rounding:
- Red: 5 bits shifted left by 3, with the top 2 bits replicated into the bottom 2
- Green: 6 bits shifted left by 2, with the top 2 bits replicated into the bottom 2
- Blue: 5 bits shifted left by 3, with the top 2 bits replicated into the bottom 2

### Fullscreen Scaling with StretchDIBits

After RGB565 to RGB8888 conversion, the DLL calls `StretchDIBits` to blit the 640x480 image to fill the window's client area. The stretch mode is set to `HALFTONE` via `SetStretchBltMode`, which provides smooth bilinear-like interpolation when scaling up. The window itself is resized in `SetDisplayMode` to match the full screen dimensions using `GetSystemMetrics(SM_CXSCREEN)` and `GetSystemMetrics(SM_CYSCREEN)`.

### COM Vtable Structure

The DLL implements COM interfaces using C vtable structs:

- **IDirectDraw4:** 28 methods in the vtable (3 IUnknown + 25 IDirectDraw4-specific). The most important implementations are `SetCooperativeLevel` (captures the game window handle), `SetDisplayMode` (resizes the window to fill the screen), and `CreateSurface` (allocates primary, back buffer, and offscreen surfaces).
- **IDirectDrawSurface4:** 45 methods in the vtable (3 IUnknown + 42 IDirectDrawSurface4-specific). Key implementations are `Lock`/`Unlock` (provides pixel buffer access), `Blt` (triggers presentation when targeting primary), `GetDC`/`ReleaseDC` (GDI text rendering support), and `GetAttachedSurface` (returns the back buffer).

### Window Resizing in SetDisplayMode

When the game calls `SetDisplayMode(640, 480, 16)`, the custom DLL does not actually change the display resolution. Instead, it resizes the game window to fill the screen by calling `SetWindowPos` with coordinates `(0, 0, screen_width, screen_height)` and flags `SWP_NOZORDER | SWP_FRAMECHANGED`. It also strips the window style to `WS_POPUP` (no title bar or borders) for a clean fullscreen look.

### IDirect3D3, IDirect3DDevice3, and IDirect3DViewport3 Stubs

The game queries `IDirectDraw4` for `IDirect3D3` during initialization. From there, it creates a `IDirect3DDevice3` (a HAL or RGB software device) and a `IDirect3DViewport3`. These interfaces are stubbed:

- **IDirect3D3** (13 methods): `QueryInterface`, `AddRef`, `Release`, `EnumDevices` (calls the callback with a fake "HAL" device), `CreateDevice`, `CreateViewport`, and others returning `DD_OK` or `D3D_OK`.
- **IDirect3DDevice3** (50 methods): Most methods return `D3D_OK` as no-ops. `GetCaps` reports texture and rendering capabilities (RGB565, texture sizes 1x1 to 1024x1024). `GetRenderTarget` / `SetRenderTarget` track the active render target surface. `GetDirect3D` returns the D3D3 object. `SetRenderState` and `SetTextureStageState` are logged but otherwise ignored.
- **IDirect3DViewport3** (20 methods): `SetViewport2` stores viewport parameters. Other methods return `D3D_OK`.

These stubs exist because the game checks for D3D device availability and texture capabilities during initialization. Without these stubs, the game fails with "This game requires DirectX 6.0 or higher." The device capability reporting is also critical for texture loading: the game checks `D3DDEVICEDESC.dpcTriCaps.dwTextureCaps` to confirm texture support before loading game assets.

### IDirect3DTexture2 and Texture Handle Management

Surfaces created with `DDSCAPS_TEXTURE` can be queried for the `IDirect3DTexture2` interface. The implementation:

- **GetHandle:** Returns a handle derived from the surface pointer. The game uses these handles in `SetRenderState(D3DRENDERSTATE_TEXTUREHANDLE, ...)` calls.
- **Load:** Copies pixel data from one texture surface to another row-by-row, handling pitch mismatches between source and destination surfaces. This is used by the game to upload texture data to "device" textures.

A pool of up to 512 texture objects is maintained (`g_tex_pool`), mapping surfaces to their texture interface wrappers.

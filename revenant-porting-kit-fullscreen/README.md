# Revenant on Mac Apple Silicon with Porting Kit: Hybrid `ddraw` Proxy

## Overview

Revenant fails on Apple Silicon Wine wrappers with:

```text
Fatal Error: Direct Error DDERR_GENERIC in file d:\revenant\DirectDraw.cpp at line 333
```

The root cause is still the same: the game asks DirectDraw to switch to `640x480x16`, and macOS rejects that mode.

This repo now uses a hybrid approach instead of a fake DirectDraw implementation:

- `_inmm.dll` is a proxy that hooks `ChangeDisplaySettingsExW` and rewrites the failing `640x480x16` request to `960x600x32`.
- `ddraw.dll` is a proxy around Wine's real 32-bit `ddraw`, loaded from a local `ddraw_real.dll` sidecar.
- The proxy keeps Wine in charge of surfaces, `GetDC`, textures, and Direct3D, but intercepts the primary-surface present path and scales the final `640x480` frame to the window with `StretchDIBits`.

That avoids the stub-completeness problems of the previous fully custom `ddraw.dll` while keeping the fullscreen scaling path in one place.

## Why This Exists

The old approaches had opposite tradeoffs:

- The `_inmm.dll` display-mode hook was reliable because it let Wine's real DirectDraw handle the game, but it did not provide a good fullscreen presentation path.
- The old self-contained `revenant_ddraw.c` provided custom fullscreen scaling, but it had to fake large parts of DirectDraw and Direct3D and was therefore brittle.

The current code combines them:

- use the old `_inmm.dll` mode-fix injection to get past initialization
- use Wine's real DirectDraw for correctness
- keep only the fullscreen presenter and window-management logic in the local `ddraw.dll`

## Files

- `patch/revenant_ddraw.c`: `ddraw.dll` proxy implementation
- `patch/ddraw_new.def`: exported `ddraw.dll` entry points
- `patch/dispmode_fix.c`: `_inmm.dll` mode-fix hook
- `patch/_inmm.def`: full `_inmm.dll` forwarder definition

## Build

Install MinGW if needed:

```bash
brew install mingw-w64
```

Build `ddraw.dll`:

```bash
cd patch
i686-w64-mingw32-gcc -shared -o ddraw.dll revenant_ddraw.c ddraw_new.def \
    -lgdi32 -luser32 -lkernel32 -lole32 -O2 -Wl,--enable-stdcall-fixup
```

Build `_inmm.dll`:

```bash
cd patch
i686-w64-mingw32-gcc -shared -o _inmm.dll dispmode_fix.c _inmm.def \
    -luser32 -lkernel32 -O2
```

## Deploy to Porting Kit

Assuming the game is installed in:

```text
~/Applications/Revenant.app/Contents/SharedSupport/prefix/drive_c/GOG Games/Revenant/
```

Copy the new `ddraw.dll`:

```bash
cp patch/ddraw.dll ~/Applications/Revenant.app/Contents/SharedSupport/prefix/drive_c/GOG\ Games/Revenant/
```

Copy the real Wine 32-bit `ddraw.dll` next to it as `ddraw_real.dll`:

```bash
cp ~/Applications/Revenant.app/Contents/SharedSupport/prefix/drive_c/windows/syswow64/ddraw.dll \
   ~/Applications/Revenant.app/Contents/SharedSupport/prefix/drive_c/GOG\ Games/Revenant/ddraw_real.dll
```

Replace `_inmm.dll` with the proxy and keep the original as `_inmm_real.dll`:

```bash
cd ~/Applications/Revenant.app/Contents/SharedSupport/prefix/drive_c/GOG\ Games/Revenant/
mv _inmm.dll _inmm_real.dll
cp /path/to/repo/patch/_inmm.dll .
```

Keep the Wine `ddraw` override set to `native` so the game loads the local proxy DLL:

```text
[Software\\Wine\\DllOverrides]
"ddraw"="native"
```

## Runtime Design

### `_inmm.dll`

The `_inmm` proxy is the early injection point. On process attach it:

- loads `user32.dll`
- hooks `ChangeDisplaySettingsExW`
- retries failed display-mode changes as `960x600x32`
- forwards all original `_inmm` exports to `_inmm_real.dll`

### `ddraw.dll`

The local `ddraw.dll` no longer emulates DirectDraw objects. It:

- loads the real 32-bit Wine `ddraw` from local `ddraw_real.dll`
- forwards `DirectDrawCreate*` calls to Wine
- patches the returned COM interfaces in place
- intercepts only:
  - `QueryInterface` and `Release`
  - `CreateSurface`, `DuplicateSurface`, `GetGDISurface`, `GetSurfaceFromDC`
  - `SetCooperativeLevel`, `SetDisplayMode`, `RestoreDisplayMode`
  - surface `Blt`, `BltFast`, `Flip`, and `GetAttachedSurface`

Everything else goes straight to Wine.

### Fullscreen Presentation

When the game blits to the primary surface, the proxy:

1. lets Wine do the real blit first
2. locks the real primary surface and reads the top-left `640x480` final output
3. copies the top-left `640x480` pixels into a local 32-bit buffer
4. stretches that buffer into a centered 4:3 rectangle with black bars as needed

If primary-surface locking fails, the proxy falls back to capturing the top-left `640x480` region from the window client DC and scaling that.

The window-resize logic is still local to this repo:

- `SetDisplayMode` expands the game window to the screen
- `RestoreDisplayMode` and `SetCooperativeLevel(DDSCL_NORMAL)` restore the original window rect

## Logs

The proxies log to the game directory:

- `revenant_ddraw.log`
- `dispmode_fix.log`

These are the first files to check if the game still fails on startup or the scaling path is not active.

## Notes

- The proxy prefers a local `ddraw_real.dll` and only falls back to Wine system paths if needed.
- This repo no longer carries custom Direct3D device, texture, or `GetDC` stubs. Those calls now go to Wine.
- If a future issue appears, the default debugging question should be whether the failure is in the `_inmm` mode-fix path or in the `ddraw` presentation hook path; they are now separate.

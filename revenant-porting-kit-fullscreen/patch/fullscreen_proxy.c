/*
 * fullscreen_proxy.c - Enhanced _inmm.dll hook for Revenant (1999) fullscreen scaling
 *
 * Hooks:
 *   1. ChangeDisplaySettingsExW: redirects 640x480 to 960x600 when macOS rejects it
 *   2. DirectDrawCreateEx: wraps IDirectDraw4 and primary surface for fullscreen scaling
 *
 * Wine's built-in DirectDraw and D3D handle all actual rendering.
 * We only intercept the presentation path to add StretchDIBits fullscreen scaling.
 *
 * Build as _inmm.dll proxy:
 *   i686-w64-mingw32-gcc -shared -o _inmm.dll fullscreen_proxy.c _inmm.def \
 *       -lgdi32 -luser32 -lkernel32 -O2 -Wl,--enable-stdcall-fixup
 */

#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
#include <string.h>

#ifndef DDLOCK_READONLY
#define DDLOCK_READONLY 0x00000010L
#endif

/* ================================================================
 * Logging
 * ================================================================ */

static FILE *g_logfile = NULL;

static void hook_log(const char *fmt, ...) {
    if (!g_logfile) g_logfile = fopen("fullscreen_proxy.log", "a");
    if (g_logfile) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(g_logfile, fmt, ap);
        va_end(ap);
        fprintf(g_logfile, "\n");
        fflush(g_logfile);
    }
}

/* ================================================================
 * GUIDs
 * ================================================================ */

static const GUID MY_IID_IDirectDraw4 =
    {0x9C59509A,0x39BD,0x11D1,{0x8C,0x4A,0x00,0xC0,0x4F,0xD9,0x30,0xC5}};

static int guid_eq(REFIID a, const GUID *b) {
    return memcmp(a, b, sizeof(GUID)) == 0;
}

/* ================================================================
 * Inline hook infrastructure
 * ================================================================ */

typedef struct {
    void *target;
    unsigned char orig_bytes[16];
    unsigned char hook_bytes[16];
    int size;
} InlineHook;

static void install_inline_hook(InlineHook *h, void *target, void *detour) {
    DWORD op;
    h->target = target;
    h->size = 5;
    memcpy(h->orig_bytes, target, h->size);
    h->hook_bytes[0] = 0xE9; /* JMP rel32 */
    *(DWORD *)(h->hook_bytes + 1) = (DWORD)(uintptr_t)detour - ((DWORD)(uintptr_t)target + 5);
    VirtualProtect(target, h->size, PAGE_EXECUTE_READWRITE, &op);
    memcpy(target, h->hook_bytes, h->size);
    VirtualProtect(target, h->size, op, &op);
}

static void pause_hook(InlineHook *h) {
    DWORD op;
    VirtualProtect(h->target, h->size, PAGE_EXECUTE_READWRITE, &op);
    memcpy(h->target, h->orig_bytes, h->size);
    VirtualProtect(h->target, h->size, op, &op);
}

static void resume_hook(InlineHook *h) {
    DWORD op;
    VirtualProtect(h->target, h->size, PAGE_EXECUTE_READWRITE, &op);
    memcpy(h->target, h->hook_bytes, h->size);
    VirtualProtect(h->target, h->size, op, &op);
}

/* ================================================================
 * Fullscreen presentation: RGB565 LUT + StretchDIBits
 * ================================================================ */

static HWND g_hwnd = NULL;
static DWORD *g_present_buf = NULL;
static DWORD g_rgb565_lut[65536];
static BITMAPINFO g_bmi;

static void init_lut(void) {
    int i;
    for (i = 0; i < 65536; i++) {
        DWORD r5 = (i >> 11) & 0x1F;
        DWORD g6 = (i >> 5) & 0x3F;
        DWORD b5 = i & 0x1F;
        DWORD r8 = (r5 << 3) | (r5 >> 2);
        DWORD g8 = (g6 << 2) | (g6 >> 4);
        DWORD b8 = (b5 << 3) | (b5 >> 2);
        g_rgb565_lut[i] = (r8 << 16) | (g8 << 8) | b8;
    }
}

static void init_bmi(void) {
    memset(&g_bmi, 0, sizeof(g_bmi));
    g_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bmi.bmiHeader.biWidth = 640;
    g_bmi.bmiHeader.biHeight = -480; /* top-down */
    g_bmi.bmiHeader.biPlanes = 1;
    g_bmi.bmiHeader.biBitCount = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;
}

/* Forward declarations */
typedef struct ProxySurface ProxySurface;
typedef struct ProxyDD4 ProxyDD4;

static int g_present_count = 0;

static void present_scaled(IDirectDrawSurface4 *real_primary) {
    DDSURFACEDESC2 desc;
    HRESULT hr;
    HDC hdc;
    RECT rc;

    if (!g_hwnd || !real_primary || !g_present_buf) {
        if (g_present_count < 5)
            hook_log("present_scaled: SKIP hwnd=%p primary=%p buf=%p",
                     g_hwnd, real_primary, g_present_buf);
        return;
    }

    memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(desc);

    hr = real_primary->lpVtbl->Lock(real_primary, NULL, &desc,
                                     DDLOCK_READONLY | DDLOCK_WAIT, NULL);
    if (FAILED(hr)) {
        if (g_present_count < 5)
            hook_log("present_scaled: Lock failed hr=0x%lx", (unsigned long)hr);
        return;
    }

    if (g_present_count < 5)
        hook_log("present_scaled: Lock OK %lux%lu bpp=%lu pitch=%ld",
                 desc.dwWidth, desc.dwHeight,
                 desc.ddpfPixelFormat.dwRGBBitCount, desc.lPitch);

    if (desc.ddpfPixelFormat.dwRGBBitCount == 16) {
        WORD *src = (WORD *)desc.lpSurface;
        int pitch_w = (int)(desc.lPitch / 2);
        int h = (int)desc.dwHeight, w = (int)desc.dwWidth;
        int y, x;
        if (h > 480) h = 480;
        if (w > 640) w = 640;
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                g_present_buf[y * 640 + x] = g_rgb565_lut[src[y * pitch_w + x]];
    } else if (desc.ddpfPixelFormat.dwRGBBitCount == 32) {
        DWORD *src = (DWORD *)desc.lpSurface;
        int pitch_dw = (int)(desc.lPitch / 4);
        int h = (int)desc.dwHeight, w = (int)desc.dwWidth;
        int y;
        if (h > 480) h = 480;
        if (w > 640) w = 640;
        for (y = 0; y < h; y++)
            memcpy(&g_present_buf[y * 640], &src[y * pitch_dw], (size_t)(w * 4));
    }

    real_primary->lpVtbl->Unlock(real_primary, NULL);

    hdc = GetDC(g_hwnd);
    if (!hdc) return;
    GetClientRect(g_hwnd, &rc);
    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, NULL);
    StretchDIBits(hdc, 0, 0, rc.right, rc.bottom,
                  0, 0, 640, 480,
                  g_present_buf, &g_bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(g_hwnd, hdc);

    if (g_present_count < 5)
        hook_log("present_scaled: StretchDIBits to %ldx%ld", rc.right, rc.bottom);
    g_present_count++;
}

/* ================================================================
 * ProxySurface: IDirectDrawSurface4 wrapper (primary surface only)
 *
 * Wraps Wine's real primary surface. Intercepts Blt/BltFast/Flip
 * to add StretchDIBits fullscreen scaling. Returns unwrapped Wine
 * surfaces from GetAttachedSurface so D3D can operate on them.
 * ================================================================ */

static IDirectDrawSurface4Vtbl g_proxy_surf_vtbl;

struct ProxySurface {
    IDirectDrawSurface4Vtbl *lpVtbl;
    IDirectDrawSurface4 *real;
    ULONG refcount;
};

static ProxySurface *g_proxy_primary = NULL;

static IDirectDrawSurface4 *unwrap_surf(IDirectDrawSurface4 *s) {
    if (s && s->lpVtbl == &g_proxy_surf_vtbl)
        return ((ProxySurface *)s)->real;
    return s;
}

static ProxySurface *wrap_surface(IDirectDrawSurface4 *real) {
    ProxySurface *p = (ProxySurface *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ProxySurface));
    if (!p) return NULL;
    p->lpVtbl = &g_proxy_surf_vtbl;
    p->real = real;
    p->refcount = 1;
    return p;
}

/* --- IUnknown --- */

static HRESULT WINAPI PSurf_QueryInterface(IDirectDrawSurface4 *iface, REFIID riid, void **out) {
    /* Forward to Wine: returns Wine's real D3D interfaces */
    return ((ProxySurface *)iface)->real->lpVtbl->QueryInterface(
        ((ProxySurface *)iface)->real, riid, out);
}

static ULONG WINAPI PSurf_AddRef(IDirectDrawSurface4 *iface) {
    return ++((ProxySurface *)iface)->refcount;
}

static ULONG WINAPI PSurf_Release(IDirectDrawSurface4 *iface) {
    ProxySurface *self = (ProxySurface *)iface;
    ULONG ref = --self->refcount;
    if (ref == 0) {
        hook_log("PSurf_Release: freeing wrapped primary");
        if (g_proxy_primary == self) g_proxy_primary = NULL;
        if (self->real) self->real->lpVtbl->Release(self->real);
        HeapFree(GetProcessHeap(), 0, self);
    }
    return ref;
}

/* --- Intercepted presentation methods --- */

static HRESULT WINAPI PSurf_Blt(IDirectDrawSurface4 *iface, LPRECT dst,
                                 IDirectDrawSurface4 *src, LPRECT srcRect,
                                 DWORD flags, LPDDBLTFX fx) {
    ProxySurface *self = (ProxySurface *)iface;
    IDirectDrawSurface4 *real_src = unwrap_surf(src);

    if (real_src && !(flags & DDBLT_COLORFILL)) {
        /* Don't forward to Wine: read from SOURCE surface and present ourselves.
         * This prevents Wine's wined3d/OpenGL from overwriting our scaled output. */
        present_scaled(real_src);
        return DD_OK;
    }

    /* Color fills and other ops: forward to Wine */
    return self->real->lpVtbl->Blt(self->real, dst, real_src, srcRect, flags, fx);
}

static HRESULT WINAPI PSurf_BltFast(IDirectDrawSurface4 *iface, DWORD dx, DWORD dy,
                                     IDirectDrawSurface4 *src, LPRECT srcRect, DWORD flags) {
    ProxySurface *self = (ProxySurface *)iface;
    IDirectDrawSurface4 *real_src = unwrap_surf(src);

    if (real_src) {
        present_scaled(real_src);
        return DD_OK;
    }

    return self->real->lpVtbl->BltFast(self->real, dx, dy, real_src, srcRect, flags);
}

static HRESULT WINAPI PSurf_Flip(IDirectDrawSurface4 *iface, IDirectDrawSurface4 *override,
                                  DWORD flags) {
    ProxySurface *self = (ProxySurface *)iface;

    /* Present from the back buffer BEFORE the flip */
    IDirectDrawSurface4 *back = NULL;
    DDSCAPS2 caps;
    memset(&caps, 0, sizeof(caps));
    caps.dwCaps = DDSCAPS_BACKBUFFER;
    if (SUCCEEDED(self->real->lpVtbl->GetAttachedSurface(self->real, &caps, &back))) {
        present_scaled(back);
        back->lpVtbl->Release(back);
    }

    /* Forward the Flip to keep Wine's D3D state consistent */
    return self->real->lpVtbl->Flip(self->real, unwrap_surf(override), flags);
}

/* --- GetAttachedSurface: returns UNWRAPPED Wine surface (critical for D3D) --- */

static HRESULT WINAPI PSurf_GetAttachedSurface(IDirectDrawSurface4 *iface,
                                                 LPDDSCAPS2 caps,
                                                 IDirectDrawSurface4 **out) {
    ProxySurface *self = (ProxySurface *)iface;
    HRESULT hr = self->real->lpVtbl->GetAttachedSurface(self->real, caps, out);
    if (SUCCEEDED(hr))
        hook_log("PSurf_GetAttachedSurface: caps=0x%lx -> %p (unwrapped for D3D)",
                 caps ? caps->dwCaps : 0, *out);
    return hr;
}

/* --- ReleaseDC: present after GDI drawing --- */

static HRESULT WINAPI PSurf_ReleaseDC(IDirectDrawSurface4 *iface, HDC hdc) {
    ProxySurface *self = (ProxySurface *)iface;
    HRESULT hr = self->real->lpVtbl->ReleaseDC(self->real, hdc);
    if (SUCCEEDED(hr)) present_scaled(self->real);
    return hr;
}

/* --- Simple forwarding methods --- */

static HRESULT WINAPI PSurf_AddAttachedSurface(IDirectDrawSurface4 *iface, IDirectDrawSurface4 *a) {
    return ((ProxySurface *)iface)->real->lpVtbl->AddAttachedSurface(
        ((ProxySurface *)iface)->real, unwrap_surf(a));
}
static HRESULT WINAPI PSurf_AddOverlayDirtyRect(IDirectDrawSurface4 *iface, LPRECT r) {
    return ((ProxySurface *)iface)->real->lpVtbl->AddOverlayDirtyRect(
        ((ProxySurface *)iface)->real, r);
}
static HRESULT WINAPI PSurf_BltBatch(IDirectDrawSurface4 *iface, LPDDBLTBATCH b, DWORD c, DWORD d) {
    return ((ProxySurface *)iface)->real->lpVtbl->BltBatch(
        ((ProxySurface *)iface)->real, b, c, d);
}
static HRESULT WINAPI PSurf_DeleteAttachedSurface(IDirectDrawSurface4 *iface, DWORD f, IDirectDrawSurface4 *a) {
    return ((ProxySurface *)iface)->real->lpVtbl->DeleteAttachedSurface(
        ((ProxySurface *)iface)->real, f, unwrap_surf(a));
}
static HRESULT WINAPI PSurf_EnumAttachedSurfaces(IDirectDrawSurface4 *iface, LPVOID ctx, LPDDENUMSURFACESCALLBACK2 cb) {
    return ((ProxySurface *)iface)->real->lpVtbl->EnumAttachedSurfaces(
        ((ProxySurface *)iface)->real, ctx, cb);
}
static HRESULT WINAPI PSurf_EnumOverlayZOrders(IDirectDrawSurface4 *iface, DWORD f, LPVOID ctx, LPDDENUMSURFACESCALLBACK2 cb) {
    return ((ProxySurface *)iface)->real->lpVtbl->EnumOverlayZOrders(
        ((ProxySurface *)iface)->real, f, ctx, cb);
}
static HRESULT WINAPI PSurf_GetBltStatus(IDirectDrawSurface4 *iface, DWORD f) {
    return ((ProxySurface *)iface)->real->lpVtbl->GetBltStatus(
        ((ProxySurface *)iface)->real, f);
}
static HRESULT WINAPI PSurf_GetCaps(IDirectDrawSurface4 *iface, LPDDSCAPS2 c) {
    return ((ProxySurface *)iface)->real->lpVtbl->GetCaps(
        ((ProxySurface *)iface)->real, c);
}
static HRESULT WINAPI PSurf_GetClipper(IDirectDrawSurface4 *iface, LPDIRECTDRAWCLIPPER *c) {
    return ((ProxySurface *)iface)->real->lpVtbl->GetClipper(
        ((ProxySurface *)iface)->real, c);
}
static HRESULT WINAPI PSurf_GetColorKey(IDirectDrawSurface4 *iface, DWORD f, LPDDCOLORKEY k) {
    return ((ProxySurface *)iface)->real->lpVtbl->GetColorKey(
        ((ProxySurface *)iface)->real, f, k);
}
static HRESULT WINAPI PSurf_GetDC(IDirectDrawSurface4 *iface, HDC *h) {
    return ((ProxySurface *)iface)->real->lpVtbl->GetDC(
        ((ProxySurface *)iface)->real, h);
}
static HRESULT WINAPI PSurf_GetFlipStatus(IDirectDrawSurface4 *iface, DWORD f) {
    return ((ProxySurface *)iface)->real->lpVtbl->GetFlipStatus(
        ((ProxySurface *)iface)->real, f);
}
static HRESULT WINAPI PSurf_GetOverlayPosition(IDirectDrawSurface4 *iface, LPLONG x, LPLONG y) {
    return ((ProxySurface *)iface)->real->lpVtbl->GetOverlayPosition(
        ((ProxySurface *)iface)->real, x, y);
}
static HRESULT WINAPI PSurf_GetPalette(IDirectDrawSurface4 *iface, LPDIRECTDRAWPALETTE *p) {
    return ((ProxySurface *)iface)->real->lpVtbl->GetPalette(
        ((ProxySurface *)iface)->real, p);
}
static HRESULT WINAPI PSurf_GetPixelFormat(IDirectDrawSurface4 *iface, LPDDPIXELFORMAT pf) {
    return ((ProxySurface *)iface)->real->lpVtbl->GetPixelFormat(
        ((ProxySurface *)iface)->real, pf);
}
static HRESULT WINAPI PSurf_GetSurfaceDesc(IDirectDrawSurface4 *iface, LPDDSURFACEDESC2 d) {
    return ((ProxySurface *)iface)->real->lpVtbl->GetSurfaceDesc(
        ((ProxySurface *)iface)->real, d);
}
static HRESULT WINAPI PSurf_Initialize(IDirectDrawSurface4 *iface, IDirectDraw *dd, LPDDSURFACEDESC2 d) {
    return ((ProxySurface *)iface)->real->lpVtbl->Initialize(
        ((ProxySurface *)iface)->real, dd, d);
}
static HRESULT WINAPI PSurf_IsLost(IDirectDrawSurface4 *iface) {
    return ((ProxySurface *)iface)->real->lpVtbl->IsLost(
        ((ProxySurface *)iface)->real);
}
static HRESULT WINAPI PSurf_Lock(IDirectDrawSurface4 *iface, LPRECT r, LPDDSURFACEDESC2 d, DWORD f, HANDLE e) {
    return ((ProxySurface *)iface)->real->lpVtbl->Lock(
        ((ProxySurface *)iface)->real, r, d, f, e);
}
static HRESULT WINAPI PSurf_Restore(IDirectDrawSurface4 *iface) {
    return ((ProxySurface *)iface)->real->lpVtbl->Restore(
        ((ProxySurface *)iface)->real);
}
static HRESULT WINAPI PSurf_SetClipper(IDirectDrawSurface4 *iface, LPDIRECTDRAWCLIPPER c) {
    return ((ProxySurface *)iface)->real->lpVtbl->SetClipper(
        ((ProxySurface *)iface)->real, c);
}
static HRESULT WINAPI PSurf_SetColorKey(IDirectDrawSurface4 *iface, DWORD f, LPDDCOLORKEY k) {
    return ((ProxySurface *)iface)->real->lpVtbl->SetColorKey(
        ((ProxySurface *)iface)->real, f, k);
}
static HRESULT WINAPI PSurf_SetOverlayPosition(IDirectDrawSurface4 *iface, LONG x, LONG y) {
    return ((ProxySurface *)iface)->real->lpVtbl->SetOverlayPosition(
        ((ProxySurface *)iface)->real, x, y);
}
static HRESULT WINAPI PSurf_SetPalette(IDirectDrawSurface4 *iface, LPDIRECTDRAWPALETTE p) {
    return ((ProxySurface *)iface)->real->lpVtbl->SetPalette(
        ((ProxySurface *)iface)->real, p);
}
static HRESULT WINAPI PSurf_Unlock(IDirectDrawSurface4 *iface, LPRECT r) {
    return ((ProxySurface *)iface)->real->lpVtbl->Unlock(
        ((ProxySurface *)iface)->real, r);
}
static HRESULT WINAPI PSurf_UpdateOverlay(IDirectDrawSurface4 *iface, LPRECT sr,
                                           IDirectDrawSurface4 *d, LPRECT dr,
                                           DWORD f, LPDDOVERLAYFX fx) {
    return ((ProxySurface *)iface)->real->lpVtbl->UpdateOverlay(
        ((ProxySurface *)iface)->real, sr, unwrap_surf(d), dr, f, fx);
}
static HRESULT WINAPI PSurf_UpdateOverlayDisplay(IDirectDrawSurface4 *iface, DWORD f) {
    return ((ProxySurface *)iface)->real->lpVtbl->UpdateOverlayDisplay(
        ((ProxySurface *)iface)->real, f);
}
static HRESULT WINAPI PSurf_UpdateOverlayZOrder(IDirectDrawSurface4 *iface, DWORD f, IDirectDrawSurface4 *r) {
    return ((ProxySurface *)iface)->real->lpVtbl->UpdateOverlayZOrder(
        ((ProxySurface *)iface)->real, f, unwrap_surf(r));
}
static HRESULT WINAPI PSurf_GetDDInterface(IDirectDrawSurface4 *iface, LPVOID *dd) {
    return ((ProxySurface *)iface)->real->lpVtbl->GetDDInterface(
        ((ProxySurface *)iface)->real, dd);
}
static HRESULT WINAPI PSurf_PageLock(IDirectDrawSurface4 *iface, DWORD f) {
    return ((ProxySurface *)iface)->real->lpVtbl->PageLock(
        ((ProxySurface *)iface)->real, f);
}
static HRESULT WINAPI PSurf_PageUnlock(IDirectDrawSurface4 *iface, DWORD f) {
    return ((ProxySurface *)iface)->real->lpVtbl->PageUnlock(
        ((ProxySurface *)iface)->real, f);
}
static HRESULT WINAPI PSurf_SetSurfaceDesc(IDirectDrawSurface4 *iface, LPDDSURFACEDESC2 d, DWORD f) {
    return ((ProxySurface *)iface)->real->lpVtbl->SetSurfaceDesc(
        ((ProxySurface *)iface)->real, d, f);
}
static HRESULT WINAPI PSurf_SetPrivateData(IDirectDrawSurface4 *iface, REFGUID g, LPVOID d, DWORD sz, DWORD f) {
    return ((ProxySurface *)iface)->real->lpVtbl->SetPrivateData(
        ((ProxySurface *)iface)->real, g, d, sz, f);
}
static HRESULT WINAPI PSurf_GetPrivateData(IDirectDrawSurface4 *iface, REFGUID g, LPVOID d, LPDWORD sz) {
    return ((ProxySurface *)iface)->real->lpVtbl->GetPrivateData(
        ((ProxySurface *)iface)->real, g, d, sz);
}
static HRESULT WINAPI PSurf_FreePrivateData(IDirectDrawSurface4 *iface, REFGUID g) {
    return ((ProxySurface *)iface)->real->lpVtbl->FreePrivateData(
        ((ProxySurface *)iface)->real, g);
}
static HRESULT WINAPI PSurf_GetUniquenessValue(IDirectDrawSurface4 *iface, LPDWORD v) {
    return ((ProxySurface *)iface)->real->lpVtbl->GetUniquenessValue(
        ((ProxySurface *)iface)->real, v);
}
static HRESULT WINAPI PSurf_ChangeUniquenessValue(IDirectDrawSurface4 *iface) {
    return ((ProxySurface *)iface)->real->lpVtbl->ChangeUniquenessValue(
        ((ProxySurface *)iface)->real);
}

/* ================================================================
 * ProxyDD4: IDirectDraw4 wrapper
 *
 * Wraps Wine's real IDirectDraw4. Intercepts SetCooperativeLevel
 * (capture HWND), SetDisplayMode (fullscreen resize), and
 * CreateSurface (wrap primary). All D3D interfaces (IDirect3D3,
 * IDirect3DDevice3, etc.) pass through via QueryInterface.
 * ================================================================ */

static IDirectDraw4Vtbl g_proxy_dd4_vtbl;

struct ProxyDD4 {
    IDirectDraw4Vtbl *lpVtbl;
    IDirectDraw4 *real;
    ULONG refcount;
};

static ProxyDD4 *wrap_dd4(IDirectDraw4 *real) {
    ProxyDD4 *p = (ProxyDD4 *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ProxyDD4));
    if (!p) return NULL;
    p->lpVtbl = &g_proxy_dd4_vtbl;
    p->real = real;
    p->refcount = 1;
    return p;
}

/* --- IUnknown --- */

static HRESULT WINAPI PDD4_QueryInterface(IDirectDraw4 *iface, REFIID riid, void **out) {
    /* Forward to Wine: returns Wine's real IDirect3D3, etc. */
    return ((ProxyDD4 *)iface)->real->lpVtbl->QueryInterface(
        ((ProxyDD4 *)iface)->real, riid, out);
}

static ULONG WINAPI PDD4_AddRef(IDirectDraw4 *iface) {
    return ++((ProxyDD4 *)iface)->refcount;
}

static ULONG WINAPI PDD4_Release(IDirectDraw4 *iface) {
    ProxyDD4 *self = (ProxyDD4 *)iface;
    ULONG ref = --self->refcount;
    if (ref == 0) {
        hook_log("PDD4_Release: freeing wrapped DD4");
        if (self->real) self->real->lpVtbl->Release(self->real);
        HeapFree(GetProcessHeap(), 0, self);
    }
    return ref;
}

/* --- Intercepted: SetCooperativeLevel (capture HWND) --- */

static HRESULT WINAPI PDD4_SetCooperativeLevel(IDirectDraw4 *iface, HWND hwnd, DWORD flags) {
    ProxyDD4 *self = (ProxyDD4 *)iface;
    g_hwnd = hwnd;
    hook_log("SetCooperativeLevel: hwnd=%p flags=0x%lx", hwnd, flags);
    return self->real->lpVtbl->SetCooperativeLevel(self->real, hwnd, flags);
}

/* --- Intercepted: SetDisplayMode (fullscreen resize) --- */

static HRESULT WINAPI PDD4_SetDisplayMode(IDirectDraw4 *iface, DWORD w, DWORD h,
                                            DWORD bpp, DWORD refresh, DWORD flags) {
    ProxyDD4 *self = (ProxyDD4 *)iface;
    HRESULT hr;
    int sw, sh;

    hook_log("SetDisplayMode: %lux%lux%lu", w, h, bpp);

    /* Forward to Wine (CDSEW hook handles the 640x480 fallback) */
    hr = self->real->lpVtbl->SetDisplayMode(self->real, w, h, bpp, refresh, flags);
    if (FAILED(hr)) {
        hook_log("SetDisplayMode: Wine returned 0x%lx, faking success", (unsigned long)hr);
    }

    /* Resize window to fill the entire screen */
    sw = GetSystemMetrics(SM_CXSCREEN);
    sh = GetSystemMetrics(SM_CYSCREEN);
    if (g_hwnd) {
        SetWindowLongPtr(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_hwnd, HWND_TOP, 0, 0, sw, sh,
                     SWP_FRAMECHANGED | SWP_NOZORDER);
        hook_log("SetDisplayMode: window resized to %dx%d", sw, sh);
    }

    return DD_OK;
}

/* --- Intercepted: CreateSurface (wrap primary) --- */

static HRESULT WINAPI PDD4_CreateSurface(IDirectDraw4 *iface, LPDDSURFACEDESC2 desc,
                                           LPDIRECTDRAWSURFACE4 *surf, IUnknown *outer) {
    ProxyDD4 *self = (ProxyDD4 *)iface;
    HRESULT hr;

    hr = self->real->lpVtbl->CreateSurface(self->real, desc, surf, outer);
    if (FAILED(hr)) return hr;

    if (desc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) {
        ProxySurface *proxy = wrap_surface(*surf);
        if (proxy) {
            g_proxy_primary = proxy;
            *surf = (IDirectDrawSurface4 *)proxy;
            hook_log("CreateSurface: wrapped primary %p -> %p", proxy->real, proxy);
        }
    }

    return hr;
}

/* --- Simple forwarding methods --- */

static HRESULT WINAPI PDD4_Compact(IDirectDraw4 *iface) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->Compact(((ProxyDD4 *)iface)->real);
}
static HRESULT WINAPI PDD4_CreateClipper(IDirectDraw4 *iface, DWORD f, LPDIRECTDRAWCLIPPER *c, IUnknown *o) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->CreateClipper(((ProxyDD4 *)iface)->real, f, c, o);
}
static HRESULT WINAPI PDD4_CreatePalette(IDirectDraw4 *iface, DWORD f, LPPALETTEENTRY e, LPDIRECTDRAWPALETTE *p, IUnknown *o) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->CreatePalette(((ProxyDD4 *)iface)->real, f, e, p, o);
}
static HRESULT WINAPI PDD4_DuplicateSurface(IDirectDraw4 *iface, LPDIRECTDRAWSURFACE4 s, LPDIRECTDRAWSURFACE4 *d) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->DuplicateSurface(((ProxyDD4 *)iface)->real, unwrap_surf(s), d);
}
static HRESULT WINAPI PDD4_EnumDisplayModes(IDirectDraw4 *iface, DWORD f, LPDDSURFACEDESC2 d, LPVOID ctx, LPDDENUMMODESCALLBACK2 cb) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->EnumDisplayModes(((ProxyDD4 *)iface)->real, f, d, ctx, cb);
}
static HRESULT WINAPI PDD4_EnumSurfaces(IDirectDraw4 *iface, DWORD f, LPDDSURFACEDESC2 d, LPVOID ctx, LPDDENUMSURFACESCALLBACK2 cb) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->EnumSurfaces(((ProxyDD4 *)iface)->real, f, d, ctx, cb);
}
static HRESULT WINAPI PDD4_FlipToGDISurface(IDirectDraw4 *iface) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->FlipToGDISurface(((ProxyDD4 *)iface)->real);
}
static HRESULT WINAPI PDD4_GetCaps(IDirectDraw4 *iface, LPDDCAPS a, LPDDCAPS b) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->GetCaps(((ProxyDD4 *)iface)->real, a, b);
}
static HRESULT WINAPI PDD4_GetDisplayMode(IDirectDraw4 *iface, LPDDSURFACEDESC2 d) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->GetDisplayMode(((ProxyDD4 *)iface)->real, d);
}
static HRESULT WINAPI PDD4_GetFourCCCodes(IDirectDraw4 *iface, LPDWORD a, LPDWORD b) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->GetFourCCCodes(((ProxyDD4 *)iface)->real, a, b);
}
static HRESULT WINAPI PDD4_GetGDISurface(IDirectDraw4 *iface, LPDIRECTDRAWSURFACE4 *s) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->GetGDISurface(((ProxyDD4 *)iface)->real, s);
}
static HRESULT WINAPI PDD4_GetMonitorFrequency(IDirectDraw4 *iface, LPDWORD f) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->GetMonitorFrequency(((ProxyDD4 *)iface)->real, f);
}
static HRESULT WINAPI PDD4_GetScanLine(IDirectDraw4 *iface, LPDWORD l) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->GetScanLine(((ProxyDD4 *)iface)->real, l);
}
static HRESULT WINAPI PDD4_GetVerticalBlankStatus(IDirectDraw4 *iface, LPBOOL s) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->GetVerticalBlankStatus(((ProxyDD4 *)iface)->real, s);
}
static HRESULT WINAPI PDD4_Initialize(IDirectDraw4 *iface, GUID *g) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->Initialize(((ProxyDD4 *)iface)->real, g);
}
static HRESULT WINAPI PDD4_RestoreDisplayMode(IDirectDraw4 *iface) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->RestoreDisplayMode(((ProxyDD4 *)iface)->real);
}
static HRESULT WINAPI PDD4_WaitForVerticalBlank(IDirectDraw4 *iface, DWORD f, HANDLE h) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->WaitForVerticalBlank(((ProxyDD4 *)iface)->real, f, h);
}
static HRESULT WINAPI PDD4_GetAvailableVidMem(IDirectDraw4 *iface, LPDDSCAPS2 c, LPDWORD t, LPDWORD f) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->GetAvailableVidMem(((ProxyDD4 *)iface)->real, c, t, f);
}
static HRESULT WINAPI PDD4_GetSurfaceFromDC(IDirectDraw4 *iface, HDC h, LPDIRECTDRAWSURFACE4 *s) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->GetSurfaceFromDC(((ProxyDD4 *)iface)->real, h, s);
}
static HRESULT WINAPI PDD4_RestoreAllSurfaces(IDirectDraw4 *iface) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->RestoreAllSurfaces(((ProxyDD4 *)iface)->real);
}
static HRESULT WINAPI PDD4_TestCooperativeLevel(IDirectDraw4 *iface) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->TestCooperativeLevel(((ProxyDD4 *)iface)->real);
}
static HRESULT WINAPI PDD4_GetDeviceIdentifier(IDirectDraw4 *iface, LPDDDEVICEIDENTIFIER d, DWORD f) {
    return ((ProxyDD4 *)iface)->real->lpVtbl->GetDeviceIdentifier(((ProxyDD4 *)iface)->real, d, f);
}

/* ================================================================
 * ChangeDisplaySettingsExW hook
 * Redirects 640x480 to 960x600 when macOS rejects the mode.
 * Same technique as the crossover-wine approach.
 * ================================================================ */

static InlineHook g_cdsew_hook;
typedef LONG (WINAPI *PFN_CDSEW)(LPCWSTR, DEVMODEW *, HWND, DWORD, LPVOID);

static LONG WINAPI Hooked_CDSEW(LPCWSTR dev, DEVMODEW *dm, HWND hwnd,
                                  DWORD flags, LPVOID lp) {
    LONG result;

    pause_hook(&g_cdsew_hook);

    if (!dm) {
        result = ChangeDisplaySettingsExW(dev, dm, hwnd, flags, lp);
        resume_hook(&g_cdsew_hook);
        return result;
    }

    hook_log("CDSEW: %lux%lux%lu",
             (unsigned long)dm->dmPelsWidth,
             (unsigned long)dm->dmPelsHeight,
             (unsigned long)dm->dmBitsPerPel);

    result = ChangeDisplaySettingsExW(dev, dm, hwnd, flags, lp);
    if (result == DISP_CHANGE_SUCCESSFUL) {
        hook_log("  OK");
        resume_hook(&g_cdsew_hook);
        return result;
    }

    hook_log("  Failed(%ld), trying 960x600x32 IN-PLACE", result);
    dm->dmPelsWidth = 960;
    dm->dmPelsHeight = 600;
    dm->dmBitsPerPel = 32;
    dm->dmFields |= DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    result = ChangeDisplaySettingsExW(dev, dm, hwnd, flags, lp);
    if (result == DISP_CHANGE_SUCCESSFUL) {
        hook_log("  960x600 OK (in-place)");
    } else {
        hook_log("  Faking success");
        result = DISP_CHANGE_SUCCESSFUL;
    }

    resume_hook(&g_cdsew_hook);
    return result;
}

/* ================================================================
 * DirectDrawCreateEx hook
 * Intercepts the call, wraps the returned IDirectDraw4 with ProxyDD4.
 * ================================================================ */

static InlineHook g_ddcex_hook;
typedef HRESULT (WINAPI *PFN_DDCEX)(GUID *, void **, REFIID, IUnknown *);

static HRESULT WINAPI Hooked_DirectDrawCreateEx(GUID *guid, void **out,
                                                  REFIID iid, IUnknown *outer) {
    HRESULT hr;

    pause_hook(&g_ddcex_hook);
    hr = ((PFN_DDCEX)g_ddcex_hook.target)(guid, out, iid, outer);
    resume_hook(&g_ddcex_hook);

    if (SUCCEEDED(hr) && guid_eq(iid, &MY_IID_IDirectDraw4)) {
        ProxyDD4 *proxy = wrap_dd4((IDirectDraw4 *)*out);
        if (proxy) {
            *out = proxy;
            hook_log("DirectDrawCreateEx: wrapped DD4 %p -> %p", proxy->real, proxy);
        }
    }

    return hr;
}

/* ================================================================
 * DirectDrawCreate hook
 * The game uses DirectDrawCreate (v1), not DirectDrawCreateEx.
 * We patch the returned IDirectDraw v1 vtable to intercept:
 *   - QueryInterface: wrap IDirectDraw4 with ProxyDD4
 *   - SetCooperativeLevel: capture HWND
 *   - SetDisplayMode: resize window to fullscreen
 * ================================================================ */

static InlineHook g_ddc_hook;
typedef HRESULT (WINAPI *PFN_DDC)(GUID *, IDirectDraw **, IUnknown *);

/* Original vtable function pointers (saved before patching) */
static HRESULT (WINAPI *g_orig_dd1_qi)(IDirectDraw *, REFIID, void **) = NULL;
static HRESULT (WINAPI *g_orig_dd1_scl)(IDirectDraw *, HWND, DWORD) = NULL;
static HRESULT (WINAPI *g_orig_dd1_sdm)(IDirectDraw *, DWORD, DWORD, DWORD) = NULL;

static void patch_vtable_entry(void **vtbl, int index, void *new_func, void **orig_out) {
    DWORD op;
    *orig_out = vtbl[index];
    VirtualProtect(&vtbl[index], sizeof(void *), PAGE_READWRITE, &op);
    vtbl[index] = new_func;
    VirtualProtect(&vtbl[index], sizeof(void *), op, &op);
}

static HRESULT WINAPI Patched_DD1_QueryInterface(IDirectDraw *dd, REFIID riid, void **out) {
    HRESULT hr = g_orig_dd1_qi(dd, riid, out);
    if (SUCCEEDED(hr) && guid_eq(riid, &MY_IID_IDirectDraw4)) {
        ProxyDD4 *proxy = wrap_dd4((IDirectDraw4 *)*out);
        if (proxy) {
            *out = proxy;
            hook_log("DD1_QI -> IDirectDraw4: wrapped %p -> %p", proxy->real, proxy);
        }
    }
    return hr;
}

static HRESULT WINAPI Patched_DD1_SetCooperativeLevel(IDirectDraw *dd, HWND hwnd, DWORD flags) {
    g_hwnd = hwnd;
    hook_log("DD1_SetCooperativeLevel: hwnd=%p flags=0x%lx", hwnd, flags);
    return g_orig_dd1_scl(dd, hwnd, flags);
}

/* IDirectDraw v1 SetDisplayMode has 3 params (not 5 like v4) */
static HRESULT WINAPI Patched_DD1_SetDisplayMode(IDirectDraw *dd, DWORD w, DWORD h, DWORD bpp) {
    HRESULT hr;
    int sw, sh;

    hook_log("DD1_SetDisplayMode: %lux%lux%lu", w, h, bpp);
    hr = g_orig_dd1_sdm(dd, w, h, bpp);
    if (FAILED(hr))
        hook_log("DD1_SetDisplayMode: Wine returned 0x%lx, faking success", (unsigned long)hr);

    sw = GetSystemMetrics(SM_CXSCREEN);
    sh = GetSystemMetrics(SM_CYSCREEN);
    if (g_hwnd) {
        SetWindowLongPtr(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_hwnd, HWND_TOP, 0, 0, sw, sh,
                     SWP_FRAMECHANGED | SWP_NOZORDER);
        hook_log("DD1_SetDisplayMode: window resized to %dx%d", sw, sh);
    }

    return DD_OK;
}

static HRESULT WINAPI Hooked_DirectDrawCreate(GUID *guid, IDirectDraw **out,
                                                IUnknown *outer) {
    HRESULT hr;

    pause_hook(&g_ddc_hook);
    hr = ((PFN_DDC)g_ddc_hook.target)(guid, out, outer);
    resume_hook(&g_ddc_hook);

    if (SUCCEEDED(hr) && *out) {
        /* Patch the IDirectDraw v1 vtable in-place.
         * Vtable indices: 0=QI, 20=SetCooperativeLevel, 21=SetDisplayMode
         * Only patch once: DirectDrawCreate may be called multiple times
         * and Wine shares the vtable, so re-patching would save our own
         * hook as the "original", causing infinite recursion. */
        if (!g_orig_dd1_qi) {
            void **vtbl = *(void ***)(*out);

            patch_vtable_entry(vtbl, 0, Patched_DD1_QueryInterface, (void **)&g_orig_dd1_qi);
            patch_vtable_entry(vtbl, 20, Patched_DD1_SetCooperativeLevel, (void **)&g_orig_dd1_scl);
            patch_vtable_entry(vtbl, 21, Patched_DD1_SetDisplayMode, (void **)&g_orig_dd1_sdm);

            hook_log("DirectDrawCreate: patched DD1 vtable at %p", vtbl);
        } else {
            hook_log("DirectDrawCreate: vtable already patched, skipping");
        }
    }

    return hr;
}

/* ================================================================
 * Vtable initialization
 * ================================================================ */

static void init_vtables(void) {
    /* IDirectDrawSurface4 vtable (45 methods) */
    g_proxy_surf_vtbl.QueryInterface          = PSurf_QueryInterface;
    g_proxy_surf_vtbl.AddRef                  = PSurf_AddRef;
    g_proxy_surf_vtbl.Release                 = PSurf_Release;
    g_proxy_surf_vtbl.AddAttachedSurface      = PSurf_AddAttachedSurface;
    g_proxy_surf_vtbl.AddOverlayDirtyRect     = PSurf_AddOverlayDirtyRect;
    g_proxy_surf_vtbl.Blt                     = PSurf_Blt;
    g_proxy_surf_vtbl.BltBatch                = PSurf_BltBatch;
    g_proxy_surf_vtbl.BltFast                 = PSurf_BltFast;
    g_proxy_surf_vtbl.DeleteAttachedSurface   = PSurf_DeleteAttachedSurface;
    g_proxy_surf_vtbl.EnumAttachedSurfaces    = PSurf_EnumAttachedSurfaces;
    g_proxy_surf_vtbl.EnumOverlayZOrders      = PSurf_EnumOverlayZOrders;
    g_proxy_surf_vtbl.Flip                    = PSurf_Flip;
    g_proxy_surf_vtbl.GetAttachedSurface      = PSurf_GetAttachedSurface;
    g_proxy_surf_vtbl.GetBltStatus            = PSurf_GetBltStatus;
    g_proxy_surf_vtbl.GetCaps                 = PSurf_GetCaps;
    g_proxy_surf_vtbl.GetClipper              = PSurf_GetClipper;
    g_proxy_surf_vtbl.GetColorKey             = PSurf_GetColorKey;
    g_proxy_surf_vtbl.GetDC                   = PSurf_GetDC;
    g_proxy_surf_vtbl.GetFlipStatus           = PSurf_GetFlipStatus;
    g_proxy_surf_vtbl.GetOverlayPosition      = PSurf_GetOverlayPosition;
    g_proxy_surf_vtbl.GetPalette              = PSurf_GetPalette;
    g_proxy_surf_vtbl.GetPixelFormat          = PSurf_GetPixelFormat;
    g_proxy_surf_vtbl.GetSurfaceDesc          = PSurf_GetSurfaceDesc;
    g_proxy_surf_vtbl.Initialize              = PSurf_Initialize;
    g_proxy_surf_vtbl.IsLost                  = PSurf_IsLost;
    g_proxy_surf_vtbl.Lock                    = PSurf_Lock;
    g_proxy_surf_vtbl.ReleaseDC               = PSurf_ReleaseDC;
    g_proxy_surf_vtbl.Restore                 = PSurf_Restore;
    g_proxy_surf_vtbl.SetClipper              = PSurf_SetClipper;
    g_proxy_surf_vtbl.SetColorKey             = PSurf_SetColorKey;
    g_proxy_surf_vtbl.SetOverlayPosition      = PSurf_SetOverlayPosition;
    g_proxy_surf_vtbl.SetPalette              = PSurf_SetPalette;
    g_proxy_surf_vtbl.Unlock                  = PSurf_Unlock;
    g_proxy_surf_vtbl.UpdateOverlay           = PSurf_UpdateOverlay;
    g_proxy_surf_vtbl.UpdateOverlayDisplay    = PSurf_UpdateOverlayDisplay;
    g_proxy_surf_vtbl.UpdateOverlayZOrder     = PSurf_UpdateOverlayZOrder;
    g_proxy_surf_vtbl.GetDDInterface          = PSurf_GetDDInterface;
    g_proxy_surf_vtbl.PageLock                = PSurf_PageLock;
    g_proxy_surf_vtbl.PageUnlock              = PSurf_PageUnlock;
    g_proxy_surf_vtbl.SetSurfaceDesc          = PSurf_SetSurfaceDesc;
    g_proxy_surf_vtbl.SetPrivateData          = PSurf_SetPrivateData;
    g_proxy_surf_vtbl.GetPrivateData          = PSurf_GetPrivateData;
    g_proxy_surf_vtbl.FreePrivateData         = PSurf_FreePrivateData;
    g_proxy_surf_vtbl.GetUniquenessValue      = PSurf_GetUniquenessValue;
    g_proxy_surf_vtbl.ChangeUniquenessValue   = PSurf_ChangeUniquenessValue;

    /* IDirectDraw4 vtable (28 methods) */
    g_proxy_dd4_vtbl.QueryInterface           = PDD4_QueryInterface;
    g_proxy_dd4_vtbl.AddRef                   = PDD4_AddRef;
    g_proxy_dd4_vtbl.Release                  = PDD4_Release;
    g_proxy_dd4_vtbl.Compact                  = PDD4_Compact;
    g_proxy_dd4_vtbl.CreateClipper            = PDD4_CreateClipper;
    g_proxy_dd4_vtbl.CreatePalette            = PDD4_CreatePalette;
    g_proxy_dd4_vtbl.CreateSurface            = PDD4_CreateSurface;
    g_proxy_dd4_vtbl.DuplicateSurface         = PDD4_DuplicateSurface;
    g_proxy_dd4_vtbl.EnumDisplayModes         = PDD4_EnumDisplayModes;
    g_proxy_dd4_vtbl.EnumSurfaces             = PDD4_EnumSurfaces;
    g_proxy_dd4_vtbl.FlipToGDISurface         = PDD4_FlipToGDISurface;
    g_proxy_dd4_vtbl.GetCaps                  = PDD4_GetCaps;
    g_proxy_dd4_vtbl.GetDisplayMode           = PDD4_GetDisplayMode;
    g_proxy_dd4_vtbl.GetFourCCCodes           = PDD4_GetFourCCCodes;
    g_proxy_dd4_vtbl.GetGDISurface            = PDD4_GetGDISurface;
    g_proxy_dd4_vtbl.GetMonitorFrequency      = PDD4_GetMonitorFrequency;
    g_proxy_dd4_vtbl.GetScanLine              = PDD4_GetScanLine;
    g_proxy_dd4_vtbl.GetVerticalBlankStatus   = PDD4_GetVerticalBlankStatus;
    g_proxy_dd4_vtbl.Initialize               = PDD4_Initialize;
    g_proxy_dd4_vtbl.RestoreDisplayMode       = PDD4_RestoreDisplayMode;
    g_proxy_dd4_vtbl.SetCooperativeLevel      = PDD4_SetCooperativeLevel;
    g_proxy_dd4_vtbl.SetDisplayMode           = PDD4_SetDisplayMode;
    g_proxy_dd4_vtbl.WaitForVerticalBlank     = PDD4_WaitForVerticalBlank;
    g_proxy_dd4_vtbl.GetAvailableVidMem       = PDD4_GetAvailableVidMem;
    g_proxy_dd4_vtbl.GetSurfaceFromDC         = PDD4_GetSurfaceFromDC;
    g_proxy_dd4_vtbl.RestoreAllSurfaces       = PDD4_RestoreAllSurfaces;
    g_proxy_dd4_vtbl.TestCooperativeLevel     = PDD4_TestCooperativeLevel;
    g_proxy_dd4_vtbl.GetDeviceIdentifier      = PDD4_GetDeviceIdentifier;
}

/* ================================================================
 * DllMain: initialize hooks on load
 * ================================================================ */

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    (void)hInst; (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        HMODULE user32, ddraw_mod;
        void *cdsew_addr, *ddcex_addr, *ddc_addr;

        DisableThreadLibraryCalls(hInst);
        hook_log("=== fullscreen_proxy loaded ===");

        /* Initialize presentation */
        init_lut();
        init_bmi();
        g_present_buf = (DWORD *)HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, 640 * 480 * sizeof(DWORD));
        init_vtables();

        /* Hook ChangeDisplaySettingsExW */
        user32 = GetModuleHandleA("user32.dll");
        if (!user32) user32 = LoadLibraryA("user32.dll");
        cdsew_addr = (void *)GetProcAddress(user32, "ChangeDisplaySettingsExW");
        if (cdsew_addr) {
            install_inline_hook(&g_cdsew_hook, cdsew_addr, Hooked_CDSEW);
            hook_log("CDSEW hook installed at %p", cdsew_addr);
        }

        /* Hook DirectDrawCreateEx in Wine's loaded ddraw.dll */
        ddraw_mod = GetModuleHandleA("ddraw.dll");
        if (!ddraw_mod) ddraw_mod = LoadLibraryA("ddraw.dll");
        if (ddraw_mod) {
            ddcex_addr = (void *)GetProcAddress(ddraw_mod, "DirectDrawCreateEx");
            if (ddcex_addr) {
                install_inline_hook(&g_ddcex_hook, ddcex_addr, Hooked_DirectDrawCreateEx);
                hook_log("DirectDrawCreateEx hook installed at %p", ddcex_addr);
            } else {
                hook_log("ERROR: DirectDrawCreateEx not found in ddraw.dll");
            }

            /* Hook DirectDrawCreate (game uses v1 API, not Ex) */
            ddc_addr = (void *)GetProcAddress(ddraw_mod, "DirectDrawCreate");
            if (ddc_addr) {
                install_inline_hook(&g_ddc_hook, ddc_addr, Hooked_DirectDrawCreate);
                hook_log("DirectDrawCreate hook installed at %p", ddc_addr);
            } else {
                hook_log("ERROR: DirectDrawCreate not found in ddraw.dll");
            }
        } else {
            hook_log("ERROR: ddraw.dll not loaded");
        }
    }

    if (reason == DLL_PROCESS_DETACH) {
        hook_log("=== fullscreen_proxy unloading ===");
        if (g_logfile) { fclose(g_logfile); g_logfile = NULL; }
        if (g_present_buf) {
            HeapFree(GetProcessHeap(), 0, g_present_buf);
            g_present_buf = NULL;
        }
    }

    return TRUE;
}

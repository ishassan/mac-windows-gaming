/*
 * Display-mode fix for Revenant on Wine (loaded as C:\Revenant\_inmm.dll;
 * _inmm.def sends all other exports to _inmm_real.dll, the GOG DLL).
 *
 * 1. Mode change. The game asks for 640x480x16. A Mac with Apple Silicon has
 *    no 640x480 mode, so the request fails and the game stops with
 *    DDERR_GENERIC. The hook on ChangeDisplaySettingsExW tries the fallback
 *    mode (960x600x32), then the current desktop mode, then reports success.
 *
 * 2. Scaling. After a fallback, the game still draws a 640x480 frame, so the
 *    frame was in the top-left part of the screen. The hooks on the shared
 *    Wine DirectDraw surface vtables (versions 1, 2, 3, 4 and 7) send all
 *    writes to the primary surface (Flip, Blt, BltFast, Lock/Unlock,
 *    GetDC/ReleaseDC) to a 640x480 surface ("the frame"). After each write,
 *    the frame is stretched into the largest 4:3 area at the center of the
 *    primary surface, with black bars at the sides.
 *
 * 3. Mouse. The game reads the cursor position in screen pixels
 *    (GetCursorPos, mouse messages from PeekMessageA) and sets it
 *    (SetCursorPos, ClipCursor). Hooks in the import table of Revenant.exe
 *    map these between the scaled area and the frame. Wine's DirectInput
 *    clips the cursor to the 640x480 window when it takes the mouse; that
 *    clip is mapped to the scaled area too.
 *
 * Settings: C:\Revenant\dispmode_fix.ini, section [dispmode_fix]:
 *   Fallback=960x600       the first fallback mode (default 960x600)
 *   ForceFallback=1        test only: treat the 640x480 request as failed
 *                          (for a test machine that has a 640x480 mode)
 *   Stretch=0              do not scale (the frame stays in the top-left part)
 *   FrameMemory=video      the frame surface in video memory (default: system
 *                          memory, which used less CPU in the test VM)
 *   Debug=1                log mouse button messages and the frames per second
 * The log is C:\Revenant\dispmode_fix.log.
 */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>

#define INI "C:\\Revenant\\dispmode_fix.ini"

static void hook_log(const char *fmt, ...) {
    FILE *f = fopen("C:\\Revenant\\dispmode_fix.log", "a");
    if (f) { va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap); fprintf(f, "\n"); fclose(f); }
}

/* ------------------------------------------------------------ code patches */
/* A 5-byte jump at the start of a function. The hook takes the jump out,
 * calls the function, and puts the jump back. */

typedef struct { unsigned char *target; unsigned char orig[5], patch[5]; } patch_t;

static void patch_write(patch_t *p, const unsigned char *bytes) {
    DWORD op;
    VirtualProtect(p->target, 5, PAGE_EXECUTE_READWRITE, &op);
    memcpy(p->target, bytes, 5);
    VirtualProtect(p->target, 5, op, &op);
    FlushInstructionCache(GetCurrentProcess(), p->target, 5);
}

static int patch_install(patch_t *p, void *target, void *hook) {
    if (!target) return 0;
    p->target = target;
    memcpy(p->orig, target, 5);
    p->patch[0] = 0xE9;
    *(DWORD *)(p->patch + 1) = (DWORD)hook - ((DWORD)target + 5);
    patch_write(p, p->patch);
    return 1;
}

static patch_t g_cdsew, g_ddcreate;

/* -------------------------------------------------------------- settings */

static int g_force_fallback, g_stretch = 1, g_debug;
static DWORD g_frame_caps = DDSCAPS_SYSTEMMEMORY;   /* FrameMemory= system (default), video, any */
static DWORD g_fb_w = 960, g_fb_h = 600;
static int g_scaled;                  /* 1 after a fallback: the frame is smaller than the screen */
static LONG g_mode_w, g_mode_h;       /* the last display mode that was set */
static LONG g_frame_w = 640, g_frame_h = 480;

static void read_settings(void) {
    char buf[64];
    unsigned w, h;
    g_force_fallback = GetPrivateProfileIntA("dispmode_fix", "ForceFallback", 0, INI);
    g_stretch = GetPrivateProfileIntA("dispmode_fix", "Stretch", 1, INI);
    g_debug = GetPrivateProfileIntA("dispmode_fix", "Debug", 0, INI);
    GetPrivateProfileStringA("dispmode_fix", "FrameMemory", "", buf, sizeof(buf), INI);
    if (!lstrcmpiA(buf, "video")) g_frame_caps = DDSCAPS_VIDEOMEMORY;
    else if (!lstrcmpiA(buf, "any")) g_frame_caps = 0;
    GetPrivateProfileStringA("dispmode_fix", "Fallback", "960x600", buf, sizeof(buf), INI);
    if (sscanf(buf, "%ux%u", &w, &h) == 2 && w >= 640 && h >= 480) { g_fb_w = w; g_fb_h = h; }
    hook_log("Settings: Fallback=%lux%lu ForceFallback=%d Stretch=%d",
             (unsigned long)g_fb_w, (unsigned long)g_fb_h, g_force_fallback, g_stretch);
}

/* ------------------------------------------------- ChangeDisplaySettingsExW */

typedef LONG (WINAPI *PFN_CDSEW)(LPCWSTR, DEVMODEW *, HWND, DWORD, LPVOID);

static LONG try_mode(LPCWSTR dev, DEVMODEW *dm, HWND hwnd, DWORD flags, LPVOID lp, DWORD w, DWORD h) {
    /* Change the DEVMODE in place, so that Wine's ddraw sees the new mode */
    dm->dmPelsWidth = w; dm->dmPelsHeight = h; dm->dmBitsPerPel = 32;
    dm->dmFields |= DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    return ((PFN_CDSEW)g_cdsew.target)(dev, dm, hwnd, flags, lp);
}

static LONG WINAPI Hooked_CDSEW(LPCWSTR dev, DEVMODEW *dm, HWND hwnd, DWORD flags, LPVOID lp) {
    LONG result;
    DEVMODEW cur;
    DWORD req_w, req_h;
    patch_write(&g_cdsew, g_cdsew.orig);

    if (!dm) { result = ((PFN_CDSEW)g_cdsew.target)(dev, dm, hwnd, flags, lp); goto rehook; }

    hook_log("CDSEW: %lux%lux%lu", (unsigned long)dm->dmPelsWidth, (unsigned long)dm->dmPelsHeight, (unsigned long)dm->dmBitsPerPel);
    req_w = dm->dmPelsWidth; req_h = dm->dmPelsHeight;
    if (g_force_fallback && req_w == 640 && req_h == 480) {
        hook_log("  ForceFallback: 640x480 treated as failed");
        result = DISP_CHANGE_FAILED;
    } else {
        result = ((PFN_CDSEW)g_cdsew.target)(dev, dm, hwnd, flags, lp);
    }
    if (result == DISP_CHANGE_SUCCESSFUL) { hook_log("  OK"); g_mode_w = dm->dmPelsWidth; g_mode_h = dm->dmPelsHeight; goto rehook; }

    hook_log("  Failed(%ld), trying %lux%lux32 IN-PLACE", result, (unsigned long)g_fb_w, (unsigned long)g_fb_h);
    result = try_mode(dev, dm, hwnd, flags, lp, g_fb_w, g_fb_h);
    if (result == DISP_CHANGE_SUCCESSFUL) {
        hook_log("  %lux%lu OK (in-place)", (unsigned long)g_fb_w, (unsigned long)g_fb_h);
    } else {
        memset(&cur, 0, sizeof(cur)); cur.dmSize = sizeof(cur);
        if (EnumDisplaySettingsW(dev, ENUM_CURRENT_SETTINGS, &cur)) {
            hook_log("  Failed(%ld), trying the current mode %lux%lu", result,
                     (unsigned long)cur.dmPelsWidth, (unsigned long)cur.dmPelsHeight);
            result = try_mode(dev, dm, hwnd, flags, lp, cur.dmPelsWidth, cur.dmPelsHeight);
        }
        if (result == DISP_CHANGE_SUCCESSFUL) hook_log("  current mode OK (in-place)");
        else { hook_log("  Faking success"); result = DISP_CHANGE_SUCCESSFUL; }
    }
    if (result == DISP_CHANGE_SUCCESSFUL) { g_mode_w = dm->dmPelsWidth; g_mode_h = dm->dmPelsHeight; }
    if (req_w < dm->dmPelsWidth || req_h < dm->dmPelsHeight) {
        if (!g_scaled) hook_log("  Scaling on: frame %lux%lu", (unsigned long)req_w, (unsigned long)req_h);
        g_scaled = 1; g_frame_w = req_w; g_frame_h = req_h;
    }
rehook:
    patch_write(&g_cdsew, g_cdsew.patch);
    return result;
}

/* ------------------------------------------------- DirectDraw surface hooks */
/* The surface vtables of Wine's ddraw are shared by all surfaces of one
 * interface version. Indices are the same in versions 1, 2, 3, 4 and 7. */

enum { I_QI = 0, I_RELEASE = 2, I_BLT = 5, I_BLTFAST = 7, I_FLIP = 11, I_GETATTACHED = 12,
       I_GETCAPS = 14, I_GETDC = 17, I_GETSURFACEDESC = 22, I_LOCK = 25, I_RELEASEDC = 26,
       I_RESTORE = 27, I_UNLOCK = 32 };

typedef HRESULT (WINAPI *Blt_t)(void *, RECT *, void *, RECT *, DWORD, DDBLTFX *);
typedef HRESULT (WINAPI *BltFast_t)(void *, DWORD, DWORD, void *, RECT *, DWORD);
typedef HRESULT (WINAPI *Flip_t)(void *, void *, DWORD);
typedef HRESULT (WINAPI *GetAttached_t)(void *, void *, void **);
typedef HRESULT (WINAPI *GetCaps_t)(void *, void *);
typedef HRESULT (WINAPI *GetDC_t)(void *, HDC *);
typedef HRESULT (WINAPI *GetDesc_t)(void *, void *);
typedef HRESULT (WINAPI *Lock_t)(void *, RECT *, void *, DWORD, HANDLE);
typedef HRESULT (WINAPI *ReleaseDC_t)(void *, HDC);
typedef HRESULT (WINAPI *Unlock_t)(void *, void *);
typedef HRESULT (WINAPI *QI_t)(void *, REFIID, void **);
typedef ULONG (WINAPI *Release_t)(void *);
typedef HRESULT (WINAPI *Restore_t)(void *);

#define NV 5
static const char *const v_name[NV] = { "1", "2", "3", "4", "7" };
static const IID *const v_iid[NV] = { &IID_IDirectDrawSurface, &IID_IDirectDrawSurface2,
    &IID_IDirectDrawSurface3, &IID_IDirectDrawSurface4, &IID_IDirectDrawSurface7 };

typedef struct {
    void **vtbl;
    Blt_t blt; BltFast_t bltfast; Flip_t flip; Lock_t lock; Unlock_t unlock;
    GetDC_t getdc; ReleaseDC_t releasedc;
    void *frame;            /* the frame surface in this version */
    void *locked;           /* primary that is locked (Lock sent to the frame) */
    void *dc_owner;         /* primary whose GetDC was sent to the frame */
} ver_t;

static ver_t g_v[NV];
static IDirectDraw *g_dd;           /* the game's DirectDraw object (one reference kept) */
static IDirectDrawSurface4 *g_frame4;
static int g_frame_failed;
static void *g_prim_cache[NV];      /* the last surface found to be the primary, per version */
static void *g_prim_size_for;       /* the primary of g_prim_w/h */
static LONG g_prim_w, g_prim_h;
static void *g_bars_for;            /* the primary that has the bars */
static unsigned g_bars_count;

static int ver_of(void *s) {
    int i;
    if (!s) return -1;
    for (i = 0; i < NV; i++) if (g_v[i].vtbl && *(void ***)s == g_v[i].vtbl) return i;
    return -1;
}

#define VT(s, i) ((*(void ***)(s))[i])

static int is_primary(void *s) {
    DWORD caps[4] = { 0 };
    int v = ver_of(s);
    if (v < 0) return 0;
    if (s == g_v[v].frame) return 0;
    if (((GetCaps_t)VT(s, I_GETCAPS))(s, caps) != DD_OK) return 0;
    if (!(caps[0] & DDSCAPS_PRIMARYSURFACE)) return 0;
    if (g_prim_cache[v] != s) { g_prim_cache[v] = s; hook_log("Primary surface (v%s) %p", v_name[v], s); }
    return 1;
}

static int active(void) { return g_scaled && g_stretch && !g_frame_failed; }

/* Make the frame surface: frame size, the pixel format of the primary. */
static int make_frame(void *prim) {
    IDirectDrawSurface4 *p4 = NULL;
    IDirectDraw4 *dd4 = NULL;
    DDSURFACEDESC2 d;
    HRESULT hr;
    int i;
    if (g_frame4) return 1;
    if (g_frame_failed || !g_dd) return 0;
    if (((QI_t)VT(prim, I_QI))(prim, &IID_IDirectDrawSurface4, (void **)&p4) != DD_OK) goto fail;
    memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
    if (IDirectDrawSurface4_GetSurfaceDesc(p4, &d) != DD_OK) goto fail;
    if (IDirectDraw_QueryInterface(g_dd, &IID_IDirectDraw4, (void **)&dd4) != DD_OK) goto fail;
    d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    d.dwWidth = g_frame_w; d.dwHeight = g_frame_h;
    d.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | g_frame_caps;
    d.ddsCaps.dwCaps2 = d.ddsCaps.dwCaps3 = d.ddsCaps.dwCaps4 = 0;
    hr = IDirectDraw4_CreateSurface(dd4, &d, &g_frame4, NULL);
    if (hr != DD_OK) { hook_log("Frame: CreateSurface failed 0x%08lx", (unsigned long)hr); goto fail; }
    for (i = 0; i < NV; i++) {
        if (IDirectDrawSurface4_QueryInterface(g_frame4, v_iid[i], &g_v[i].frame) != DD_OK) {
            hook_log("Frame: no v%s interface", v_name[i]); goto fail;
        }
    }
    hook_log("Frame: %lux%lu, %lu bits, caps 0x%lx", (unsigned long)g_frame_w, (unsigned long)g_frame_h,
             (unsigned long)d.ddpfPixelFormat.dwRGBBitCount, (unsigned long)d.ddsCaps.dwCaps);
    IDirectDrawSurface4_Release(p4);
    IDirectDraw4_Release(dd4);
    return 1;
fail:
    hook_log("Frame: failed, scaling off");
    g_frame_failed = 1;
    if (p4) IDirectDrawSurface4_Release(p4);
    if (dd4) IDirectDraw4_Release(dd4);
    return 0;
}

static void restore_frame(void) {
    if (g_frame4 && IDirectDrawSurface4_IsLost(g_frame4) != DD_OK) {
        HRESULT hr = IDirectDrawSurface4_Restore(g_frame4);
        hook_log("Frame: restored (0x%08lx)", (unsigned long)hr);
        g_bars_for = NULL;
    }
}

static void scaling_started(void);

/* The part of the primary that is on the screen. Wine 8 keeps the primary
 * at the desktop size (1470x956) after the change to 960x600, and the Mac
 * shows only its top-left 960x600 part. So use the display mode when it is
 * smaller than the primary. */
static void screen_size(LONG *W, LONG *H) {
    *W = g_prim_w; *H = g_prim_h;
    if (g_mode_w > 0 && g_mode_w < *W) *W = g_mode_w;
    if (g_mode_h > 0 && g_mode_h < *H) *H = g_mode_h;
}

/* Debug=1: log the number of frames per second (writes to the primary) */
static void debug_frame(void) {
    static DWORD t0, frames;
    DWORD now;
    if (!g_debug) return;
    now = GetTickCount();
    frames++;
    if (!t0) t0 = now;
    if (now - t0 >= 10000) { hook_log("Frames: %.1f per second", frames * 1000.0 / (now - t0)); t0 = now; frames = 0; }
}
static void fix_clip(void);

/* Stretch the frame into the largest 4:3 area at the center of the primary. */
static void present(int v, void *prim) {
    static int errors;
    RECT src = { 0, 0, g_frame_w, g_frame_h }, dst, bar[2];
    DDBLTFX fx;
    HRESULT hr;
    LONG w, h, x, y, W, H;
    int i, new_bars = prim != g_bars_for || (++g_bars_count & 63) == 0;
    if (new_bars) g_prim_size_for = NULL;   /* read the size again with the bars */
    if (prim != g_prim_size_for) {
        DDSURFACEDESC2 d;
        memset(&d, 0, sizeof(d));
        d.dwSize = (v == 3 || v == 4) ? sizeof(DDSURFACEDESC2) : sizeof(DDSURFACEDESC);
        if (((GetDesc_t)VT(prim, I_GETSURFACEDESC))(prim, &d) != DD_OK) return;
        if (d.dwWidth != (DWORD)g_prim_w || d.dwHeight != (DWORD)g_prim_h) {
            g_prim_w = d.dwWidth; g_prim_h = d.dwHeight;
            hook_log("Present: primary %ldx%ld, display mode %ldx%ld", g_prim_w, g_prim_h, g_mode_w, g_mode_h);
            g_prim_size_for = prim;
            scaling_started();
        }
        g_prim_size_for = prim;
    }
    screen_size(&W, &H);
    if (W * g_frame_h >= H * g_frame_w) { h = H; w = h * g_frame_w / g_frame_h; }
    else { w = W; h = w * g_frame_h / g_frame_w; }
    x = (W - w) / 2; y = (H - h) / 2;
    SetRect(&dst, x, y, x + w, y + h);
    if (x > 0) { SetRect(&bar[0], 0, 0, x, H); SetRect(&bar[1], x + w, 0, W, H); }
    else { SetRect(&bar[0], 0, 0, W, y); SetRect(&bar[1], 0, y + h, W, H); }
    /* In Wine each write to the primary is a present, so the bars are
     * drawn only for a new primary and then at every 64th frame. */
    if (new_bars) {
        g_bars_for = prim;
        memset(&fx, 0, sizeof(fx)); fx.dwSize = sizeof(fx);
        for (i = 0; i < 2; i++)
            if (bar[i].right > bar[i].left && bar[i].bottom > bar[i].top)
                g_v[v].blt(prim, &bar[i], NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
    }
    fix_clip();
    debug_frame();
    hr = g_v[v].blt(prim, &dst, g_v[v].frame, &src, DDBLT_WAIT, NULL);
    if (hr == DDERR_SURFACELOST) { restore_frame(); hr = g_v[v].blt(prim, &dst, g_v[v].frame, &src, DDBLT_WAIT, NULL); }
    if (hr != DD_OK && errors < 10) { errors++; hook_log("Present: Blt failed 0x%08lx", (unsigned long)hr); }
}

#define ONCE(msg, ...) do { static int done; if (!done) { done = 1; hook_log(msg, __VA_ARGS__); } } while (0)

static HRESULT WINAPI Hooked_Flip(void *This, void *target, DWORD flags) {
    int v = ver_of(This);
    void *back = NULL;
    DWORD caps[4] = { DDSCAPS_BACKBUFFER, 0, 0, 0 };
    RECT r = { 0, 0, g_frame_w, g_frame_h };
    HRESULT hr;
    if (v < 0) return DDERR_GENERIC;
    if (!active() || !is_primary(This) || !make_frame(This)) return g_v[v].flip(This, target, flags);
    ONCE("Flip (v%s): sent to the frame", v_name[v]);
    if (target) back = target;
    else if (((GetAttached_t)VT(This, I_GETATTACHED))(This, caps, &back) != DD_OK) return g_v[v].flip(This, target, flags);
    hr = g_v[v].blt(g_v[v].frame, &r, back, &r, DDBLT_WAIT, NULL);
    if (hr == DDERR_SURFACELOST) { restore_frame(); hr = g_v[v].blt(g_v[v].frame, &r, back, &r, DDBLT_WAIT, NULL); }
    if (!target) ((Release_t)VT(back, I_RELEASE))(back);
    if (hr != DD_OK) { ONCE("Flip: copy failed 0x%08lx", (unsigned long)hr); return g_v[v].flip(This, target, flags); }
    present(v, This);
    return DD_OK;
}

/* The game sees a primary surface of the screen size, so a rectangle on the
 * primary can be larger than the frame. Clip it to the frame. When "other"
 * is not NULL, it is the rectangle on the other surface of a Blt: it is cut
 * by the same fraction. Returns 0 when nothing is left. */
static int clip_to_frame(RECT *r, RECT *other) {
    RECT c = *r;
    LONG w = r->right - r->left, h = r->bottom - r->top;
    if (w <= 0 || h <= 0) return 0;
    if (c.left < 0) c.left = 0;
    if (c.top < 0) c.top = 0;
    if (c.right > g_frame_w) c.right = g_frame_w;
    if (c.bottom > g_frame_h) c.bottom = g_frame_h;
    if (c.right <= c.left || c.bottom <= c.top) return 0;
    if (other) {
        LONG ow = other->right - other->left, oh = other->bottom - other->top;
        RECT o = *other;
        o.left = other->left + (c.left - r->left) * ow / w;
        o.right = other->left + (c.right - r->left) * ow / w;
        o.top = other->top + (c.top - r->top) * oh / h;
        o.bottom = other->top + (c.bottom - r->top) * oh / h;
        if (o.right <= o.left || o.bottom <= o.top) return 0;
        *other = o;
    }
    *r = c;
    return 1;
}

static void surface_rect(void *s, RECT *r) {
    DDSURFACEDESC2 d;
    int v = ver_of(s);
    memset(&d, 0, sizeof(d));
    d.dwSize = (v == 3 || v == 4) ? sizeof(DDSURFACEDESC2) : sizeof(DDSURFACEDESC);
    ((GetDesc_t)VT(s, I_GETSURFACEDESC))(s, &d);
    SetRect(r, 0, 0, d.dwWidth, d.dwHeight);
}

static HRESULT WINAPI Hooked_Blt(void *This, RECT *dr, void *src, RECT *sr, DWORD flags, DDBLTFX *fx) {
    int v = ver_of(This), dprim, sprim;
    RECT d, s;
    HRESULT hr;
    if (v < 0) return DDERR_GENERIC;
    if (!active()) {
        if (g_debug && src && is_primary(This)) debug_frame();
        return g_v[v].blt(This, dr, src, sr, flags, fx);
    }
    dprim = is_primary(This);
    sprim = src && is_primary(src);
    if ((!dprim && !sprim) || !make_frame(dprim ? This : src)) return g_v[v].blt(This, dr, src, sr, flags, fx);
    if (dr) d = *dr; else surface_rect(This, &d);
    if (src) { if (sr) s = *sr; else surface_rect(src, &s); }
    ONCE("Blt (v%s) with the primary: sent to the frame (first: dst %ld,%ld-%ld,%ld flags 0x%lx)",
         v_name[v], d.left, d.top, d.right, d.bottom, (unsigned long)flags);
    if (dprim && !clip_to_frame(&d, src ? &s : NULL)) return DD_OK;
    if (sprim && !clip_to_frame(&s, &d)) return DD_OK;
    hr = g_v[v].blt(dprim ? g_v[v].frame : This, &d, sprim ? g_v[v].frame : src, src ? &s : NULL, flags, fx);
    if (hr == DDERR_SURFACELOST) { restore_frame(); hr = g_v[v].blt(dprim ? g_v[v].frame : This, &d, sprim ? g_v[v].frame : src, src ? &s : NULL, flags, fx); }
    if (hr != DD_OK) ONCE("Blt to the frame failed 0x%08lx", (unsigned long)hr);
    if (dprim && hr == DD_OK) present(v, This);
    return hr;
}

static HRESULT WINAPI Hooked_BltFast(void *This, DWORD x, DWORD y, void *src, RECT *sr, DWORD trans) {
    int v = ver_of(This), dprim, sprim;
    RECT d, s;
    HRESULT hr;
    if (v < 0) return DDERR_GENERIC;
    if (!active()) return g_v[v].bltfast(This, x, y, src, sr, trans);
    dprim = is_primary(This);
    sprim = src && is_primary(src);
    if ((!dprim && !sprim) || !make_frame(dprim ? This : src)) return g_v[v].bltfast(This, x, y, src, sr, trans);
    ONCE("BltFast (v%s) with the primary: sent to the frame", v_name[v]);
    if (sr) s = *sr; else surface_rect(src, &s);
    SetRect(&d, x, y, x + s.right - s.left, y + s.bottom - s.top);
    if (dprim && !clip_to_frame(&d, &s)) return DD_OK;
    if (sprim && !clip_to_frame(&s, &d)) return DD_OK;
    hr = g_v[v].bltfast(dprim ? g_v[v].frame : This, d.left, d.top, sprim ? g_v[v].frame : src, &s, trans);
    if (hr != DD_OK) ONCE("BltFast to the frame failed 0x%08lx", (unsigned long)hr);
    if (dprim && hr == DD_OK) present(v, This);
    return hr;
}

static HRESULT WINAPI Hooked_Lock(void *This, RECT *r, void *desc, DWORD flags, HANDLE ev) {
    int v = ver_of(This);
    HRESULT hr;
    if (v < 0) return DDERR_GENERIC;
    if (!active() || !is_primary(This) || !make_frame(This)) return g_v[v].lock(This, r, desc, flags, ev);
    ONCE("Lock (v%s) of the primary: sent to the frame", v_name[v]);
    hr = g_v[v].lock(g_v[v].frame, r, desc, flags, ev);
    if (hr == DDERR_SURFACELOST) { restore_frame(); hr = g_v[v].lock(g_v[v].frame, r, desc, flags, ev); }
    if (hr == DD_OK) g_v[v].locked = This;
    return hr;
}

static HRESULT WINAPI Hooked_Unlock(void *This, void *r) {
    int v = ver_of(This);
    HRESULT hr;
    if (v < 0) return DDERR_GENERIC;
    if (!g_v[v].locked || This != g_v[v].locked) return g_v[v].unlock(This, r);
    g_v[v].locked = NULL;
    hr = g_v[v].unlock(g_v[v].frame, r);
    present(v, This);
    return hr;
}

static HRESULT WINAPI Hooked_GetDC(void *This, HDC *dc) {
    int v = ver_of(This);
    HRESULT hr;
    if (v < 0) return DDERR_GENERIC;
    if (!active() || !is_primary(This) || !make_frame(This)) return g_v[v].getdc(This, dc);
    ONCE("GetDC (v%s) of the primary: sent to the frame", v_name[v]);
    hr = g_v[v].getdc(g_v[v].frame, dc);
    if (hr == DD_OK) g_v[v].dc_owner = This;
    return hr;
}

static HRESULT WINAPI Hooked_ReleaseDC(void *This, HDC dc) {
    int v = ver_of(This);
    HRESULT hr;
    if (v < 0) return DDERR_GENERIC;
    if (!g_v[v].dc_owner || This != g_v[v].dc_owner) return g_v[v].releasedc(This, dc);
    g_v[v].dc_owner = NULL;
    hr = g_v[v].releasedc(g_v[v].frame, dc);
    present(v, This);
    return hr;
}

static void set_entry(void **vtbl, int i, void *hook, void **orig) {
    DWORD op;
    *orig = vtbl[i];
    VirtualProtect(&vtbl[i], sizeof(void *), PAGE_READWRITE, &op);
    vtbl[i] = hook;
    VirtualProtect(&vtbl[i], sizeof(void *), op, &op);
}

/* ------------------------------------------------------------------ mouse */
/* The game reads and sets the cursor position in screen pixels
 * (GetCursorPos, SetCursorPos, ClipCursor) and expects the 640x480 screen.
 * Its window is at 0,0, so its screen pixels are frame pixels. These hooks
 * (in the import table of Revenant.exe only) map between the frame and the
 * scaled area on the screen. */

static BOOL (WINAPI *real_GetCursorPos)(POINT *);
static BOOL (WINAPI *real_SetCursorPos)(int, int);
static BOOL (WINAPI *real_ClipCursor)(const RECT *);

/* The scaled area on the screen; 0 when there is no scaling yet */
static int area(LONG *x, LONG *y, LONG *w, LONG *h) {
    if (!active() || !g_prim_size_for) return 0;
    LONG W, H;
    screen_size(&W, &H);
    if (W * g_frame_h >= H * g_frame_w) { *h = H; *w = *h * g_frame_w / g_frame_h; }
    else { *w = W; *h = *w * g_frame_h / g_frame_w; }
    *x = (W - *w) / 2; *y = (H - *h) / 2;
    return 1;
}

static BOOL WINAPI Hooked_GetCursorPos(POINT *pt) {
    LONG x, y, w, h;
    BOOL ok = real_GetCursorPos(pt);
    if (ok && pt && area(&x, &y, &w, &h)) {
        ONCE("GetCursorPos: mapped (first: %ld,%ld)", pt->x, pt->y);
        pt->x = (pt->x - x) * g_frame_w / w;
        pt->y = (pt->y - y) * g_frame_h / h;
        if (pt->x < 0) pt->x = 0;
        if (pt->y < 0) pt->y = 0;
        if (pt->x >= g_frame_w) pt->x = g_frame_w - 1;
        if (pt->y >= g_frame_h) pt->y = g_frame_h - 1;
    }
    return ok;
}

static BOOL WINAPI Hooked_SetCursorPos(int px, int py) {
    LONG x, y, w, h;
    if (area(&x, &y, &w, &h)) {
        ONCE("SetCursorPos: mapped (first: %d,%d)", px, py);
        px = x + (px * w + w / 2) / g_frame_w;
        py = y + (py * h + h / 2) / g_frame_h;
    }
    return real_SetCursorPos(px, py);
}

static RECT g_clip;                 /* the last clip rectangle of the game (frame pixels) */
static int g_has_clip;

static BOOL clip_now(void) {
    LONG x, y, w, h;
    RECT m;
    if (!g_has_clip) return real_ClipCursor(NULL);
    if (!area(&x, &y, &w, &h)) return real_ClipCursor(&g_clip);
    m.left = x + g_clip.left * w / g_frame_w;
    m.top = y + g_clip.top * h / g_frame_h;
    m.right = x + g_clip.right * w / g_frame_w;
    m.bottom = y + g_clip.bottom * h / g_frame_h;
    return real_ClipCursor(&m);
}

static BOOL WINAPI Hooked_ClipCursor(const RECT *r) {
    static int n;
    if (n < 5) { n++; if (r) hook_log("ClipCursor: %ld,%ld-%ld,%ld", r->left, r->top, r->right, r->bottom); else hook_log("ClipCursor: NULL"); }
    g_has_clip = r != NULL;
    if (r) g_clip = *r;
    return clip_now();
}

/* Wine's DirectInput clips the cursor to the game window when it takes the
 * mouse; at that time the window can be 640x480. A clip that lies inside the
 * frame is in frame pixels: map it to the scaled area. Checked at every 16th
 * present. */
static void fix_clip(void) {
    static unsigned n;
    static RECT done;
    LONG x, y, w, h;
    RECT c, m;
    if ((n++ & 15) || !real_ClipCursor || !area(&x, &y, &w, &h) || !GetClipCursor(&c)) return;
    if (EqualRect(&c, &done)) return;
    if (c.left < 0 || c.top < 0 || c.right > g_frame_w || c.bottom > g_frame_h) return;
    if (c.right - c.left >= w && c.bottom - c.top >= h) return;
    m.left = x + c.left * w / g_frame_w;
    m.top = y + c.top * h / g_frame_h;
    m.right = x + c.right * w / g_frame_w;
    m.bottom = y + c.bottom * h / g_frame_h;
    hook_log("Clip %ld,%ld-%ld,%ld mapped to %ld,%ld-%ld,%ld", c.left, c.top, c.right, c.bottom, m.left, m.top, m.right, m.bottom);
    if (real_ClipCursor(&m)) done = m;
}

/* Called when the scaled area is known: map a clip that the game set before. */
static void scaling_started(void) {
    if (real_ClipCursor && g_has_clip) { hook_log("ClipCursor: clip mapped again"); clip_now(); }
}

static BOOL (WINAPI *real_PeekMessageA)(MSG *, HWND, UINT, UINT, UINT);

static void map_point(LONG *px, LONG *py) {
    LONG x, y, w, h;
    if (!area(&x, &y, &w, &h)) return;
    *px = (*px - x) * g_frame_w / w;
    *py = (*py - y) * g_frame_h / h;
    if (*px < 0) *px = 0;
    if (*py < 0) *py = 0;
    if (*px >= g_frame_w) *px = g_frame_w - 1;
    if (*py >= g_frame_h) *py = g_frame_h - 1;
}

/* Mouse messages carry the position in the client area (the window is at
 * 0,0, so these are screen pixels) and in msg->pt. */
static BOOL WINAPI Hooked_PeekMessageA(MSG *msg, HWND hwnd, UINT first, UINT last, UINT remove) {
    BOOL ok = real_PeekMessageA(msg, hwnd, first, last, remove);
    if (ok && msg && msg->message >= WM_MOUSEFIRST && msg->message <= WM_MOUSELAST
        && msg->message != WM_MOUSEWHEEL && active() && g_prim_size_for) {
        LONG x = (short)LOWORD(msg->lParam), y = (short)HIWORD(msg->lParam);
        LONG x0 = x, y0 = y;
        ONCE("PeekMessageA: mouse message mapped (first: 0x%x at %ld,%ld)", msg->message, x, y);
        map_point(&x, &y);
        if (g_debug && msg->message != WM_MOUSEMOVE) {
            RECT wr; GetWindowRect(msg->hwnd, &wr);
            hook_log("  msg 0x%x hwnd %p (%ld,%ld-%ld,%ld) at %ld,%ld pt %ld,%ld -> %ld,%ld", msg->message, msg->hwnd,
                     wr.left, wr.top, wr.right, wr.bottom, x0, y0, msg->pt.x, msg->pt.y, x, y);
        }
        msg->lParam = MAKELPARAM(x, y);
        map_point(&msg->pt.x, &msg->pt.y);
    }
    return ok;
}

/* Replace one import of the main program (by DLL and function name). */
static void *patch_import(const char *dll, const char *name, void *hook) {
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp;
    void *target = (void *)GetProcAddress(GetModuleHandleA(dll), name);
    if (!dir->VirtualAddress || !target) return NULL;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); imp->Name; imp++) {
        IMAGE_THUNK_DATA *t;
        if (lstrcmpiA((char *)(base + imp->Name), dll)) continue;
        for (t = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk); t->u1.Function; t++) {
            if ((void *)t->u1.Function == target) {
                DWORD op;
                VirtualProtect(&t->u1.Function, sizeof(void *), PAGE_READWRITE, &op);
                t->u1.Function = (DWORD)hook;
                VirtualProtect(&t->u1.Function, sizeof(void *), op, &op);
                return target;
            }
        }
    }
    return NULL;
}

static void hook_mouse(void) {
    real_GetCursorPos = patch_import("user32.dll", "GetCursorPos", Hooked_GetCursorPos);
    real_SetCursorPos = patch_import("user32.dll", "SetCursorPos", Hooked_SetCursorPos);
    real_ClipCursor = patch_import("user32.dll", "ClipCursor", Hooked_ClipCursor);
    real_PeekMessageA = patch_import("user32.dll", "PeekMessageA", Hooked_PeekMessageA);
    hook_log("Mouse hooks: GetCursorPos %s, SetCursorPos %s, ClipCursor %s, PeekMessageA %s",
             real_GetCursorPos ? "yes" : "no", real_SetCursorPos ? "yes" : "no", real_ClipCursor ? "yes" : "no",
             real_PeekMessageA ? "yes" : "no");
}

typedef HRESULT (WINAPI *PFN_DDCREATE)(GUID *, LPDIRECTDRAW *, IUnknown *);

/* Find the surface vtables with a temporary DirectDraw object and surface,
 * then put the hooks into them. */
static void hook_surfaces(void) {
    IDirectDraw *dd = NULL;
    IDirectDrawSurface *s1 = NULL;
    void *s[NV] = { 0 };
    DDSURFACEDESC d;
    HRESULT hr;
    int i, j;
    if (((PFN_DDCREATE)g_ddcreate.target)(NULL, &dd, NULL) != DD_OK) { hook_log("Surface hooks: DirectDrawCreate failed"); return; }
    hr = IDirectDraw_SetCooperativeLevel(dd, GetDesktopWindow(), DDSCL_NORMAL);
    memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
    d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    d.dwWidth = d.dwHeight = 16;
    d.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    if (hr == DD_OK) hr = IDirectDraw_CreateSurface(dd, &d, &s1, NULL);
    if (hr != DD_OK) { hook_log("Surface hooks: temporary surface failed 0x%08lx", (unsigned long)hr); IDirectDraw_Release(dd); return; }
    for (i = 0; i < NV; i++) {
        if (IDirectDrawSurface_QueryInterface(s1, v_iid[i], &s[i]) != DD_OK) continue;
        for (j = 0; j < i; j++) if (g_v[j].vtbl == *(void ***)s[i]) break;
        if (j < i) continue;
        g_v[i].vtbl = *(void ***)s[i];
        set_entry(g_v[i].vtbl, I_BLT, Hooked_Blt, (void **)&g_v[i].blt);
        set_entry(g_v[i].vtbl, I_BLTFAST, Hooked_BltFast, (void **)&g_v[i].bltfast);
        set_entry(g_v[i].vtbl, I_FLIP, Hooked_Flip, (void **)&g_v[i].flip);
        set_entry(g_v[i].vtbl, I_LOCK, Hooked_Lock, (void **)&g_v[i].lock);
        set_entry(g_v[i].vtbl, I_UNLOCK, Hooked_Unlock, (void **)&g_v[i].unlock);
        set_entry(g_v[i].vtbl, I_GETDC, Hooked_GetDC, (void **)&g_v[i].getdc);
        set_entry(g_v[i].vtbl, I_RELEASEDC, Hooked_ReleaseDC, (void **)&g_v[i].releasedc);
        hook_log("Surface hooks: v%s vtable %p", v_name[i], g_v[i].vtbl);
    }
    for (i = 0; i < NV; i++) if (s[i]) ((Release_t)VT(s[i], I_RELEASE))(s[i]);
    IDirectDrawSurface_Release(s1);
    IDirectDraw_Release(dd);
}

static HRESULT WINAPI Hooked_DDCreate(GUID *guid, LPDIRECTDRAW *out, IUnknown *outer) {
    static int surfaces_hooked;
    HRESULT hr;
    patch_write(&g_ddcreate, g_ddcreate.orig);
    hr = ((PFN_DDCREATE)g_ddcreate.target)(guid, out, outer);
    hook_log("DirectDrawCreate: 0x%08lx", (unsigned long)hr);
    if (hr == DD_OK && out && *out) {
        if (g_dd) IDirectDraw_Release(g_dd);
        g_dd = *out; IDirectDraw_AddRef(g_dd);
        if (!surfaces_hooked) { surfaces_hooked = 1; hook_surfaces(); }
    }
    patch_write(&g_ddcreate, g_ddcreate.patch);
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID p) {
    if (r == DLL_PROCESS_ATTACH) {
        HMODULE u, dd;
        DisableThreadLibraryCalls(h);
        hook_log("=== dispmode_fix loaded ===");
        read_settings();
        u = GetModuleHandleA("user32.dll");
        if (!u) u = LoadLibraryA("user32.dll");
        if (patch_install(&g_cdsew, (void *)GetProcAddress(u, "ChangeDisplaySettingsExW"), Hooked_CDSEW))
            hook_log("Hook installed");
        dd = GetModuleHandleA("ddraw.dll");
        if (!dd) dd = LoadLibraryA("ddraw.dll");
        if (g_stretch && dd && patch_install(&g_ddcreate, (void *)GetProcAddress(dd, "DirectDrawCreate"), Hooked_DDCreate))
            hook_log("DirectDrawCreate hook installed");
        if (g_stretch) hook_mouse();
    }
    return TRUE;
}

/*
 * revenant_ddraw.c - DirectDraw proxy for Revenant (1999)
 *
 * Instead of reimplementing DirectDraw and Direct3D, this DLL loads Wine's
 * real 32-bit ddraw from a sidecar ddraw_real.dll (falling back to the Wine
 * system paths) and patches the COM objects in place. Only the methods needed
 * for fullscreen presentation and window state tracking are intercepted; all
 * rendering, surfaces, textures, GetDC, and D3D behaviour stay inside Wine.
 *
 * Build:
 *   i686-w64-mingw32-gcc -shared -o ddraw.dll revenant_ddraw.c ddraw_new.def \
 *       -lgdi32 -luser32 -lkernel32 -lole32 -O2 -Wl,--enable-stdcall-fixup
 */

#include <windows.h>
#include <ddraw.h>
#include <objbase.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================
 * Logging
 * ================================================================ */

static FILE *g_logfile = NULL;

static void rdd_log(const char *fmt, ...)
{
    if (!g_logfile) g_logfile = fopen("revenant_ddraw.log", "a");
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
 * GUIDs (defined manually to avoid INITGUID/linker issues)
 * ================================================================ */

static int guid_eq(REFIID a, const GUID *b)
{
    return memcmp(a, b, sizeof(GUID)) == 0;
}

static const GUID MY_IID_IDirectDraw =
    {0x6C14DB80,0xA733,0x11CE,{0xA5,0x21,0x00,0x20,0xAF,0x0B,0xE5,0x60}};
static const GUID MY_IID_IDirectDraw2 =
    {0xB3A6F3E0,0x2B43,0x11CF,{0xA2,0xDE,0x00,0xAA,0x00,0xB9,0x33,0x56}};
static const GUID MY_IID_IDirectDraw3 =
    {0x618F8AD4,0x8B7A,0x11D0,{0x8F,0xCC,0x00,0xC0,0x4F,0xD9,0x18,0x9D}};
static const GUID MY_IID_IDirectDraw4 =
    {0x9C59509A,0x39BD,0x11D1,{0x8C,0x4A,0x00,0xC0,0x4F,0xD9,0x30,0xC5}};
static const GUID MY_IID_IDirectDrawSurface =
    {0x6C14DB81,0xA733,0x11CE,{0xA5,0x21,0x00,0x20,0xAF,0x0B,0xE5,0x60}};
static const GUID MY_IID_IDirectDrawSurface2 =
    {0x57805885,0x6EEC,0x11CF,{0x94,0x41,0xA8,0x23,0x03,0xC1,0x0E,0x27}};
static const GUID MY_IID_IDirectDrawSurface3 =
    {0xDA044E00,0x69B2,0x11D0,{0xA1,0xD5,0x00,0xAA,0x00,0xB8,0xDF,0xBB}};
static const GUID MY_IID_IDirectDrawSurface4 =
    {0x0B2B8630,0xAD35,0x11D0,{0x8E,0xA6,0x00,0x60,0x97,0x97,0xEA,0x5B}};

/* ================================================================
 * Real ddraw exports
 * ================================================================ */

typedef HRESULT (WINAPI *PFN_DirectDrawCreate)(GUID *, LPDIRECTDRAW *, IUnknown *);
typedef HRESULT (WINAPI *PFN_DirectDrawCreateEx)(GUID *, void **, REFIID, IUnknown *);
typedef HRESULT (WINAPI *PFN_DirectDrawCreateClipper)(DWORD, LPDIRECTDRAWCLIPPER *, IUnknown *);
typedef HRESULT (WINAPI *PFN_DirectDrawEnumerateA)(LPDDENUMCALLBACKA, void *);
typedef HRESULT (WINAPI *PFN_DirectDrawEnumerateExA)(LPDDENUMCALLBACKEXA, void *, DWORD);
typedef HRESULT (WINAPI *PFN_DirectDrawEnumerateW)(LPDDENUMCALLBACKW, void *);
typedef HRESULT (WINAPI *PFN_DirectDrawEnumerateExW)(LPDDENUMCALLBACKEXW, void *, DWORD);

static HMODULE g_real_ddraw = NULL;
static HMODULE g_self_module = NULL;
static PFN_DirectDrawCreate g_real_DirectDrawCreate = NULL;
static PFN_DirectDrawCreateEx g_real_DirectDrawCreateEx = NULL;
static PFN_DirectDrawCreateClipper g_real_DirectDrawCreateClipper = NULL;
static PFN_DirectDrawEnumerateA g_real_DirectDrawEnumerateA = NULL;
static PFN_DirectDrawEnumerateExA g_real_DirectDrawEnumerateExA = NULL;
static PFN_DirectDrawEnumerateW g_real_DirectDrawEnumerateW = NULL;
static PFN_DirectDrawEnumerateExW g_real_DirectDrawEnumerateExW = NULL;

/* ================================================================
 * Global fullscreen state
 * ================================================================ */

static HWND g_hwnd = NULL;
static RECT g_orig_rect = {0};
static BOOL g_is_fullscreen = FALSE;
static DWORD *g_present_buf = NULL;
static DWORD g_rgb565_lut[65536];
static BITMAPINFO g_bmi;
static int g_surface_proxy_install_logs = 0;
static int g_surface_proxy_release_logs = 0;
static int g_present_log_count = 0;

/* ================================================================
 * Proxy registry
 * ================================================================ */

typedef enum {
    PROXY_DD1,
    PROXY_DD2,
    PROXY_DD3,
    PROXY_DD4,
    PROXY_SURF1,
    PROXY_SURF2,
    PROXY_SURF3,
    PROXY_SURF4
} ProxyKind;

typedef struct ProxyEntry ProxyEntry;
struct ProxyEntry {
    void *iface;
    ProxyKind kind;
    void **orig_vtbl;
    void **proxy_vtbl;
    ProxyEntry *next;
};

typedef struct GenericIface {
    void **lpVtbl;
} GenericIface;

static CRITICAL_SECTION g_proxy_lock;
static BOOL g_proxy_lock_ready = FALSE;
static ProxyEntry *g_proxy_list = NULL;

/* ================================================================
 * Helpers
 * ================================================================ */

static const char *proxy_kind_name(ProxyKind kind)
{
    switch (kind) {
    case PROXY_DD1: return "DD1";
    case PROXY_DD2: return "DD2";
    case PROXY_DD3: return "DD3";
    case PROXY_DD4: return "DD4";
    case PROXY_SURF1: return "SURF1";
    case PROXY_SURF2: return "SURF2";
    case PROXY_SURF3: return "SURF3";
    case PROXY_SURF4: return "SURF4";
    default: return "UNKNOWN";
    }
}

static size_t proxy_vtbl_count(ProxyKind kind)
{
    switch (kind) {
    case PROXY_DD1: return 23;
    case PROXY_DD2: return 24;
    case PROXY_DD3: return 25;
    case PROXY_DD4: return 28;
    case PROXY_SURF1: return 36;
    case PROXY_SURF2: return 39;
    case PROXY_SURF3: return 40;
    case PROXY_SURF4: return 45;
    default: return 0;
    }
}

static BOOL is_surface_kind(ProxyKind kind)
{
    return kind == PROXY_SURF1 || kind == PROXY_SURF2 ||
           kind == PROXY_SURF3 || kind == PROXY_SURF4;
}

static BOOL dd_kind_from_iid(REFIID riid, ProxyKind *kind)
{
    if (guid_eq(riid, &MY_IID_IDirectDraw)) { *kind = PROXY_DD1; return TRUE; }
    if (guid_eq(riid, &MY_IID_IDirectDraw2)) { *kind = PROXY_DD2; return TRUE; }
    if (guid_eq(riid, &MY_IID_IDirectDraw3)) { *kind = PROXY_DD3; return TRUE; }
    if (guid_eq(riid, &MY_IID_IDirectDraw4)) { *kind = PROXY_DD4; return TRUE; }
    return FALSE;
}

static BOOL surface_kind_from_iid(REFIID riid, ProxyKind *kind)
{
    if (guid_eq(riid, &MY_IID_IDirectDrawSurface)) { *kind = PROXY_SURF1; return TRUE; }
    if (guid_eq(riid, &MY_IID_IDirectDrawSurface2)) { *kind = PROXY_SURF2; return TRUE; }
    if (guid_eq(riid, &MY_IID_IDirectDrawSurface3)) { *kind = PROXY_SURF3; return TRUE; }
    if (guid_eq(riid, &MY_IID_IDirectDrawSurface4)) { *kind = PROXY_SURF4; return TRUE; }
    return FALSE;
}

static ProxyEntry *find_proxy_locked(void *iface)
{
    ProxyEntry *entry;
    for (entry = g_proxy_list; entry; entry = entry->next)
        if (entry->iface == iface) return entry;
    return NULL;
}

static ProxyEntry *find_proxy(void *iface)
{
    ProxyEntry *entry;
    if (!g_proxy_lock_ready) return NULL;
    EnterCriticalSection(&g_proxy_lock);
    entry = find_proxy_locked(iface);
    LeaveCriticalSection(&g_proxy_lock);
    return entry;
}

static void remove_proxy(void *iface)
{
    ProxyEntry **pp;
    if (!g_proxy_lock_ready) return;
    EnterCriticalSection(&g_proxy_lock);
    pp = &g_proxy_list;
    while (*pp) {
        ProxyEntry *entry = *pp;
        if (entry->iface == iface) {
            *pp = entry->next;
            if (entry->proxy_vtbl) free(entry->proxy_vtbl);
            free(entry);
            break;
        }
        pp = &entry->next;
    }
    LeaveCriticalSection(&g_proxy_lock);
}

static void init_lut(void)
{
    int i;
    for (i = 0; i < 65536; ++i) {
        DWORD r5 = (i >> 11) & 0x1F;
        DWORD g6 = (i >> 5) & 0x3F;
        DWORD b5 = i & 0x1F;
        DWORD r8 = (r5 << 3) | (r5 >> 2);
        DWORD g8 = (g6 << 2) | (g6 >> 4);
        DWORD b8 = (b5 << 3) | (b5 >> 2);
        g_rgb565_lut[i] = (r8 << 16) | (g8 << 8) | b8;
    }
}

static void init_bmi(void)
{
    memset(&g_bmi, 0, sizeof(g_bmi));
    g_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bmi.bmiHeader.biWidth = 640;
    g_bmi.bmiHeader.biHeight = -480;
    g_bmi.bmiHeader.biPlanes = 1;
    g_bmi.bmiHeader.biBitCount = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;
}

static void ensure_present_state(void)
{
    if (!g_present_buf) g_present_buf = (DWORD *)calloc(640 * 480, sizeof(DWORD));
}

static FARPROC resolve_export(const char *name)
{
    FARPROC proc = NULL;
    if (!g_real_ddraw) return NULL;
    proc = GetProcAddress(g_real_ddraw, name);
    if (!proc) rdd_log("resolve_export failed: %s", name);
    return proc;
}

#define ASSIGN_EXPORT(var, name) \
    do { \
        union { FARPROC raw; __typeof__(var) typed; } export_cast; \
        export_cast.raw = resolve_export(name); \
        (var) = export_cast.typed; \
    } while (0)

static BOOL load_real_ddraw(void)
{
    char path[MAX_PATH];
    char module_path[MAX_PATH];
    char *slash;
    if (g_real_ddraw) return TRUE;

    memset(path, 0, sizeof(path));
    memset(module_path, 0, sizeof(module_path));

    if (g_self_module &&
        GetModuleFileNameA(g_self_module, module_path, sizeof(module_path)) > 0) {
        slash = strrchr(module_path, '\\');
        if (slash) {
            *(slash + 1) = '\0';
            snprintf(path, sizeof(path), "%sddraw_real.dll", module_path);
            g_real_ddraw = LoadLibraryA(path);
            if (!g_real_ddraw)
                rdd_log("LoadLibraryA failed for %s: %lu", path, GetLastError());
        }
    }

    if (!g_real_ddraw) {
        strcpy(path, "C:\\windows\\syswow64\\ddraw.dll");
        g_real_ddraw = LoadLibraryA(path);
        if (!g_real_ddraw)
            rdd_log("LoadLibraryA failed for %s: %lu", path, GetLastError());
    }

    if (!g_real_ddraw) {
        UINT len = GetSystemDirectoryA(module_path, sizeof(module_path));
        if (!len || len >= sizeof(module_path) - 12) {
            rdd_log("GetSystemDirectoryA failed: %lu", GetLastError());
            return FALSE;
        }
        snprintf(path, sizeof(path), "%s\\ddraw.dll", module_path);
        g_real_ddraw = LoadLibraryA(path);
        if (!g_real_ddraw)
            rdd_log("LoadLibraryA failed for %s: %lu", path, GetLastError());
    }

    if (!g_real_ddraw) {
        rdd_log("LoadLibraryA failed for real ddraw");
        return FALSE;
    }

    ASSIGN_EXPORT(g_real_DirectDrawCreate, "DirectDrawCreate");
    ASSIGN_EXPORT(g_real_DirectDrawCreateEx, "DirectDrawCreateEx");
    ASSIGN_EXPORT(g_real_DirectDrawCreateClipper, "DirectDrawCreateClipper");
    ASSIGN_EXPORT(g_real_DirectDrawEnumerateA, "DirectDrawEnumerateA");
    ASSIGN_EXPORT(g_real_DirectDrawEnumerateExA, "DirectDrawEnumerateExA");
    ASSIGN_EXPORT(g_real_DirectDrawEnumerateW, "DirectDrawEnumerateW");
    ASSIGN_EXPORT(g_real_DirectDrawEnumerateExW, "DirectDrawEnumerateExW");

    if (!g_real_DirectDrawCreate || !g_real_DirectDrawCreateEx ||
        !g_real_DirectDrawCreateClipper || !g_real_DirectDrawEnumerateA ||
        !g_real_DirectDrawEnumerateExA || !g_real_DirectDrawEnumerateW ||
        !g_real_DirectDrawEnumerateExW) {
        return FALSE;
    }

    rdd_log("Loaded real ddraw successfully");
    return TRUE;
}

static void restore_window(void)
{
    if (!g_is_fullscreen || !g_hwnd || !IsWindow(g_hwnd)) return;

    g_is_fullscreen = FALSE;
    SetWindowPos(g_hwnd, HWND_NOTOPMOST,
                 g_orig_rect.left, g_orig_rect.top,
                 g_orig_rect.right - g_orig_rect.left,
                 g_orig_rect.bottom - g_orig_rect.top,
                 SWP_SHOWWINDOW | SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS);
    rdd_log("restore_window");
}

static void setup_fullscreen_window(void)
{
    int screen_w, screen_h;

    if (!g_hwnd || !IsWindow(g_hwnd)) return;

    if (!g_is_fullscreen)
        GetWindowRect(g_hwnd, &g_orig_rect);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);

    SetWindowPos(g_hwnd, HWND_TOP, 0, 0, screen_w, screen_h,
                 SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS);
    g_is_fullscreen = TRUE;
    rdd_log("setup_fullscreen_window: %dx%d", screen_w, screen_h);
}

static int mask_shift(DWORD mask)
{
    int shift = 0;
    if (!mask) return 0;
    while ((mask & 1u) == 0u) {
        mask >>= 1;
        ++shift;
    }
    return shift;
}

static BYTE expand_component(DWORD pixel, DWORD mask)
{
    DWORD raw, max_value;
    int shift;

    if (!mask) return 0;
    shift = mask_shift(mask);
    raw = (pixel & mask) >> shift;
    max_value = mask >> shift;
    if (!max_value) return 0;
    return (BYTE)((raw * 255u + max_value / 2u) / max_value);
}

static DWORD load_pixel(const BYTE *src, DWORD bpp)
{
    switch (bpp) {
    case 16:
        return *(const WORD *)src;
    case 24:
        return (DWORD)src[0] | ((DWORD)src[1] << 8) | ((DWORD)src[2] << 16);
    case 32:
        return *(const DWORD *)src;
    default:
        return 0;
    }
}

static void present_buffer(void)
{
    RECT rc;
    HDC hdc;
    int win_w, win_h, dest_w, dest_h, dest_x, dest_y;

    if (!g_hwnd || !g_present_buf || !IsWindow(g_hwnd)) return;

    GetClientRect(g_hwnd, &rc);
    win_w = rc.right;
    win_h = rc.bottom;
    if (win_w <= 0 || win_h <= 0) return;

    hdc = GetDC(g_hwnd);
    if (!hdc) return;

    if (win_w * 3 > win_h * 4) {
        dest_h = win_h;
        dest_w = win_h * 4 / 3;
        dest_x = (win_w - dest_w) / 2;
        dest_y = 0;
    } else {
        dest_w = win_w;
        dest_h = win_w * 3 / 4;
        dest_x = 0;
        dest_y = (win_h - dest_h) / 2;
    }

    if (dest_x > 0 || dest_y > 0) {
        HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
        if (dest_x > 0) {
            RECT left_bar = {0, 0, dest_x, win_h};
            RECT right_bar = {dest_x + dest_w, 0, win_w, win_h};
            FillRect(hdc, &left_bar, black);
            FillRect(hdc, &right_bar, black);
        }
        if (dest_y > 0) {
            RECT top_bar = {0, 0, win_w, dest_y};
            RECT bottom_bar = {0, dest_y + dest_h, win_w, win_h};
            FillRect(hdc, &top_bar, black);
            FillRect(hdc, &bottom_bar, black);
        }
    }

    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, NULL);
    StretchDIBits(hdc,
        dest_x, dest_y, dest_w, dest_h,
        0, 0, 640, 480,
        g_present_buf, &g_bmi, DIB_RGB_COLORS, SRCCOPY);

    ReleaseDC(g_hwnd, hdc);
}

typedef struct SurfaceSnapshot {
    BYTE *bits;
    LONG pitch;
    DWORD width;
    DWORD height;
    DDPIXELFORMAT pf;
    ProxyKind kind;
    void *iface;
} SurfaceSnapshot;

static BOOL surface_is_primary(void *iface, ProxyKind kind)
{
    HRESULT hr;
    if (kind == PROXY_SURF4) {
        DDSCAPS2 caps;
        memset(&caps, 0, sizeof(caps));
        hr = IDirectDrawSurface4_GetCaps((LPDIRECTDRAWSURFACE4)iface, &caps);
        return SUCCEEDED(hr) && (caps.dwCaps & DDSCAPS_PRIMARYSURFACE);
    }

    if (kind == PROXY_SURF1 || kind == PROXY_SURF2 || kind == PROXY_SURF3) {
        DDSCAPS caps;
        memset(&caps, 0, sizeof(caps));
        if (kind == PROXY_SURF1)
            hr = IDirectDrawSurface_GetCaps((LPDIRECTDRAWSURFACE)iface, &caps);
        else if (kind == PROXY_SURF2)
            hr = IDirectDrawSurface2_GetCaps((LPDIRECTDRAWSURFACE2)iface, &caps);
        else
            hr = IDirectDrawSurface3_GetCaps((LPDIRECTDRAWSURFACE3)iface, &caps);
        return SUCCEEDED(hr) && (caps.dwCaps & DDSCAPS_PRIMARYSURFACE);
    }

    return FALSE;
}

static HRESULT lock_surface_snapshot(void *iface, ProxyKind kind, SurfaceSnapshot *snap)
{
    HRESULT hr;
    memset(snap, 0, sizeof(*snap));
    snap->kind = kind;
    snap->iface = iface;

    if (kind == PROXY_SURF4) {
        DDSURFACEDESC2 desc;
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        hr = IDirectDrawSurface4_Lock((LPDIRECTDRAWSURFACE4)iface, NULL, &desc,
                                      DDLOCK_WAIT | DDLOCK_READONLY, NULL);
        if (FAILED(hr)) return hr;
        snap->bits = (BYTE *)desc.lpSurface;
        snap->pitch = desc.lPitch;
        snap->width = desc.dwWidth;
        snap->height = desc.dwHeight;
        snap->pf = desc.ddpfPixelFormat;
        return DD_OK;
    }

    {
        DDSURFACEDESC desc;
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);

        if (kind == PROXY_SURF1)
            hr = IDirectDrawSurface_Lock((LPDIRECTDRAWSURFACE)iface, NULL, &desc,
                                         DDLOCK_WAIT | DDLOCK_READONLY, NULL);
        else if (kind == PROXY_SURF2)
            hr = IDirectDrawSurface2_Lock((LPDIRECTDRAWSURFACE2)iface, NULL, &desc,
                                          DDLOCK_WAIT | DDLOCK_READONLY, NULL);
        else
            hr = IDirectDrawSurface3_Lock((LPDIRECTDRAWSURFACE3)iface, NULL, &desc,
                                          DDLOCK_WAIT | DDLOCK_READONLY, NULL);
        if (FAILED(hr)) return hr;

        snap->bits = (BYTE *)desc.lpSurface;
        snap->pitch = desc.lPitch;
        snap->width = desc.dwWidth;
        snap->height = desc.dwHeight;
        snap->pf = desc.ddpfPixelFormat;
        return DD_OK;
    }
}

static void unlock_surface_snapshot(const SurfaceSnapshot *snap)
{
    if (!snap->iface) return;
    if (snap->kind == PROXY_SURF4) {
        IDirectDrawSurface4_Unlock((LPDIRECTDRAWSURFACE4)snap->iface, NULL);
    } else if (snap->kind == PROXY_SURF1) {
        IDirectDrawSurface_Unlock((LPDIRECTDRAWSURFACE)snap->iface, snap->bits);
    } else if (snap->kind == PROXY_SURF2) {
        IDirectDrawSurface2_Unlock((LPDIRECTDRAWSURFACE2)snap->iface, snap->bits);
    } else if (snap->kind == PROXY_SURF3) {
        IDirectDrawSurface3_Unlock((LPDIRECTDRAWSURFACE3)snap->iface, snap->bits);
    }
}

static BOOL snapshot_to_present_buffer(const SurfaceSnapshot *snap)
{
    DWORD width, height, bpp;
    BYTE *base;
    LONG pitch;
    DWORD y;

    if (!snap->bits || !snap->pf.dwRGBBitCount) return FALSE;
    if (!(snap->pf.dwFlags & DDPF_RGB) && snap->pf.dwRGBBitCount != 16) return FALSE;

    ensure_present_state();
    if (!g_present_buf) return FALSE;

    memset(g_present_buf, 0, 640 * 480 * sizeof(DWORD));

    width = snap->width ? snap->width : 640;
    height = snap->height ? snap->height : 480;
    if (width > 640) width = 640;
    if (height > 480) height = 480;

    bpp = snap->pf.dwRGBBitCount;
    pitch = snap->pitch;
    base = snap->bits;
    if (pitch < 0)
        base = snap->bits + (height - 1) * (size_t)(-pitch);

    for (y = 0; y < height; ++y) {
        const BYTE *src_row = base + y * pitch;
        DWORD *dst_row = g_present_buf + y * 640;
        DWORD x;

        if (bpp == 16 &&
            snap->pf.dwRBitMask == 0xF800 &&
            snap->pf.dwGBitMask == 0x07E0 &&
            snap->pf.dwBBitMask == 0x001F) {
            const WORD *src16 = (const WORD *)src_row;
            for (x = 0; x < width; ++x)
                dst_row[x] = g_rgb565_lut[src16[x]];
            continue;
        }

        if (bpp != 16 && bpp != 24 && bpp != 32) return FALSE;

        for (x = 0; x < width; ++x) {
            DWORD pixel = load_pixel(src_row + x * (bpp / 8), bpp);
            BYTE r = expand_component(pixel, snap->pf.dwRBitMask);
            BYTE g = expand_component(pixel, snap->pf.dwGBitMask);
            BYTE b = expand_component(pixel, snap->pf.dwBBitMask);
            dst_row[x] = (r << 16) | (g << 8) | b;
        }
    }

    return TRUE;
}

static void log_present_event(const char *trigger, const char *path,
                              const SurfaceSnapshot *snap, DWORD caps, HRESULT hr)
{
    if (g_present_log_count >= 16) return;
    ++g_present_log_count;

    if (snap) {
        rdd_log("present[%d]: trigger=%s path=%s caps=0x%lx size=%lux%lu pitch=%ld bpp=%lu flags=0x%lx hr=0x%08lx",
                g_present_log_count, trigger, path, (unsigned long)caps,
                (unsigned long)snap->width, (unsigned long)snap->height,
                snap->pitch, (unsigned long)snap->pf.dwRGBBitCount,
                (unsigned long)snap->pf.dwFlags, (unsigned long)hr);
    } else {
        rdd_log("present[%d]: trigger=%s path=%s hr=0x%08lx",
                g_present_log_count, trigger, path, (unsigned long)hr);
    }
}

static DWORD get_surface_caps_value(void *iface, ProxyKind kind)
{
    HRESULT hr;

    if (kind == PROXY_SURF4) {
        DDSCAPS2 caps;
        memset(&caps, 0, sizeof(caps));
        hr = IDirectDrawSurface4_GetCaps((LPDIRECTDRAWSURFACE4)iface, &caps);
        return SUCCEEDED(hr) ? caps.dwCaps : 0;
    }

    if (kind == PROXY_SURF1 || kind == PROXY_SURF2 || kind == PROXY_SURF3) {
        DDSCAPS caps;
        memset(&caps, 0, sizeof(caps));
        if (kind == PROXY_SURF1)
            hr = IDirectDrawSurface_GetCaps((LPDIRECTDRAWSURFACE)iface, &caps);
        else if (kind == PROXY_SURF2)
            hr = IDirectDrawSurface2_GetCaps((LPDIRECTDRAWSURFACE2)iface, &caps);
        else
            hr = IDirectDrawSurface3_GetCaps((LPDIRECTDRAWSURFACE3)iface, &caps);
        return SUCCEEDED(hr) ? caps.dwCaps : 0;
    }

    return 0;
}

static BOOL capture_window_client_to_present_buffer(void)
{
    HDC window_dc = NULL, mem_dc = NULL;
    HBITMAP dib = NULL;
    void *bits = NULL;
    BITMAPINFO bmi;
    RECT rc;
    int copy_w, copy_h;

    if (!g_hwnd || !IsWindow(g_hwnd)) return FALSE;

    ensure_present_state();
    if (!g_present_buf) return FALSE;
    memset(g_present_buf, 0, 640 * 480 * sizeof(DWORD));

    GetClientRect(g_hwnd, &rc);
    copy_w = rc.right;
    copy_h = rc.bottom;
    if (copy_w > 640) copy_w = 640;
    if (copy_h > 480) copy_h = 480;
    if (copy_w <= 0 || copy_h <= 0) return FALSE;

    window_dc = GetDC(g_hwnd);
    if (!window_dc) return FALSE;

    mem_dc = CreateCompatibleDC(window_dc);
    if (!mem_dc) goto done;

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = 640;
    bmi.bmiHeader.biHeight = -480;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    dib = CreateDIBSection(window_dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dib || !bits) goto done;
    memset(bits, 0, 640 * 480 * sizeof(DWORD));

    SelectObject(mem_dc, dib);
    if (!BitBlt(mem_dc, 0, 0, copy_w, copy_h, window_dc, 0, 0, SRCCOPY))
        goto done;

    memcpy(g_present_buf, bits, 640 * 480 * sizeof(DWORD));

    DeleteObject(dib);
    DeleteDC(mem_dc);
    ReleaseDC(g_hwnd, window_dc);
    return TRUE;

done:
    if (dib) DeleteObject(dib);
    if (mem_dc) DeleteDC(mem_dc);
    if (window_dc) ReleaseDC(g_hwnd, window_dc);
    return FALSE;
}

static BOOL present_surface(void *iface, ProxyKind kind, const char *trigger)
{
    SurfaceSnapshot snap;
    HRESULT hr;
    DWORD caps;

    hr = lock_surface_snapshot(iface, kind, &snap);
    if (FAILED(hr)) {
        log_present_event(trigger, "primary-lock-failed", NULL, 0, hr);
        return FALSE;
    }

    caps = get_surface_caps_value(iface, kind);
    if (!snapshot_to_present_buffer(&snap)) {
        log_present_event(trigger, "primary-unsupported", &snap, caps, DDERR_UNSUPPORTED);
        unlock_surface_snapshot(&snap);
        return FALSE;
    }

    log_present_event(trigger, "primary-lock", &snap, caps, DD_OK);
    unlock_surface_snapshot(&snap);
    present_buffer();
    return TRUE;
}

static void maybe_present_primary(void *self, ProxyKind kind, const char *trigger)
{
    if (!g_hwnd || !surface_is_primary(self, kind)) return;

    if (present_surface(self, kind, trigger)) return;

    if (capture_window_client_to_present_buffer()) {
        log_present_event(trigger, "window-dc", NULL, 0, DD_OK);
        present_buffer();
    } else {
        log_present_event(trigger, "window-dc-failed", NULL, 0, DDERR_GENERIC);
    }
}

/* ================================================================
 * Proxy installation
 * ================================================================ */

static HRESULT WINAPI DD1_QueryInterface_Hook(LPDIRECTDRAW self, REFIID riid, void **out);
static HRESULT WINAPI DD2_QueryInterface_Hook(LPDIRECTDRAW2 self, REFIID riid, void **out);
static HRESULT WINAPI DD3_QueryInterface_Hook(LPDIRECTDRAW3 self, REFIID riid, void **out);
static HRESULT WINAPI DD4_QueryInterface_Hook(LPDIRECTDRAW4 self, REFIID riid, void **out);
static ULONG WINAPI DD1_Release_Hook(LPDIRECTDRAW self);
static ULONG WINAPI DD2_Release_Hook(LPDIRECTDRAW2 self);
static ULONG WINAPI DD3_Release_Hook(LPDIRECTDRAW3 self);
static ULONG WINAPI DD4_Release_Hook(LPDIRECTDRAW4 self);
static HRESULT WINAPI DD1_CreateSurface_Hook(LPDIRECTDRAW self, LPDDSURFACEDESC desc, LPDIRECTDRAWSURFACE *out, IUnknown *outer);
static HRESULT WINAPI DD2_CreateSurface_Hook(LPDIRECTDRAW2 self, LPDDSURFACEDESC desc, LPDIRECTDRAWSURFACE *out, IUnknown *outer);
static HRESULT WINAPI DD3_CreateSurface_Hook(LPDIRECTDRAW3 self, LPDDSURFACEDESC desc, LPDIRECTDRAWSURFACE *out, IUnknown *outer);
static HRESULT WINAPI DD4_CreateSurface_Hook(LPDIRECTDRAW4 self, LPDDSURFACEDESC2 desc, LPDIRECTDRAWSURFACE4 *out, IUnknown *outer);
static HRESULT WINAPI DD1_DuplicateSurface_Hook(LPDIRECTDRAW self, LPDIRECTDRAWSURFACE src, LPDIRECTDRAWSURFACE *out);
static HRESULT WINAPI DD2_DuplicateSurface_Hook(LPDIRECTDRAW2 self, LPDIRECTDRAWSURFACE src, LPDIRECTDRAWSURFACE *out);
static HRESULT WINAPI DD3_DuplicateSurface_Hook(LPDIRECTDRAW3 self, LPDIRECTDRAWSURFACE src, LPDIRECTDRAWSURFACE *out);
static HRESULT WINAPI DD4_DuplicateSurface_Hook(LPDIRECTDRAW4 self, LPDIRECTDRAWSURFACE4 src, LPDIRECTDRAWSURFACE4 *out);
static HRESULT WINAPI DD1_GetGDISurface_Hook(LPDIRECTDRAW self, LPDIRECTDRAWSURFACE *out);
static HRESULT WINAPI DD2_GetGDISurface_Hook(LPDIRECTDRAW2 self, LPDIRECTDRAWSURFACE *out);
static HRESULT WINAPI DD3_GetGDISurface_Hook(LPDIRECTDRAW3 self, LPDIRECTDRAWSURFACE *out);
static HRESULT WINAPI DD4_GetGDISurface_Hook(LPDIRECTDRAW4 self, LPDIRECTDRAWSURFACE4 *out);
static HRESULT WINAPI DD3_GetSurfaceFromDC_Hook(LPDIRECTDRAW3 self, HDC hdc, LPDIRECTDRAWSURFACE *out);
static HRESULT WINAPI DD4_GetSurfaceFromDC_Hook(LPDIRECTDRAW4 self, HDC hdc, LPDIRECTDRAWSURFACE4 *out);
static HRESULT WINAPI DD1_RestoreDisplayMode_Hook(LPDIRECTDRAW self);
static HRESULT WINAPI DD2_RestoreDisplayMode_Hook(LPDIRECTDRAW2 self);
static HRESULT WINAPI DD3_RestoreDisplayMode_Hook(LPDIRECTDRAW3 self);
static HRESULT WINAPI DD4_RestoreDisplayMode_Hook(LPDIRECTDRAW4 self);
static HRESULT WINAPI DD1_SetCooperativeLevel_Hook(LPDIRECTDRAW self, HWND hwnd, DWORD flags);
static HRESULT WINAPI DD2_SetCooperativeLevel_Hook(LPDIRECTDRAW2 self, HWND hwnd, DWORD flags);
static HRESULT WINAPI DD3_SetCooperativeLevel_Hook(LPDIRECTDRAW3 self, HWND hwnd, DWORD flags);
static HRESULT WINAPI DD4_SetCooperativeLevel_Hook(LPDIRECTDRAW4 self, HWND hwnd, DWORD flags);
static HRESULT WINAPI DD1_SetDisplayMode_Hook(LPDIRECTDRAW self, DWORD w, DWORD h, DWORD bpp);
static HRESULT WINAPI DD2_SetDisplayMode_Hook(LPDIRECTDRAW2 self, DWORD w, DWORD h, DWORD bpp, DWORD refresh, DWORD flags);
static HRESULT WINAPI DD3_SetDisplayMode_Hook(LPDIRECTDRAW3 self, DWORD w, DWORD h, DWORD bpp, DWORD refresh, DWORD flags);
static HRESULT WINAPI DD4_SetDisplayMode_Hook(LPDIRECTDRAW4 self, DWORD w, DWORD h, DWORD bpp, DWORD refresh, DWORD flags);

static HRESULT WINAPI Surf1_QueryInterface_Hook(LPDIRECTDRAWSURFACE self, REFIID riid, void **out);
static HRESULT WINAPI Surf2_QueryInterface_Hook(LPDIRECTDRAWSURFACE2 self, REFIID riid, void **out);
static HRESULT WINAPI Surf3_QueryInterface_Hook(LPDIRECTDRAWSURFACE3 self, REFIID riid, void **out);
static HRESULT WINAPI Surf4_QueryInterface_Hook(LPDIRECTDRAWSURFACE4 self, REFIID riid, void **out);
static ULONG WINAPI Surf1_Release_Hook(LPDIRECTDRAWSURFACE self);
static ULONG WINAPI Surf2_Release_Hook(LPDIRECTDRAWSURFACE2 self);
static ULONG WINAPI Surf3_Release_Hook(LPDIRECTDRAWSURFACE3 self);
static ULONG WINAPI Surf4_Release_Hook(LPDIRECTDRAWSURFACE4 self);
static HRESULT WINAPI Surf1_Blt_Hook(LPDIRECTDRAWSURFACE self, LPRECT dst, LPDIRECTDRAWSURFACE src, LPRECT src_rect, DWORD flags, LPDDBLTFX fx);
static HRESULT WINAPI Surf2_Blt_Hook(LPDIRECTDRAWSURFACE2 self, LPRECT dst, LPDIRECTDRAWSURFACE2 src, LPRECT src_rect, DWORD flags, LPDDBLTFX fx);
static HRESULT WINAPI Surf3_Blt_Hook(LPDIRECTDRAWSURFACE3 self, LPRECT dst, LPDIRECTDRAWSURFACE3 src, LPRECT src_rect, DWORD flags, LPDDBLTFX fx);
static HRESULT WINAPI Surf4_Blt_Hook(LPDIRECTDRAWSURFACE4 self, LPRECT dst, LPDIRECTDRAWSURFACE4 src, LPRECT src_rect, DWORD flags, LPDDBLTFX fx);
static HRESULT WINAPI Surf1_BltFast_Hook(LPDIRECTDRAWSURFACE self, DWORD x, DWORD y, LPDIRECTDRAWSURFACE src, LPRECT src_rect, DWORD trans);
static HRESULT WINAPI Surf2_BltFast_Hook(LPDIRECTDRAWSURFACE2 self, DWORD x, DWORD y, LPDIRECTDRAWSURFACE2 src, LPRECT src_rect, DWORD trans);
static HRESULT WINAPI Surf3_BltFast_Hook(LPDIRECTDRAWSURFACE3 self, DWORD x, DWORD y, LPDIRECTDRAWSURFACE3 src, LPRECT src_rect, DWORD trans);
static HRESULT WINAPI Surf4_BltFast_Hook(LPDIRECTDRAWSURFACE4 self, DWORD x, DWORD y, LPDIRECTDRAWSURFACE4 src, LPRECT src_rect, DWORD trans);
static HRESULT WINAPI Surf1_Flip_Hook(LPDIRECTDRAWSURFACE self, LPDIRECTDRAWSURFACE override, DWORD flags);
static HRESULT WINAPI Surf2_Flip_Hook(LPDIRECTDRAWSURFACE2 self, LPDIRECTDRAWSURFACE2 override, DWORD flags);
static HRESULT WINAPI Surf3_Flip_Hook(LPDIRECTDRAWSURFACE3 self, LPDIRECTDRAWSURFACE3 override, DWORD flags);
static HRESULT WINAPI Surf4_Flip_Hook(LPDIRECTDRAWSURFACE4 self, LPDIRECTDRAWSURFACE4 override, DWORD flags);
static HRESULT WINAPI Surf1_GetAttachedSurface_Hook(LPDIRECTDRAWSURFACE self, LPDDSCAPS caps, LPDIRECTDRAWSURFACE *out);
static HRESULT WINAPI Surf2_GetAttachedSurface_Hook(LPDIRECTDRAWSURFACE2 self, LPDDSCAPS caps, LPDIRECTDRAWSURFACE2 *out);
static HRESULT WINAPI Surf3_GetAttachedSurface_Hook(LPDIRECTDRAWSURFACE3 self, LPDDSCAPS caps, LPDIRECTDRAWSURFACE3 *out);
static HRESULT WINAPI Surf4_GetAttachedSurface_Hook(LPDIRECTDRAWSURFACE4 self, LPDDSCAPS2 caps, LPDIRECTDRAWSURFACE4 *out);

static ProxyEntry *install_proxy(void *iface, ProxyKind kind)
{
    ProxyEntry *entry;
    size_t count;

    if (!iface || !g_proxy_lock_ready) return NULL;

    EnterCriticalSection(&g_proxy_lock);
    entry = find_proxy_locked(iface);
    if (entry) {
        if (entry->kind != kind) {
            rdd_log("install_proxy: existing kind mismatch %s != %s",
                    proxy_kind_name(entry->kind), proxy_kind_name(kind));
        }
        LeaveCriticalSection(&g_proxy_lock);
        return entry;
    }

    count = proxy_vtbl_count(kind);
    if (!count) {
        LeaveCriticalSection(&g_proxy_lock);
        return NULL;
    }

    entry = (ProxyEntry *)calloc(1, sizeof(*entry));
    if (!entry) {
        LeaveCriticalSection(&g_proxy_lock);
        return NULL;
    }

    entry->proxy_vtbl = (void **)calloc(count, sizeof(void *));
    if (!entry->proxy_vtbl) {
        free(entry);
        LeaveCriticalSection(&g_proxy_lock);
        return NULL;
    }

    entry->iface = iface;
    entry->kind = kind;
    entry->orig_vtbl = ((GenericIface *)iface)->lpVtbl;
    memcpy(entry->proxy_vtbl, entry->orig_vtbl, count * sizeof(void *));

    switch (kind) {
    case PROXY_DD1:
        entry->proxy_vtbl[0] = DD1_QueryInterface_Hook;
        entry->proxy_vtbl[2] = DD1_Release_Hook;
        entry->proxy_vtbl[6] = DD1_CreateSurface_Hook;
        entry->proxy_vtbl[7] = DD1_DuplicateSurface_Hook;
        entry->proxy_vtbl[14] = DD1_GetGDISurface_Hook;
        entry->proxy_vtbl[19] = DD1_RestoreDisplayMode_Hook;
        entry->proxy_vtbl[20] = DD1_SetCooperativeLevel_Hook;
        entry->proxy_vtbl[21] = DD1_SetDisplayMode_Hook;
        break;
    case PROXY_DD2:
        entry->proxy_vtbl[0] = DD2_QueryInterface_Hook;
        entry->proxy_vtbl[2] = DD2_Release_Hook;
        entry->proxy_vtbl[6] = DD2_CreateSurface_Hook;
        entry->proxy_vtbl[7] = DD2_DuplicateSurface_Hook;
        entry->proxy_vtbl[14] = DD2_GetGDISurface_Hook;
        entry->proxy_vtbl[19] = DD2_RestoreDisplayMode_Hook;
        entry->proxy_vtbl[20] = DD2_SetCooperativeLevel_Hook;
        entry->proxy_vtbl[21] = DD2_SetDisplayMode_Hook;
        break;
    case PROXY_DD3:
        entry->proxy_vtbl[0] = DD3_QueryInterface_Hook;
        entry->proxy_vtbl[2] = DD3_Release_Hook;
        entry->proxy_vtbl[6] = DD3_CreateSurface_Hook;
        entry->proxy_vtbl[7] = DD3_DuplicateSurface_Hook;
        entry->proxy_vtbl[14] = DD3_GetGDISurface_Hook;
        entry->proxy_vtbl[19] = DD3_RestoreDisplayMode_Hook;
        entry->proxy_vtbl[20] = DD3_SetCooperativeLevel_Hook;
        entry->proxy_vtbl[21] = DD3_SetDisplayMode_Hook;
        entry->proxy_vtbl[24] = DD3_GetSurfaceFromDC_Hook;
        break;
    case PROXY_DD4:
        entry->proxy_vtbl[0] = DD4_QueryInterface_Hook;
        entry->proxy_vtbl[2] = DD4_Release_Hook;
        entry->proxy_vtbl[6] = DD4_CreateSurface_Hook;
        entry->proxy_vtbl[7] = DD4_DuplicateSurface_Hook;
        entry->proxy_vtbl[14] = DD4_GetGDISurface_Hook;
        entry->proxy_vtbl[19] = DD4_RestoreDisplayMode_Hook;
        entry->proxy_vtbl[20] = DD4_SetCooperativeLevel_Hook;
        entry->proxy_vtbl[21] = DD4_SetDisplayMode_Hook;
        entry->proxy_vtbl[24] = DD4_GetSurfaceFromDC_Hook;
        break;
    case PROXY_SURF1:
        entry->proxy_vtbl[0] = Surf1_QueryInterface_Hook;
        entry->proxy_vtbl[2] = Surf1_Release_Hook;
        entry->proxy_vtbl[5] = Surf1_Blt_Hook;
        entry->proxy_vtbl[7] = Surf1_BltFast_Hook;
        entry->proxy_vtbl[11] = Surf1_Flip_Hook;
        entry->proxy_vtbl[12] = Surf1_GetAttachedSurface_Hook;
        break;
    case PROXY_SURF2:
        entry->proxy_vtbl[0] = Surf2_QueryInterface_Hook;
        entry->proxy_vtbl[2] = Surf2_Release_Hook;
        entry->proxy_vtbl[5] = Surf2_Blt_Hook;
        entry->proxy_vtbl[7] = Surf2_BltFast_Hook;
        entry->proxy_vtbl[11] = Surf2_Flip_Hook;
        entry->proxy_vtbl[12] = Surf2_GetAttachedSurface_Hook;
        break;
    case PROXY_SURF3:
        entry->proxy_vtbl[0] = Surf3_QueryInterface_Hook;
        entry->proxy_vtbl[2] = Surf3_Release_Hook;
        entry->proxy_vtbl[5] = Surf3_Blt_Hook;
        entry->proxy_vtbl[7] = Surf3_BltFast_Hook;
        entry->proxy_vtbl[11] = Surf3_Flip_Hook;
        entry->proxy_vtbl[12] = Surf3_GetAttachedSurface_Hook;
        break;
    case PROXY_SURF4:
        entry->proxy_vtbl[0] = Surf4_QueryInterface_Hook;
        entry->proxy_vtbl[2] = Surf4_Release_Hook;
        entry->proxy_vtbl[5] = Surf4_Blt_Hook;
        entry->proxy_vtbl[7] = Surf4_BltFast_Hook;
        entry->proxy_vtbl[11] = Surf4_Flip_Hook;
        entry->proxy_vtbl[12] = Surf4_GetAttachedSurface_Hook;
        break;
    }

    ((GenericIface *)iface)->lpVtbl = entry->proxy_vtbl;
    entry->next = g_proxy_list;
    g_proxy_list = entry;
    LeaveCriticalSection(&g_proxy_lock);

    if (!is_surface_kind(kind) || g_surface_proxy_install_logs < 24) {
        if (is_surface_kind(kind)) ++g_surface_proxy_install_logs;
        rdd_log("install_proxy: %s iface=%p", proxy_kind_name(kind), iface);
    }
    return entry;
}

static void wrap_query_result(REFIID riid, void **out)
{
    ProxyKind kind;
    if (!out || !*out) return;
    if (dd_kind_from_iid(riid, &kind) || surface_kind_from_iid(riid, &kind))
        install_proxy(*out, kind);
}

static HRESULT proxy_query_interface_common(void *self, REFIID riid, void **out)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *, REFIID, void **);

    if (!entry) return E_NOINTERFACE;
    orig = (HRESULT (WINAPI *)(void *, REFIID, void **))entry->orig_vtbl[0];
    hr = orig(self, riid, out);
    if (SUCCEEDED(hr)) wrap_query_result(riid, out);
    return hr;
}

static ULONG proxy_release_common(void *self)
{
    ProxyEntry *entry = find_proxy(self);
    ULONG ref;
    ULONG (WINAPI *orig)(void *);

    if (!entry) return 0;
    orig = (ULONG (WINAPI *)(void *))entry->orig_vtbl[2];
    ref = orig(self);
    if (ref == 0) {
        if (!is_surface_kind(entry->kind) || g_surface_proxy_release_logs < 24) {
            if (is_surface_kind(entry->kind)) ++g_surface_proxy_release_logs;
            rdd_log("release proxy: %s iface=%p", proxy_kind_name(entry->kind), self);
        }
        remove_proxy(self);
    }
    return ref;
}

static HRESULT dd_create_surface_legacy_common(void *self, LPDDSURFACEDESC desc,
                                               LPDIRECTDRAWSURFACE *out, IUnknown *outer)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *, LPDDSURFACEDESC, LPDIRECTDRAWSURFACE *, IUnknown *);
    if (!entry) return DDERR_GENERIC;
    orig = (HRESULT (WINAPI *)(void *, LPDDSURFACEDESC, LPDIRECTDRAWSURFACE *, IUnknown *))
        entry->orig_vtbl[6];
    hr = orig(self, desc, out, outer);
    if (SUCCEEDED(hr) && out && *out) install_proxy(*out, PROXY_SURF1);
    return hr;
}

static HRESULT dd_create_surface4_common(void *self, LPDDSURFACEDESC2 desc,
                                         LPDIRECTDRAWSURFACE4 *out, IUnknown *outer)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *, LPDDSURFACEDESC2, LPDIRECTDRAWSURFACE4 *, IUnknown *);
    if (!entry) return DDERR_GENERIC;
    orig = (HRESULT (WINAPI *)(void *, LPDDSURFACEDESC2, LPDIRECTDRAWSURFACE4 *, IUnknown *))
        entry->orig_vtbl[6];
    hr = orig(self, desc, out, outer);
    if (SUCCEEDED(hr) && out && *out) install_proxy(*out, PROXY_SURF4);
    return hr;
}

static HRESULT dd_duplicate_surface_legacy_common(void *self, LPDIRECTDRAWSURFACE src,
                                                  LPDIRECTDRAWSURFACE *out)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *, LPDIRECTDRAWSURFACE, LPDIRECTDRAWSURFACE *);
    if (!entry) return DDERR_GENERIC;
    orig = (HRESULT (WINAPI *)(void *, LPDIRECTDRAWSURFACE, LPDIRECTDRAWSURFACE *))
        entry->orig_vtbl[7];
    hr = orig(self, src, out);
    if (SUCCEEDED(hr) && out && *out) install_proxy(*out, PROXY_SURF1);
    return hr;
}

static HRESULT dd_duplicate_surface4_common(void *self, LPDIRECTDRAWSURFACE4 src,
                                            LPDIRECTDRAWSURFACE4 *out)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *, LPDIRECTDRAWSURFACE4, LPDIRECTDRAWSURFACE4 *);
    if (!entry) return DDERR_GENERIC;
    orig = (HRESULT (WINAPI *)(void *, LPDIRECTDRAWSURFACE4, LPDIRECTDRAWSURFACE4 *))
        entry->orig_vtbl[7];
    hr = orig(self, src, out);
    if (SUCCEEDED(hr) && out && *out) install_proxy(*out, PROXY_SURF4);
    return hr;
}

static HRESULT dd_get_gdi_surface_legacy_common(void *self, LPDIRECTDRAWSURFACE *out)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *, LPDIRECTDRAWSURFACE *);
    if (!entry) return DDERR_GENERIC;
    orig = (HRESULT (WINAPI *)(void *, LPDIRECTDRAWSURFACE *))entry->orig_vtbl[14];
    hr = orig(self, out);
    if (SUCCEEDED(hr) && out && *out) install_proxy(*out, PROXY_SURF1);
    return hr;
}

static HRESULT dd_get_gdi_surface4_common(void *self, LPDIRECTDRAWSURFACE4 *out)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *, LPDIRECTDRAWSURFACE4 *);
    if (!entry) return DDERR_GENERIC;
    orig = (HRESULT (WINAPI *)(void *, LPDIRECTDRAWSURFACE4 *))entry->orig_vtbl[14];
    hr = orig(self, out);
    if (SUCCEEDED(hr) && out && *out) install_proxy(*out, PROXY_SURF4);
    return hr;
}

static HRESULT dd_get_surface_from_dc_legacy_common(void *self, HDC hdc, LPDIRECTDRAWSURFACE *out)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *, HDC, LPDIRECTDRAWSURFACE *);
    if (!entry) return DDERR_GENERIC;
    orig = (HRESULT (WINAPI *)(void *, HDC, LPDIRECTDRAWSURFACE *))entry->orig_vtbl[24];
    hr = orig(self, hdc, out);
    if (SUCCEEDED(hr) && out && *out) install_proxy(*out, PROXY_SURF1);
    return hr;
}

static HRESULT dd_get_surface_from_dc4_common(void *self, HDC hdc, LPDIRECTDRAWSURFACE4 *out)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *, HDC, LPDIRECTDRAWSURFACE4 *);
    if (!entry) return DDERR_GENERIC;
    orig = (HRESULT (WINAPI *)(void *, HDC, LPDIRECTDRAWSURFACE4 *))entry->orig_vtbl[24];
    hr = orig(self, hdc, out);
    if (SUCCEEDED(hr) && out && *out) install_proxy(*out, PROXY_SURF4);
    return hr;
}

static HRESULT dd_restore_display_mode_common(void *self)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *);
    if (!entry) return DDERR_GENERIC;
    orig = (HRESULT (WINAPI *)(void *))entry->orig_vtbl[19];
    hr = orig(self);
    if (SUCCEEDED(hr)) restore_window();
    return hr;
}

static HRESULT dd_set_cooperative_level_common(void *self, HWND hwnd, DWORD flags)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *, HWND, DWORD);
    if (!entry) return DDERR_GENERIC;
    orig = (HRESULT (WINAPI *)(void *, HWND, DWORD))entry->orig_vtbl[20];
    if (hwnd) g_hwnd = hwnd;
    hr = orig(self, hwnd, flags);
    if (SUCCEEDED(hr) && (flags & DDSCL_NORMAL)) restore_window();
    return hr;
}

static HRESULT dd_set_display_mode_legacy_common(void *self, DWORD w, DWORD h, DWORD bpp)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *, DWORD, DWORD, DWORD);
    if (!entry) return DDERR_GENERIC;
    orig = (HRESULT (WINAPI *)(void *, DWORD, DWORD, DWORD))entry->orig_vtbl[21];
    hr = orig(self, w, h, bpp);
    rdd_log("DD1 SetDisplayMode %lux%lux%lu hr=0x%08lx",
            (unsigned long)w, (unsigned long)h, (unsigned long)bpp, (unsigned long)hr);
    if (SUCCEEDED(hr)) setup_fullscreen_window();
    return hr;
}

static HRESULT dd_set_display_mode_modern_common(void *self, DWORD w, DWORD h, DWORD bpp,
                                                 DWORD refresh, DWORD flags)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *, DWORD, DWORD, DWORD, DWORD, DWORD);
    if (!entry) return DDERR_GENERIC;
    orig = (HRESULT (WINAPI *)(void *, DWORD, DWORD, DWORD, DWORD, DWORD))entry->orig_vtbl[21];
    hr = orig(self, w, h, bpp, refresh, flags);
    rdd_log("DDx SetDisplayMode %lux%lux%lu refresh=%lu flags=0x%lx hr=0x%08lx",
            (unsigned long)w, (unsigned long)h, (unsigned long)bpp,
            (unsigned long)refresh, (unsigned long)flags, (unsigned long)hr);
    if (SUCCEEDED(hr)) setup_fullscreen_window();
    return hr;
}

static HRESULT surface_blt_common(void *self, void *dst_rect, void *src, void *src_rect,
                                  DWORD flags, LPDDBLTFX fx)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *, void *, void *, void *, DWORD, LPDDBLTFX);
    if (!entry) return DDERR_GENERIC;
    orig = (HRESULT (WINAPI *)(void *, void *, void *, void *, DWORD, LPDDBLTFX))
        entry->orig_vtbl[5];
    hr = orig(self, dst_rect, src, src_rect, flags, fx);
    if (SUCCEEDED(hr)) maybe_present_primary(self, entry->kind, "Blt");
    return hr;
}

static HRESULT surface_bltfast_common(void *self, DWORD x, DWORD y, void *src,
                                      void *src_rect, DWORD trans)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *, DWORD, DWORD, void *, void *, DWORD);
    if (!entry) return DDERR_GENERIC;
    orig = (HRESULT (WINAPI *)(void *, DWORD, DWORD, void *, void *, DWORD))
        entry->orig_vtbl[7];
    hr = orig(self, x, y, src, src_rect, trans);
    if (SUCCEEDED(hr)) maybe_present_primary(self, entry->kind, "BltFast");
    return hr;
}

static HRESULT surface_flip_common(void *self, void *override, DWORD flags)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *, void *, DWORD);
    if (!entry) return DDERR_GENERIC;
    orig = (HRESULT (WINAPI *)(void *, void *, DWORD))entry->orig_vtbl[11];
    hr = orig(self, override, flags);
    if (SUCCEEDED(hr)) maybe_present_primary(self, entry->kind, "Flip");
    return hr;
}

static HRESULT surface_get_attached_legacy_common(void *self, LPDDSCAPS caps,
                                                  LPDIRECTDRAWSURFACE *out)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *, LPDDSCAPS, LPDIRECTDRAWSURFACE *);
    if (!entry) return DDERR_GENERIC;
    orig = (HRESULT (WINAPI *)(void *, LPDDSCAPS, LPDIRECTDRAWSURFACE *))
        entry->orig_vtbl[12];
    hr = orig(self, caps, out);
    if (SUCCEEDED(hr) && out && *out) install_proxy(*out, entry->kind);
    return hr;
}

static HRESULT surface_get_attached4_common(void *self, LPDDSCAPS2 caps,
                                            LPDIRECTDRAWSURFACE4 *out)
{
    ProxyEntry *entry = find_proxy(self);
    HRESULT hr;
    HRESULT (WINAPI *orig)(void *, LPDDSCAPS2, LPDIRECTDRAWSURFACE4 *);
    if (!entry) return DDERR_GENERIC;
    orig = (HRESULT (WINAPI *)(void *, LPDDSCAPS2, LPDIRECTDRAWSURFACE4 *))
        entry->orig_vtbl[12];
    hr = orig(self, caps, out);
    if (SUCCEEDED(hr) && out && *out) install_proxy(*out, entry->kind);
    return hr;
}

/* ================================================================
 * Typed hook thunks
 * ================================================================ */

static HRESULT WINAPI DD1_QueryInterface_Hook(LPDIRECTDRAW self, REFIID riid, void **out)
    { return proxy_query_interface_common(self, riid, out); }
static HRESULT WINAPI DD2_QueryInterface_Hook(LPDIRECTDRAW2 self, REFIID riid, void **out)
    { return proxy_query_interface_common(self, riid, out); }
static HRESULT WINAPI DD3_QueryInterface_Hook(LPDIRECTDRAW3 self, REFIID riid, void **out)
    { return proxy_query_interface_common(self, riid, out); }
static HRESULT WINAPI DD4_QueryInterface_Hook(LPDIRECTDRAW4 self, REFIID riid, void **out)
    { return proxy_query_interface_common(self, riid, out); }
static ULONG WINAPI DD1_Release_Hook(LPDIRECTDRAW self)
    { return proxy_release_common(self); }
static ULONG WINAPI DD2_Release_Hook(LPDIRECTDRAW2 self)
    { return proxy_release_common(self); }
static ULONG WINAPI DD3_Release_Hook(LPDIRECTDRAW3 self)
    { return proxy_release_common(self); }
static ULONG WINAPI DD4_Release_Hook(LPDIRECTDRAW4 self)
    { return proxy_release_common(self); }
static HRESULT WINAPI DD1_CreateSurface_Hook(LPDIRECTDRAW self, LPDDSURFACEDESC desc, LPDIRECTDRAWSURFACE *out, IUnknown *outer)
    { return dd_create_surface_legacy_common(self, desc, out, outer); }
static HRESULT WINAPI DD2_CreateSurface_Hook(LPDIRECTDRAW2 self, LPDDSURFACEDESC desc, LPDIRECTDRAWSURFACE *out, IUnknown *outer)
    { return dd_create_surface_legacy_common(self, desc, out, outer); }
static HRESULT WINAPI DD3_CreateSurface_Hook(LPDIRECTDRAW3 self, LPDDSURFACEDESC desc, LPDIRECTDRAWSURFACE *out, IUnknown *outer)
    { return dd_create_surface_legacy_common(self, desc, out, outer); }
static HRESULT WINAPI DD4_CreateSurface_Hook(LPDIRECTDRAW4 self, LPDDSURFACEDESC2 desc, LPDIRECTDRAWSURFACE4 *out, IUnknown *outer)
    { return dd_create_surface4_common(self, desc, out, outer); }
static HRESULT WINAPI DD1_DuplicateSurface_Hook(LPDIRECTDRAW self, LPDIRECTDRAWSURFACE src, LPDIRECTDRAWSURFACE *out)
    { return dd_duplicate_surface_legacy_common(self, src, out); }
static HRESULT WINAPI DD2_DuplicateSurface_Hook(LPDIRECTDRAW2 self, LPDIRECTDRAWSURFACE src, LPDIRECTDRAWSURFACE *out)
    { return dd_duplicate_surface_legacy_common(self, src, out); }
static HRESULT WINAPI DD3_DuplicateSurface_Hook(LPDIRECTDRAW3 self, LPDIRECTDRAWSURFACE src, LPDIRECTDRAWSURFACE *out)
    { return dd_duplicate_surface_legacy_common(self, src, out); }
static HRESULT WINAPI DD4_DuplicateSurface_Hook(LPDIRECTDRAW4 self, LPDIRECTDRAWSURFACE4 src, LPDIRECTDRAWSURFACE4 *out)
    { return dd_duplicate_surface4_common(self, src, out); }
static HRESULT WINAPI DD1_GetGDISurface_Hook(LPDIRECTDRAW self, LPDIRECTDRAWSURFACE *out)
    { return dd_get_gdi_surface_legacy_common(self, out); }
static HRESULT WINAPI DD2_GetGDISurface_Hook(LPDIRECTDRAW2 self, LPDIRECTDRAWSURFACE *out)
    { return dd_get_gdi_surface_legacy_common(self, out); }
static HRESULT WINAPI DD3_GetGDISurface_Hook(LPDIRECTDRAW3 self, LPDIRECTDRAWSURFACE *out)
    { return dd_get_gdi_surface_legacy_common(self, out); }
static HRESULT WINAPI DD4_GetGDISurface_Hook(LPDIRECTDRAW4 self, LPDIRECTDRAWSURFACE4 *out)
    { return dd_get_gdi_surface4_common(self, out); }
static HRESULT WINAPI DD3_GetSurfaceFromDC_Hook(LPDIRECTDRAW3 self, HDC hdc, LPDIRECTDRAWSURFACE *out)
    { return dd_get_surface_from_dc_legacy_common(self, hdc, out); }
static HRESULT WINAPI DD4_GetSurfaceFromDC_Hook(LPDIRECTDRAW4 self, HDC hdc, LPDIRECTDRAWSURFACE4 *out)
    { return dd_get_surface_from_dc4_common(self, hdc, out); }
static HRESULT WINAPI DD1_RestoreDisplayMode_Hook(LPDIRECTDRAW self)
    { return dd_restore_display_mode_common(self); }
static HRESULT WINAPI DD2_RestoreDisplayMode_Hook(LPDIRECTDRAW2 self)
    { return dd_restore_display_mode_common(self); }
static HRESULT WINAPI DD3_RestoreDisplayMode_Hook(LPDIRECTDRAW3 self)
    { return dd_restore_display_mode_common(self); }
static HRESULT WINAPI DD4_RestoreDisplayMode_Hook(LPDIRECTDRAW4 self)
    { return dd_restore_display_mode_common(self); }
static HRESULT WINAPI DD1_SetCooperativeLevel_Hook(LPDIRECTDRAW self, HWND hwnd, DWORD flags)
    { return dd_set_cooperative_level_common(self, hwnd, flags); }
static HRESULT WINAPI DD2_SetCooperativeLevel_Hook(LPDIRECTDRAW2 self, HWND hwnd, DWORD flags)
    { return dd_set_cooperative_level_common(self, hwnd, flags); }
static HRESULT WINAPI DD3_SetCooperativeLevel_Hook(LPDIRECTDRAW3 self, HWND hwnd, DWORD flags)
    { return dd_set_cooperative_level_common(self, hwnd, flags); }
static HRESULT WINAPI DD4_SetCooperativeLevel_Hook(LPDIRECTDRAW4 self, HWND hwnd, DWORD flags)
    { return dd_set_cooperative_level_common(self, hwnd, flags); }
static HRESULT WINAPI DD1_SetDisplayMode_Hook(LPDIRECTDRAW self, DWORD w, DWORD h, DWORD bpp)
    { return dd_set_display_mode_legacy_common(self, w, h, bpp); }
static HRESULT WINAPI DD2_SetDisplayMode_Hook(LPDIRECTDRAW2 self, DWORD w, DWORD h, DWORD bpp, DWORD refresh, DWORD flags)
    { return dd_set_display_mode_modern_common(self, w, h, bpp, refresh, flags); }
static HRESULT WINAPI DD3_SetDisplayMode_Hook(LPDIRECTDRAW3 self, DWORD w, DWORD h, DWORD bpp, DWORD refresh, DWORD flags)
    { return dd_set_display_mode_modern_common(self, w, h, bpp, refresh, flags); }
static HRESULT WINAPI DD4_SetDisplayMode_Hook(LPDIRECTDRAW4 self, DWORD w, DWORD h, DWORD bpp, DWORD refresh, DWORD flags)
    { return dd_set_display_mode_modern_common(self, w, h, bpp, refresh, flags); }

static HRESULT WINAPI Surf1_QueryInterface_Hook(LPDIRECTDRAWSURFACE self, REFIID riid, void **out)
    { return proxy_query_interface_common(self, riid, out); }
static HRESULT WINAPI Surf2_QueryInterface_Hook(LPDIRECTDRAWSURFACE2 self, REFIID riid, void **out)
    { return proxy_query_interface_common(self, riid, out); }
static HRESULT WINAPI Surf3_QueryInterface_Hook(LPDIRECTDRAWSURFACE3 self, REFIID riid, void **out)
    { return proxy_query_interface_common(self, riid, out); }
static HRESULT WINAPI Surf4_QueryInterface_Hook(LPDIRECTDRAWSURFACE4 self, REFIID riid, void **out)
    { return proxy_query_interface_common(self, riid, out); }
static ULONG WINAPI Surf1_Release_Hook(LPDIRECTDRAWSURFACE self)
    { return proxy_release_common(self); }
static ULONG WINAPI Surf2_Release_Hook(LPDIRECTDRAWSURFACE2 self)
    { return proxy_release_common(self); }
static ULONG WINAPI Surf3_Release_Hook(LPDIRECTDRAWSURFACE3 self)
    { return proxy_release_common(self); }
static ULONG WINAPI Surf4_Release_Hook(LPDIRECTDRAWSURFACE4 self)
    { return proxy_release_common(self); }
static HRESULT WINAPI Surf1_Blt_Hook(LPDIRECTDRAWSURFACE self, LPRECT dst, LPDIRECTDRAWSURFACE src, LPRECT src_rect, DWORD flags, LPDDBLTFX fx)
    { return surface_blt_common(self, dst, src, src_rect, flags, fx); }
static HRESULT WINAPI Surf2_Blt_Hook(LPDIRECTDRAWSURFACE2 self, LPRECT dst, LPDIRECTDRAWSURFACE2 src, LPRECT src_rect, DWORD flags, LPDDBLTFX fx)
    { return surface_blt_common(self, dst, src, src_rect, flags, fx); }
static HRESULT WINAPI Surf3_Blt_Hook(LPDIRECTDRAWSURFACE3 self, LPRECT dst, LPDIRECTDRAWSURFACE3 src, LPRECT src_rect, DWORD flags, LPDDBLTFX fx)
    { return surface_blt_common(self, dst, src, src_rect, flags, fx); }
static HRESULT WINAPI Surf4_Blt_Hook(LPDIRECTDRAWSURFACE4 self, LPRECT dst, LPDIRECTDRAWSURFACE4 src, LPRECT src_rect, DWORD flags, LPDDBLTFX fx)
    { return surface_blt_common(self, dst, src, src_rect, flags, fx); }
static HRESULT WINAPI Surf1_BltFast_Hook(LPDIRECTDRAWSURFACE self, DWORD x, DWORD y, LPDIRECTDRAWSURFACE src, LPRECT src_rect, DWORD trans)
    { return surface_bltfast_common(self, x, y, src, src_rect, trans); }
static HRESULT WINAPI Surf2_BltFast_Hook(LPDIRECTDRAWSURFACE2 self, DWORD x, DWORD y, LPDIRECTDRAWSURFACE2 src, LPRECT src_rect, DWORD trans)
    { return surface_bltfast_common(self, x, y, src, src_rect, trans); }
static HRESULT WINAPI Surf3_BltFast_Hook(LPDIRECTDRAWSURFACE3 self, DWORD x, DWORD y, LPDIRECTDRAWSURFACE3 src, LPRECT src_rect, DWORD trans)
    { return surface_bltfast_common(self, x, y, src, src_rect, trans); }
static HRESULT WINAPI Surf4_BltFast_Hook(LPDIRECTDRAWSURFACE4 self, DWORD x, DWORD y, LPDIRECTDRAWSURFACE4 src, LPRECT src_rect, DWORD trans)
    { return surface_bltfast_common(self, x, y, src, src_rect, trans); }
static HRESULT WINAPI Surf1_Flip_Hook(LPDIRECTDRAWSURFACE self, LPDIRECTDRAWSURFACE override, DWORD flags)
    { return surface_flip_common(self, override, flags); }
static HRESULT WINAPI Surf2_Flip_Hook(LPDIRECTDRAWSURFACE2 self, LPDIRECTDRAWSURFACE2 override, DWORD flags)
    { return surface_flip_common(self, override, flags); }
static HRESULT WINAPI Surf3_Flip_Hook(LPDIRECTDRAWSURFACE3 self, LPDIRECTDRAWSURFACE3 override, DWORD flags)
    { return surface_flip_common(self, override, flags); }
static HRESULT WINAPI Surf4_Flip_Hook(LPDIRECTDRAWSURFACE4 self, LPDIRECTDRAWSURFACE4 override, DWORD flags)
    { return surface_flip_common(self, override, flags); }
static HRESULT WINAPI Surf1_GetAttachedSurface_Hook(LPDIRECTDRAWSURFACE self, LPDDSCAPS caps, LPDIRECTDRAWSURFACE *out)
    { return surface_get_attached_legacy_common(self, caps, out); }
static HRESULT WINAPI Surf2_GetAttachedSurface_Hook(LPDIRECTDRAWSURFACE2 self, LPDDSCAPS caps, LPDIRECTDRAWSURFACE2 *out)
    { return surface_get_attached_legacy_common(self, caps, (LPDIRECTDRAWSURFACE *)out); }
static HRESULT WINAPI Surf3_GetAttachedSurface_Hook(LPDIRECTDRAWSURFACE3 self, LPDDSCAPS caps, LPDIRECTDRAWSURFACE3 *out)
    { return surface_get_attached_legacy_common(self, caps, (LPDIRECTDRAWSURFACE *)out); }
static HRESULT WINAPI Surf4_GetAttachedSurface_Hook(LPDIRECTDRAWSURFACE4 self, LPDDSCAPS2 caps, LPDIRECTDRAWSURFACE4 *out)
    { return surface_get_attached4_common(self, caps, out); }

/* ================================================================
 * Exported entry points
 * ================================================================ */

HRESULT WINAPI DirectDrawCreate(GUID *driver, LPDIRECTDRAW *ddraw, IUnknown *outer)
{
    HRESULT hr;
    if (!load_real_ddraw()) return DDERR_GENERIC;
    hr = g_real_DirectDrawCreate(driver, ddraw, outer);
    if (SUCCEEDED(hr) && ddraw && *ddraw) install_proxy(*ddraw, PROXY_DD1);
    return hr;
}

HRESULT WINAPI DirectDrawCreateEx(GUID *driver, void **ddraw, REFIID iid, IUnknown *outer)
{
    HRESULT hr;
    ProxyKind kind;

    if (!load_real_ddraw()) return DDERR_GENERIC;
    hr = g_real_DirectDrawCreateEx(driver, ddraw, iid, outer);
    if (SUCCEEDED(hr) && ddraw && *ddraw && dd_kind_from_iid(iid, &kind))
        install_proxy(*ddraw, kind);
    return hr;
}

HRESULT WINAPI DirectDrawCreateClipper(DWORD flags, LPDIRECTDRAWCLIPPER *clipper, IUnknown *outer)
{
    if (!load_real_ddraw()) return DDERR_GENERIC;
    return g_real_DirectDrawCreateClipper(flags, clipper, outer);
}

HRESULT WINAPI DirectDrawEnumerateA(LPDDENUMCALLBACKA cb, void *ctx)
{
    if (!load_real_ddraw()) return DDERR_GENERIC;
    return g_real_DirectDrawEnumerateA(cb, ctx);
}

HRESULT WINAPI DirectDrawEnumerateExA(LPDDENUMCALLBACKEXA cb, void *ctx, DWORD flags)
{
    if (!load_real_ddraw()) return DDERR_GENERIC;
    return g_real_DirectDrawEnumerateExA(cb, ctx, flags);
}

HRESULT WINAPI DirectDrawEnumerateW(LPDDENUMCALLBACKW cb, void *ctx)
{
    if (!load_real_ddraw()) return DDERR_GENERIC;
    return g_real_DirectDrawEnumerateW(cb, ctx);
}

HRESULT WINAPI DirectDrawEnumerateExW(LPDDENUMCALLBACKEXW cb, void *ctx, DWORD flags)
{
    if (!load_real_ddraw()) return DDERR_GENERIC;
    return g_real_DirectDrawEnumerateExW(cb, ctx, flags);
}

HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }
HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **out)
{
    (void)rclsid; (void)riid;
    if (out) *out = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}

/* ================================================================
 * DllMain
 * ================================================================ */

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        g_self_module = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        InitializeCriticalSection(&g_proxy_lock);
        g_proxy_lock_ready = TRUE;
        init_lut();
        init_bmi();
        ensure_present_state();
        rdd_log("=== revenant_ddraw proxy loaded ===");
    } else if (reason == DLL_PROCESS_DETACH) {
        restore_window();
        rdd_log("=== revenant_ddraw proxy unloaded ===");
        if (g_real_ddraw) {
            FreeLibrary(g_real_ddraw);
            g_real_ddraw = NULL;
        }
        if (g_present_buf) {
            free(g_present_buf);
            g_present_buf = NULL;
        }
        if (g_logfile) {
            fclose(g_logfile);
            g_logfile = NULL;
        }
        if (g_proxy_lock_ready) {
            DeleteCriticalSection(&g_proxy_lock);
            g_proxy_lock_ready = FALSE;
        }
        g_self_module = NULL;
    }

    return TRUE;
}

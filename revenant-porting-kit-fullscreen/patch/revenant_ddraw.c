/*
 * revenant_ddraw.c - Self-contained DirectDraw replacement for Revenant (1999)
 *
 * Replaces Wine's DirectDraw entirely. Manages pixel buffers internally
 * and scales the game's 640x480x16 (RGB565) output to fill the window
 * via GDI StretchDIBits.
 *
 * Build:
 *   i686-w64-mingw32-gcc -shared -o ddraw.dll revenant_ddraw.c ddraw_new.def \
 *       -lgdi32 -luser32 -lkernel32 -O2 -Wl,--enable-stdcall-fixup
 */

#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================
 * Logging
 * ================================================================ */

static FILE *g_logfile = NULL;

static void rdd_log(const char *fmt, ...) {
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
 * GUIDs (defined manually to avoid INITGUID/mingw issues)
 * ================================================================ */

static int guid_eq(REFIID a, const GUID *b) {
    return memcmp(a, b, sizeof(GUID)) == 0;
}

static const GUID MY_IID_IUnknown =
    {0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const GUID MY_IID_IDirectDraw =
    {0x6C14DB80,0xA733,0x11CE,{0xA5,0x21,0x00,0x20,0xAF,0x0B,0xE5,0x60}};
static const GUID MY_IID_IDirectDraw2 =
    {0xB3A6F3E0,0x2B43,0x11CF,{0xA2,0xDE,0x00,0xAA,0x00,0xB9,0x33,0x56}};
static const GUID MY_IID_IDirectDraw4 =
    {0x9C59509A,0x39BD,0x11D1,{0x8C,0x4A,0x00,0xC0,0x4F,0xD9,0x30,0xC5}};
static const GUID MY_IID_IDirect3D =
    {0x3BBA0080,0x2421,0x11CF,{0xA3,0x1A,0x00,0xAA,0x00,0xB9,0x33,0x56}};
static const GUID MY_IID_IDirect3D2 =
    {0x6AAE1EC1,0x662A,0x11D0,{0x88,0x9D,0x00,0xAA,0x00,0xBB,0xB7,0x6A}};
static const GUID MY_IID_IDirect3D3 =
    {0xBB223240,0xE72B,0x11D0,{0xA9,0xB4,0x00,0xAA,0x00,0xC0,0x99,0x3E}};
static const GUID MY_IID_IDirect3D7 =
    {0xF5049E77,0x4861,0x11D2,{0xA4,0x07,0x00,0xA0,0xC9,0x06,0x29,0xA8}};
static const GUID MY_IID_IDirect3DViewport =
    {0x4417C146,0x33AD,0x11CF,{0x81,0x6F,0x00,0x00,0xC0,0x20,0x15,0x6E}};
static const GUID MY_IID_IDirect3DViewport2 =
    {0x93281501,0x8CF8,0x11D0,{0x89,0xAB,0x00,0xA0,0xC9,0x05,0x41,0x29}};
static const GUID MY_IID_IDirect3DViewport3 =
    {0xB0AB3B61,0x33D7,0x11D1,{0xA9,0x81,0x00,0xC0,0x4F,0xD7,0xB1,0x74}};
static const GUID MY_IID_IDirect3DDevice3 =
    {0xB0AB3B60,0x33D7,0x11D1,{0xA9,0x81,0x00,0xC0,0x4F,0xD7,0xB1,0x74}};
static const GUID MY_IID_IDirect3DTexture =
    {0x2CDCD9E0,0x25A0,0x11CF,{0xA3,0x1A,0x00,0xAA,0x00,0xB9,0x33,0x56}};
static const GUID MY_IID_IDirect3DTexture2 =
    {0x93281502,0x8CF8,0x11D0,{0x89,0xAB,0x00,0xA0,0xC9,0x05,0x41,0x29}};
static const GUID MY_IID_IDirectDrawSurface =
    {0x6C14DB81,0xA733,0x11CE,{0xA5,0x21,0x00,0x20,0xAF,0x0B,0xE5,0x60}};
static const GUID MY_IID_IDirectDrawSurface2 =
    {0x57805885,0x6EEC,0x11CF,{0x94,0x41,0xA8,0x23,0x03,0xC1,0x0E,0x27}};
static const GUID MY_IID_IDirectDrawSurface3 =
    {0xDA044E00,0x69B2,0x11D0,{0xA1,0xD5,0x00,0xAA,0x00,0xB8,0xDF,0xBB}};
static const GUID MY_IID_IDirectDrawSurface4 =
    {0x0B2B8630,0xAD35,0x11D0,{0x8E,0xA6,0x00,0x60,0x97,0x97,0xEA,0x5B}};

/* ================================================================
 * Forward declarations
 * ================================================================ */

typedef struct RDDSurfaceVtbl RDDSurfaceVtbl;
typedef struct RDDSurface RDDSurface;
typedef struct DD4Vtbl DD4Vtbl;
typedef struct DD1Vtbl DD1Vtbl;
typedef struct DD4Obj DD4Obj;
typedef struct DD1Obj DD1Obj;

/* ================================================================
 * Types
 * ================================================================ */

typedef enum { SURF_PRIMARY, SURF_BACKBUFFER, SURF_OFFSCREEN } SurfType;

struct RDDSurface {
    RDDSurfaceVtbl *lpVtbl;
    ULONG refcount;
    SurfType type;
    DWORD width, height, bpp;
    LONG pitch;
    BYTE *pixels;
    BOOL locked;
    RDDSurface *back_buffer;    /* primary -> back */
    DWORD caps;
    DDPIXELFORMAT pixfmt;  /* actual pixel format from CreateSurface */
    DDCOLORKEY src_color_key;
    BOOL has_src_color_key;
    /* GetDC state */
    HDC hdc_mem;
    HBITMAP hdc_bmp;
    void *hdc_bits;
};

struct DD4Obj { DD4Vtbl *lpVtbl; };
struct DD1Obj { DD1Vtbl *lpVtbl; };

/* ================================================================
 * Global state
 * ================================================================ */

static HWND g_hwnd = NULL;
static RDDSurface *g_primary = NULL;
static DWORD *g_present_buf = NULL;
static DWORD g_rgb565_lut[65536];
static BITMAPINFO g_bmi;
static ULONG g_dd_refcount = 0;
static DD4Obj g_dd4;
static DD1Obj g_dd1;
static BOOL g_initialized = FALSE;
static RDDSurface *g_render_target = NULL;
static RECT g_orig_rect = {0};
static BOOL g_is_fullscreen = FALSE;

static void restore_window(void);

/* ================================================================
 * Presentation: RGB565 LUT + StretchDIBits
 * ================================================================ */

static void init_lut(void) {
    int i;
    for (i = 0; i < 65536; i++) {
        DWORD r5 = (i >> 11) & 0x1F;
        DWORD g6 = (i >> 5)  & 0x3F;
        DWORD b5 =  i        & 0x1F;
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
    g_bmi.bmiHeader.biHeight = -480;    /* top-down */
    g_bmi.bmiHeader.biPlanes = 1;
    g_bmi.bmiHeader.biBitCount = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;
}

static void present_frame(RDDSurface *primary) {
    RECT rc;
    HDC hdc;
    int count, i;
    WORD *src;
    int win_w, win_h, dest_w, dest_h, dest_x, dest_y;

    if (!g_hwnd || !primary || !primary->pixels || !g_present_buf || !g_is_fullscreen) return;

    /* Convert RGB565 -> BGR8888 */
    src = (WORD *)primary->pixels;
    count = (int)(primary->width * primary->height);
    for (i = 0; i < count; i++)
        g_present_buf[i] = g_rgb565_lut[src[i]];

    GetClientRect(g_hwnd, &rc);
    hdc = GetDC(g_hwnd);
    if (!hdc) return;

    win_w = rc.right;
    win_h = rc.bottom;

    /* Compute centered 4:3 rectangle preserving aspect ratio */
    if (win_w * 3 > win_h * 4) {
        /* Window wider than 4:3: pillarbox (black bars left/right) */
        dest_h = win_h;
        dest_w = win_h * 4 / 3;
        dest_x = (win_w - dest_w) / 2;
        dest_y = 0;
    } else {
        /* Window taller than 4:3: letterbox (black bars top/bottom) */
        dest_w = win_w;
        dest_h = win_w * 3 / 4;
        dest_x = 0;
        dest_y = (win_h - dest_h) / 2;
    }

    /* Fill black bars */
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
        0, 0, (int)primary->width, (int)primary->height,
        g_present_buf, &g_bmi, DIB_RGB_COLORS, SRCCOPY);

    ReleaseDC(g_hwnd, hdc);
}

/* ================================================================
 * Helpers
 * ================================================================ */

static void fill_pixelformat_rgb565(DDPIXELFORMAT *pf) {
    memset(pf, 0, sizeof(*pf));
    pf->dwSize = sizeof(DDPIXELFORMAT);
    pf->dwFlags = DDPF_RGB;
    pf->dwRGBBitCount = 16;
    pf->dwRBitMask = 0xF800;
    pf->dwGBitMask = 0x07E0;
    pf->dwBBitMask = 0x001F;
}

static void fill_surface_desc(RDDSurface *surf, DDSURFACEDESC2 *desc) {
    memset(desc, 0, sizeof(*desc));
    desc->dwSize = sizeof(DDSURFACEDESC2);
    desc->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_CAPS;
    desc->dwWidth = surf->width;
    desc->dwHeight = surf->height;
    desc->lPitch = surf->pitch;
    desc->ddpfPixelFormat = surf->pixfmt;
    desc->ddsCaps.dwCaps = surf->caps;
}

/* ================================================================
 * Surface allocation
 * ================================================================ */

/* Forward-declared vtable instance */
static RDDSurfaceVtbl g_surf_vtbl;

static RDDSurface *alloc_surface(SurfType type, DWORD w, DWORD h, DWORD bpp, DDPIXELFORMAT *pf, DWORD req_caps) {
    RDDSurface *s = (RDDSurface *)calloc(1, sizeof(RDDSurface));
    if (!s) return NULL;
    s->lpVtbl = &g_surf_vtbl;
    s->refcount = 1;
    s->type = type;
    s->width = w;
    s->height = h;
    s->bpp = bpp;
    s->pitch = (LONG)(((w * bpp + 63) & ~63) >> 3);
    s->pixels = (BYTE *)calloc(1, (size_t)(s->pitch * h));
    if (pf) {
        s->pixfmt = *pf;
    } else {
        fill_pixelformat_rgb565(&s->pixfmt);
    }

    switch (type) {
    case SURF_PRIMARY:
        s->caps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX | DDSCAPS_VISIBLE;
        break;
    case SURF_BACKBUFFER:
        s->caps = DDSCAPS_BACKBUFFER | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
        break;
    case SURF_OFFSCREEN:
        s->caps = req_caps ? req_caps : (DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY);
        break;
    }

    rdd_log("alloc_surface: %dx%dx%d type=%d pitch=%d", w, h, bpp, type, s->pitch);
    return s;
}

/* ================================================================
 * IDirectDrawSurface4 methods (45 total)
 * ================================================================ */

/* ================================================================
 * IDirect3DTexture2 stub (6 methods)
 * Returned when surfaces are QI'd for texture interface.
 * ================================================================ */

typedef struct D3DTexObj D3DTexObj;
struct D3DTexObj { void **lpVtbl; RDDSurface *surf; };

static HRESULT WINAPI D3DTex_QI(D3DTexObj *s, REFIID r, void **o) { (void)s; (void)r; *o = NULL; return E_NOINTERFACE; }
static ULONG WINAPI D3DTex_AddRef(D3DTexObj *s) { (void)s; return 2; }
static ULONG WINAPI D3DTex_Release(D3DTexObj *s) { (void)s; return 1; }
static HRESULT WINAPI D3DTex_GetHandle(D3DTexObj *s, void *dev, void *handle) {
    (void)dev;
    *(DWORD *)handle = (DWORD)(void *)s->surf;
    rdd_log("D3DTex_GetHandle: surf=%p handle=0x%lx", (void*)s->surf, (unsigned long)(DWORD)(void*)s->surf);
    return DD_OK;
}
static HRESULT WINAPI D3DTex_PaletteChanged(D3DTexObj *s, void *a, void *b) { rdd_log("D3DTex_PaletteChanged"); (void)s; (void)a; (void)b; return DD_OK; }
static HRESULT WINAPI D3DTex_Load(D3DTexObj *s, void *src_tex) {
    rdd_log("D3DTex_Load: dst=%p src=%p", (void*)s->surf, src_tex);
    /* Copy pixel data from source texture surface to destination */
    if (src_tex) {
        D3DTexObj *src = (D3DTexObj *)src_tex;
        if (src->surf && s->surf &&
            src->surf->width == s->surf->width &&
            src->surf->height == s->surf->height) {
            /* Copy row-by-row to handle pitch mismatches between surfaces */
            DWORD y;
            LONG row_bytes = (LONG)(s->surf->width * (s->surf->bpp / 8));
            for (y = 0; y < s->surf->height; y++) {
                memcpy(s->surf->pixels + y * s->surf->pitch,
                       src->surf->pixels + y * src->surf->pitch,
                       (size_t)row_bytes);
            }
            rdd_log("D3DTex_Load: copied %lux%lu pixels (src_pitch=%ld dst_pitch=%ld)",
                    s->surf->width, s->surf->height, src->surf->pitch, s->surf->pitch);
        } else if (src->surf && s->surf) {
            rdd_log("D3DTex_Load: dimension mismatch dst=%lux%lu src=%lux%lu",
                    s->surf->width, s->surf->height, src->surf->width, src->surf->height);
        }
    }
    return DD_OK;
}

static void *g_d3dtex_vtbl[6] = {
    D3DTex_QI, D3DTex_AddRef, D3DTex_Release,
    D3DTex_GetHandle,      /* 3 */
    D3DTex_PaletteChanged, /* 4 */
    D3DTex_Load            /* 5 */
};

/* Pool of texture objects (surfaces can be QI'd for texture) */
#define MAX_TEX_OBJS 512
static D3DTexObj g_tex_pool[MAX_TEX_OBJS];
static int g_tex_count = 0;

static D3DTexObj *get_tex_for_surface(RDDSurface *surf) {
    int i;
    for (i = 0; i < g_tex_count; i++)
        if (g_tex_pool[i].surf == surf) return &g_tex_pool[i];
    if (g_tex_count < MAX_TEX_OBJS) {
        D3DTexObj *t = &g_tex_pool[g_tex_count++];
        t->lpVtbl = g_d3dtex_vtbl;
        t->surf = surf;
        return t;
    }
    rdd_log("ERROR: texture pool exhausted (%d/%d)", g_tex_count, MAX_TEX_OBJS);
    return NULL;
}

/* --- IUnknown --- */

static HRESULT WINAPI Surf_QueryInterface(RDDSurface *self, REFIID riid, void **out) {
    if (guid_eq(riid, &MY_IID_IDirectDrawSurface) ||
        guid_eq(riid, &MY_IID_IDirectDrawSurface2) ||
        guid_eq(riid, &MY_IID_IDirectDrawSurface3) ||
        guid_eq(riid, &MY_IID_IDirectDrawSurface4) ||
        guid_eq(riid, &MY_IID_IUnknown)) {
        self->refcount++;
        *out = self;
        return S_OK;
    }
    if (guid_eq(riid, &MY_IID_IDirect3DTexture) ||
        guid_eq(riid, &MY_IID_IDirect3DTexture2)) {
        D3DTexObj *tex = get_tex_for_surface(self);
        if (tex) { *out = tex; return S_OK; }
    }
    *out = NULL;
    rdd_log("Surf_QI: unknown {%08lx...}", (unsigned long)riid->Data1);
    return E_NOINTERFACE;
}

static ULONG WINAPI Surf_AddRef(RDDSurface *self) {
    return ++self->refcount;
}

static ULONG WINAPI Surf_Release(RDDSurface *self) {
    ULONG ref = --self->refcount;
    if (ref == 0) {
        rdd_log("Surf_Release: freeing surface type=%d", self->type);
        if (self->pixels) free(self->pixels);
        if (self->back_buffer) {
            /* Also free the back buffer if we own it */
            if (self->back_buffer->pixels) free(self->back_buffer->pixels);
            free(self->back_buffer);
        }
        if (g_primary == self) g_primary = NULL;
        free(self);
    }
    return ref;
}

/* --- Stubs (simple returns) --- */

static HRESULT WINAPI Surf_AddAttachedSurface(RDDSurface *s, RDDSurface *a)
    { rdd_log("Surf_AddAttachedSurface: self_type=%d attached_caps=0x%lx", s->type, a ? a->caps : 0); return DD_OK; }
static HRESULT WINAPI Surf_AddOverlayDirtyRect(RDDSurface *s, LPRECT r)
    { (void)s; (void)r; return DD_OK; }
static HRESULT WINAPI Surf_BltBatch(RDDSurface *s, void *b, DWORD c, DWORD d)
    { rdd_log("Surf_BltBatch: UNSUPPORTED"); (void)s; (void)b; (void)c; (void)d; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI Surf_DeleteAttachedSurface(RDDSurface *s, DWORD f, RDDSurface *a)
    { (void)s; (void)f; (void)a; return DD_OK; }
static HRESULT WINAPI Surf_EnumOverlayZOrders(RDDSurface *s, DWORD f, void *c, void *cb)
    { (void)s; (void)f; (void)c; (void)cb; return DD_OK; }
static HRESULT WINAPI Surf_GetBltStatus(RDDSurface *s, DWORD f)
    { (void)s; (void)f; return DD_OK; }
static HRESULT WINAPI Surf_GetClipper(RDDSurface *s, void **c)
    { (void)s; *c = NULL; return DDERR_NOCLIPPERATTACHED; }
static HRESULT WINAPI Surf_GetColorKey(RDDSurface *s, DWORD f, LPDDCOLORKEY k) {
    if ((f & DDCKEY_SRCBLT) && s->has_src_color_key) {
        *k = s->src_color_key;
        return DD_OK;
    }
    return DDERR_NOCOLORKEY;
}
static HRESULT WINAPI Surf_GetDC(RDDSurface *s, HDC *hdc) {
    HDC screen_dc, mem_dc;
    BITMAPINFO bmi;
    HBITMAP bmp;
    void *bits;
    DWORD y;

    screen_dc = GetDC(NULL);
    mem_dc = CreateCompatibleDC(screen_dc);

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)s->width;
    bmi.bmiHeader.biHeight = -(LONG)s->height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    bmp = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, screen_dc);
    if (!bmp) { DeleteDC(mem_dc); *hdc = NULL; return DDERR_GENERIC; }

    /* Convert surface RGB565 pixels to 32-bit DIB */
    for (y = 0; y < s->height; y++) {
        WORD *src = (WORD *)(s->pixels + y * s->pitch);
        DWORD *dst = (DWORD *)((BYTE *)bits + y * s->width * 4);
        DWORD x;
        for (x = 0; x < s->width; x++) {
            WORD p = src[x];
            DWORD r = ((p >> 11) & 0x1F); r = (r << 3) | (r >> 2);
            DWORD g = ((p >> 5) & 0x3F); g = (g << 2) | (g >> 4);
            DWORD b = (p & 0x1F); b = (b << 3) | (b >> 2);
            dst[x] = (r << 16) | (g << 8) | b;
        }
    }

    SelectObject(mem_dc, bmp);
    s->hdc_mem = mem_dc;
    s->hdc_bmp = bmp;
    s->hdc_bits = bits;
    *hdc = mem_dc;
    rdd_log("Surf_GetDC: type=%d OK", s->type);
    return DD_OK;
}
static HRESULT WINAPI Surf_GetFlipStatus(RDDSurface *s, DWORD f)
    { (void)s; (void)f; return DD_OK; }
static HRESULT WINAPI Surf_GetOverlayPosition(RDDSurface *s, LPLONG x, LPLONG y)
    { (void)s; (void)x; (void)y; return DDERR_NOTAOVERLAYSURFACE; }
static HRESULT WINAPI Surf_GetPalette(RDDSurface *s, void **p)
    { (void)s; *p = NULL; return DDERR_NOPALETTEATTACHED; }
static HRESULT WINAPI Surf_Initialize(RDDSurface *s, void *dd, LPDDSURFACEDESC2 d)
    { (void)s; (void)dd; (void)d; return DDERR_ALREADYINITIALIZED; }
static HRESULT WINAPI Surf_ReleaseDC(RDDSurface *s, HDC hdc) {
    (void)hdc;
    if (s->hdc_bits) {
        /* Convert 32-bit DIB back to surface RGB565 */
        DWORD y;
        for (y = 0; y < s->height; y++) {
            DWORD *src = (DWORD *)((BYTE *)s->hdc_bits + y * s->width * 4);
            WORD *dst = (WORD *)(s->pixels + y * s->pitch);
            DWORD x;
            for (x = 0; x < s->width; x++) {
                DWORD p = src[x];
                dst[x] = (WORD)((((p >> 16) & 0xFF) >> 3) << 11 |
                                (((p >> 8) & 0xFF) >> 2) << 5 |
                                ((p & 0xFF) >> 3));
            }
        }
    }
    if (s->hdc_bmp) DeleteObject(s->hdc_bmp);
    if (s->hdc_mem) DeleteDC(s->hdc_mem);
    s->hdc_mem = NULL;
    s->hdc_bmp = NULL;
    s->hdc_bits = NULL;
    return DD_OK;
}
static HRESULT WINAPI Surf_SetClipper(RDDSurface *s, void *c)
    { rdd_log("Surf_SetClipper: type=%d clipper=%p", s->type, c); return DD_OK; }
static HRESULT WINAPI Surf_SetColorKey(RDDSurface *s, DWORD f, LPDDCOLORKEY k) {
    if ((f & DDCKEY_SRCBLT) && k) {
        s->src_color_key = *k;
        s->has_src_color_key = TRUE;
        rdd_log("Surf_SetColorKey: SRCBLT low=0x%lx high=0x%lx type=%d",
                k->dwColorSpaceLowValue, k->dwColorSpaceHighValue, s->type);
    }
    return DD_OK;
}
static HRESULT WINAPI Surf_SetOverlayPosition(RDDSurface *s, LONG x, LONG y)
    { (void)s; (void)x; (void)y; return DDERR_NOTAOVERLAYSURFACE; }
static HRESULT WINAPI Surf_SetPalette(RDDSurface *s, void *p)
    { (void)s; (void)p; return DD_OK; }
static HRESULT WINAPI Surf_UpdateOverlay(RDDSurface *s, LPRECT sr, RDDSurface *d, LPRECT dr, DWORD f, void *fx)
    { (void)s; (void)sr; (void)d; (void)dr; (void)f; (void)fx; return DDERR_NOTAOVERLAYSURFACE; }
static HRESULT WINAPI Surf_UpdateOverlayDisplay(RDDSurface *s, DWORD f)
    { (void)s; (void)f; return DDERR_NOTAOVERLAYSURFACE; }
static HRESULT WINAPI Surf_UpdateOverlayZOrder(RDDSurface *s, DWORD f, RDDSurface *r)
    { (void)s; (void)f; (void)r; return DDERR_NOTAOVERLAYSURFACE; }
static HRESULT WINAPI Surf_PageLock(RDDSurface *s, DWORD f)
    { (void)s; (void)f; return DD_OK; }
static HRESULT WINAPI Surf_PageUnlock(RDDSurface *s, DWORD f)
    { (void)s; (void)f; return DD_OK; }
static HRESULT WINAPI Surf_SetSurfaceDesc(RDDSurface *s, LPDDSURFACEDESC2 d, DWORD f)
    { (void)s; (void)d; (void)f; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI Surf_SetPrivateData(RDDSurface *s, REFGUID g, void *d, DWORD sz, DWORD f)
    { (void)s; (void)g; (void)d; (void)sz; (void)f; return DD_OK; }
static HRESULT WINAPI Surf_GetPrivateData(RDDSurface *s, REFGUID g, void *d, DWORD *sz)
    { (void)s; (void)g; (void)d; (void)sz; return DDERR_NOTFOUND; }
static HRESULT WINAPI Surf_FreePrivateData(RDDSurface *s, REFGUID g)
    { (void)s; (void)g; return DD_OK; }
static HRESULT WINAPI Surf_GetUniquenessValue(RDDSurface *s, DWORD *v)
    { (void)s; *v = 0; return DD_OK; }
static HRESULT WINAPI Surf_ChangeUniquenessValue(RDDSurface *s)
    { (void)s; return DD_OK; }

/* --- Real implementations --- */

static HRESULT WINAPI Surf_IsLost(RDDSurface *self) {
    (void)self;
    return DD_OK;
}

static HRESULT WINAPI Surf_Restore(RDDSurface *self) {
    (void)self;
    return DD_OK;
}

static HRESULT WINAPI Surf_GetCaps(RDDSurface *self, LPDDSCAPS2 caps) {
    memset(caps, 0, sizeof(DDSCAPS2));
    caps->dwCaps = self->caps;
    return DD_OK;
}

static HRESULT WINAPI Surf_GetPixelFormat(RDDSurface *self, LPDDPIXELFORMAT pf) {
    *pf = self->pixfmt;
    return DD_OK;
}

static HRESULT WINAPI Surf_GetSurfaceDesc(RDDSurface *self, LPDDSURFACEDESC2 desc) {
    fill_surface_desc(self, desc);
    return DD_OK;
}

static HRESULT WINAPI Surf_GetAttachedSurface(RDDSurface *self, LPDDSCAPS2 caps, RDDSurface **out) {
    if (self->type == SURF_PRIMARY && self->back_buffer &&
        (caps->dwCaps & DDSCAPS_BACKBUFFER)) {
        self->back_buffer->refcount++;
        *out = self->back_buffer;
        rdd_log("GetAttachedSurface: returning back buffer");
        return DD_OK;
    }
    *out = NULL;
    rdd_log("GetAttachedSurface: not found (caps=0x%lx)", caps->dwCaps);
    return DDERR_NOTFOUND;
}

static HRESULT WINAPI Surf_EnumAttachedSurfaces(RDDSurface *self, void *ctx, void *cb_raw) {
    typedef HRESULT (WINAPI *EnumCB)(RDDSurface *, DDSURFACEDESC2 *, void *);
    EnumCB cb = (EnumCB)cb_raw;

    if (self->type == SURF_PRIMARY && self->back_buffer) {
        DDSURFACEDESC2 desc;
        fill_surface_desc(self->back_buffer, &desc);
        cb(self->back_buffer, &desc, ctx);
    }
    return DD_OK;
}

static HRESULT WINAPI Surf_GetDDInterface(RDDSurface *self, void **dd) {
    (void)self;
    g_dd_refcount++;
    *dd = &g_dd4;
    return DD_OK;
}

static HRESULT WINAPI Surf_Lock(RDDSurface *self, LPRECT rect, LPDDSURFACEDESC2 desc, DWORD flags, HANDLE event) {
    static int logged = 0;
    (void)flags; (void)event;

    if (logged < 5) {
        rdd_log("Surf_Lock: %dx%d type=%d rect=%s", self->width, self->height, self->type,
                rect ? "subrect" : "full");
        logged++;
    }

    fill_surface_desc(self, desc);
    desc->dwFlags |= DDSD_LPSURFACE;

    if (rect) {
        desc->lpSurface = self->pixels + (rect->top * self->pitch) + (rect->left * (self->bpp / 8));
    } else {
        desc->lpSurface = self->pixels;
    }

    self->locked = TRUE;
    return DD_OK;
}

static HRESULT WINAPI Surf_Unlock(RDDSurface *self, LPRECT rect) {
    (void)rect;
    self->locked = FALSE;
    return DD_OK;
}

static HRESULT WINAPI Surf_Flip(RDDSurface *self, RDDSurface *override, DWORD flags) {
    static int logged = 0;
    BYTE *tmp;
    (void)override; (void)flags;

    if (self->type != SURF_PRIMARY || !self->back_buffer)
        return DDERR_NOTFLIPPABLE;

    /* Swap pixel buffers */
    tmp = self->pixels;
    self->pixels = self->back_buffer->pixels;
    self->back_buffer->pixels = tmp;

    /* Present the new front buffer */
    present_frame(self);

    if (logged < 5) {
        rdd_log("Surf_Flip: presented %dx%d", self->width, self->height);
        logged++;
    }
    return DD_OK;
}

static HRESULT WINAPI Surf_Blt(RDDSurface *self, LPRECT dst_rect,
                                RDDSurface *src, LPRECT src_rect,
                                DWORD flags, LPDDBLTFX fx) {
    static int logged = 0;

    /* Color fill */
    if (flags & DDBLT_COLORFILL) {
        WORD fill16 = (WORD)(fx->dwFillColor & 0xFFFF);
        RECT dr;
        int y, x;
        if (dst_rect) {
            dr = *dst_rect;
        } else {
            dr.left = 0; dr.top = 0;
            dr.right = (LONG)self->width; dr.bottom = (LONG)self->height;
        }
        for (y = dr.top; y < dr.bottom; y++) {
            WORD *row = (WORD *)(self->pixels + y * self->pitch);
            for (x = dr.left; x < dr.right; x++)
                row[x] = fill16;
        }
        { rdd_log("Surf_Blt: colorfill 0x%04x type=%d", fill16, self->type); }
        if (self->type == SURF_PRIMARY)
            present_frame(self);
        return DD_OK;
    }

    /* Surface-to-surface copy */
    if (src) {
        RECT sr, dr;
        int src_w, src_h, dst_w, dst_h, y;
        int bytes_per_pixel = (int)(self->bpp / 8);
        BOOL use_colorkey = FALSE;
        DWORD ck_low = 0;

        /* Determine if source color key transparency is needed */
        if ((flags & DDBLT_KEYSRCOVERRIDE) && fx) {
            use_colorkey = TRUE;
            ck_low = fx->ddckSrcColorkey.dwColorSpaceLowValue;
        } else if ((flags & DDBLT_KEYSRC) && src->has_src_color_key) {
            use_colorkey = TRUE;
            ck_low = src->src_color_key.dwColorSpaceLowValue;
        }

        sr.left = 0; sr.top = 0;
        sr.right = (LONG)src->width; sr.bottom = (LONG)src->height;
        if (src_rect) sr = *src_rect;

        dr.left = 0; dr.top = 0;
        dr.right = (LONG)self->width; dr.bottom = (LONG)self->height;
        if (dst_rect) dr = *dst_rect;

        src_w = sr.right - sr.left;
        src_h = sr.bottom - sr.top;
        dst_w = dr.right - dr.left;
        dst_h = dr.bottom - dr.top;

        /* Simple copy (no stretch): use minimum dimensions */
        if (src_w > dst_w) src_w = dst_w;
        if (src_h > dst_h) src_h = dst_h;

        if (use_colorkey && bytes_per_pixel == 2) {
            WORD ck16 = (WORD)(ck_low & 0xFFFF);
            for (y = 0; y < src_h; y++) {
                WORD *d = (WORD *)(self->pixels + (dr.top + y) * self->pitch + dr.left * 2);
                WORD *s = (WORD *)(src->pixels + (sr.top + y) * src->pitch + sr.left * 2);
                int x;
                for (x = 0; x < src_w; x++) {
                    if (s[x] != ck16)
                        d[x] = s[x];
                }
            }
        } else {
            for (y = 0; y < src_h; y++) {
                BYTE *dst_row = self->pixels + (dr.top + y) * self->pitch + dr.left * bytes_per_pixel;
                BYTE *src_row = src->pixels + (sr.top + y) * src->pitch + sr.left * bytes_per_pixel;
                memcpy(dst_row, src_row, (size_t)(src_w * bytes_per_pixel));
            }
        }
        { rdd_log("Surf_Blt: copy %dx%d to type=%d colorkey=%d", src_w, src_h, self->type, use_colorkey); }

        /* Present if we just blitted to the primary surface */
        if (self->type == SURF_PRIMARY)
            present_frame(self);

        return DD_OK;
    }

    rdd_log("Surf_Blt: unhandled flags=0x%lx", flags);
    return DD_OK;
}

static HRESULT WINAPI Surf_BltFast(RDDSurface *self, DWORD dx, DWORD dy,
                                    RDDSurface *src, LPRECT src_rect, DWORD flags) {
    RECT sr;
    int w, h, y;
    int bytes_per_pixel;

    if (!src) return DDERR_INVALIDPARAMS;

    bytes_per_pixel = (int)(self->bpp / 8);

    sr.left = 0; sr.top = 0;
    sr.right = (LONG)src->width; sr.bottom = (LONG)src->height;
    if (src_rect) sr = *src_rect;

    w = sr.right - sr.left;
    h = sr.bottom - sr.top;

    /* Clip to destination */
    if ((int)dx + w > (int)self->width)  w = (int)self->width - (int)dx;
    if ((int)dy + h > (int)self->height) h = (int)self->height - (int)dy;
    if (w <= 0 || h <= 0) return DD_OK;

    if ((flags & DDBLTFAST_SRCCOLORKEY) && src->has_src_color_key && bytes_per_pixel == 2) {
        WORD ck16 = (WORD)(src->src_color_key.dwColorSpaceLowValue & 0xFFFF);
        for (y = 0; y < h; y++) {
            WORD *d = (WORD *)(self->pixels + ((int)dy + y) * self->pitch + (int)dx * 2);
            WORD *s = (WORD *)(src->pixels + (sr.top + y) * src->pitch + sr.left * 2);
            int x;
            for (x = 0; x < w; x++) {
                if (s[x] != ck16)
                    d[x] = s[x];
            }
        }
    } else {
        for (y = 0; y < h; y++) {
            BYTE *dst_row = self->pixels + ((int)dy + y) * self->pitch + (int)dx * bytes_per_pixel;
            BYTE *src_row = src->pixels + (sr.top + y) * src->pitch + sr.left * bytes_per_pixel;
            memcpy(dst_row, src_row, (size_t)(w * bytes_per_pixel));
        }
    }

    /* Present if we just blitted to the primary surface */
    if (self->type == SURF_PRIMARY)
        present_frame(self);

    return DD_OK;
}

/* ================================================================
 * Surface vtable definition
 * ================================================================ */

struct RDDSurfaceVtbl {
    HRESULT (WINAPI *QueryInterface)(RDDSurface*, REFIID, void**);
    ULONG   (WINAPI *AddRef)(RDDSurface*);
    ULONG   (WINAPI *Release)(RDDSurface*);
    HRESULT (WINAPI *AddAttachedSurface)(RDDSurface*, RDDSurface*);
    HRESULT (WINAPI *AddOverlayDirtyRect)(RDDSurface*, LPRECT);
    HRESULT (WINAPI *Blt)(RDDSurface*, LPRECT, RDDSurface*, LPRECT, DWORD, LPDDBLTFX);
    HRESULT (WINAPI *BltBatch)(RDDSurface*, void*, DWORD, DWORD);
    HRESULT (WINAPI *BltFast)(RDDSurface*, DWORD, DWORD, RDDSurface*, LPRECT, DWORD);
    HRESULT (WINAPI *DeleteAttachedSurface)(RDDSurface*, DWORD, RDDSurface*);
    HRESULT (WINAPI *EnumAttachedSurfaces)(RDDSurface*, void*, void*);
    HRESULT (WINAPI *EnumOverlayZOrders)(RDDSurface*, DWORD, void*, void*);
    HRESULT (WINAPI *Flip)(RDDSurface*, RDDSurface*, DWORD);
    HRESULT (WINAPI *GetAttachedSurface)(RDDSurface*, LPDDSCAPS2, RDDSurface**);
    HRESULT (WINAPI *GetBltStatus)(RDDSurface*, DWORD);
    HRESULT (WINAPI *GetCaps)(RDDSurface*, LPDDSCAPS2);
    HRESULT (WINAPI *GetClipper)(RDDSurface*, void**);
    HRESULT (WINAPI *GetColorKey)(RDDSurface*, DWORD, LPDDCOLORKEY);
    HRESULT (WINAPI *GetDC)(RDDSurface*, HDC*);
    HRESULT (WINAPI *GetFlipStatus)(RDDSurface*, DWORD);
    HRESULT (WINAPI *GetOverlayPosition)(RDDSurface*, LPLONG, LPLONG);
    HRESULT (WINAPI *GetPalette)(RDDSurface*, void**);
    HRESULT (WINAPI *GetPixelFormat)(RDDSurface*, LPDDPIXELFORMAT);
    HRESULT (WINAPI *GetSurfaceDesc)(RDDSurface*, LPDDSURFACEDESC2);
    HRESULT (WINAPI *Initialize)(RDDSurface*, void*, LPDDSURFACEDESC2);
    HRESULT (WINAPI *IsLost)(RDDSurface*);
    HRESULT (WINAPI *Lock)(RDDSurface*, LPRECT, LPDDSURFACEDESC2, DWORD, HANDLE);
    HRESULT (WINAPI *ReleaseDC)(RDDSurface*, HDC);
    HRESULT (WINAPI *Restore)(RDDSurface*);
    HRESULT (WINAPI *SetClipper)(RDDSurface*, void*);
    HRESULT (WINAPI *SetColorKey)(RDDSurface*, DWORD, LPDDCOLORKEY);
    HRESULT (WINAPI *SetOverlayPosition)(RDDSurface*, LONG, LONG);
    HRESULT (WINAPI *SetPalette)(RDDSurface*, void*);
    HRESULT (WINAPI *Unlock)(RDDSurface*, LPRECT);
    HRESULT (WINAPI *UpdateOverlay)(RDDSurface*, LPRECT, RDDSurface*, LPRECT, DWORD, void*);
    HRESULT (WINAPI *UpdateOverlayDisplay)(RDDSurface*, DWORD);
    HRESULT (WINAPI *UpdateOverlayZOrder)(RDDSurface*, DWORD, RDDSurface*);
    HRESULT (WINAPI *GetDDInterface)(RDDSurface*, void**);
    HRESULT (WINAPI *PageLock)(RDDSurface*, DWORD);
    HRESULT (WINAPI *PageUnlock)(RDDSurface*, DWORD);
    HRESULT (WINAPI *SetSurfaceDesc)(RDDSurface*, LPDDSURFACEDESC2, DWORD);
    HRESULT (WINAPI *SetPrivateData)(RDDSurface*, REFGUID, void*, DWORD, DWORD);
    HRESULT (WINAPI *GetPrivateData)(RDDSurface*, REFGUID, void*, DWORD*);
    HRESULT (WINAPI *FreePrivateData)(RDDSurface*, REFGUID);
    HRESULT (WINAPI *GetUniquenessValue)(RDDSurface*, DWORD*);
    HRESULT (WINAPI *ChangeUniquenessValue)(RDDSurface*);
};

static RDDSurfaceVtbl g_surf_vtbl = {
    Surf_QueryInterface,
    Surf_AddRef,
    Surf_Release,
    Surf_AddAttachedSurface,        /* 3 */
    Surf_AddOverlayDirtyRect,       /* 4 */
    Surf_Blt,                       /* 5 */
    Surf_BltBatch,                  /* 6 */
    Surf_BltFast,                   /* 7 */
    Surf_DeleteAttachedSurface,     /* 8 */
    Surf_EnumAttachedSurfaces,      /* 9 */
    Surf_EnumOverlayZOrders,        /* 10 */
    Surf_Flip,                      /* 11 */
    Surf_GetAttachedSurface,        /* 12 */
    Surf_GetBltStatus,              /* 13 */
    Surf_GetCaps,                   /* 14 */
    Surf_GetClipper,                /* 15 */
    Surf_GetColorKey,               /* 16 */
    Surf_GetDC,                     /* 17 */
    Surf_GetFlipStatus,             /* 18 */
    Surf_GetOverlayPosition,        /* 19 */
    Surf_GetPalette,                /* 20 */
    Surf_GetPixelFormat,            /* 21 */
    Surf_GetSurfaceDesc,            /* 22 */
    Surf_Initialize,                /* 23 */
    Surf_IsLost,                    /* 24 */
    Surf_Lock,                      /* 25 */
    Surf_ReleaseDC,                 /* 26 */
    Surf_Restore,                   /* 27 */
    Surf_SetClipper,                /* 28 */
    Surf_SetColorKey,               /* 29 */
    Surf_SetOverlayPosition,        /* 30 */
    Surf_SetPalette,                /* 31 */
    Surf_Unlock,                    /* 32 */
    Surf_UpdateOverlay,             /* 33 */
    Surf_UpdateOverlayDisplay,      /* 34 */
    Surf_UpdateOverlayZOrder,       /* 35 */
    Surf_GetDDInterface,            /* 36 */
    Surf_PageLock,                  /* 37 */
    Surf_PageUnlock,                /* 38 */
    Surf_SetSurfaceDesc,            /* 39 */
    Surf_SetPrivateData,            /* 40 */
    Surf_GetPrivateData,            /* 41 */
    Surf_FreePrivateData,           /* 42 */
    Surf_GetUniquenessValue,        /* 43 */
    Surf_ChangeUniquenessValue      /* 44 */
};

/* ================================================================
 * IDirectDraw4 methods (28 total)
 * ================================================================ */

/* ================================================================
 * Minimal IDirect3D3 stub (just enough for DirectX 6 version check)
 * ================================================================ */

typedef struct D3DObj D3DObj;
typedef struct D3DVtbl D3DVtbl;
struct D3DObj { D3DVtbl *lpVtbl; };

static HRESULT WINAPI D3D_QueryInterface(D3DObj *self, REFIID riid, void **out);
static ULONG WINAPI D3D_AddRef(D3DObj *self) { (void)self; return ++g_dd_refcount; }
static ULONG WINAPI D3D_Release(D3DObj *self) { (void)self; if (g_dd_refcount > 0) g_dd_refcount--; return g_dd_refcount; }

/* IDirect3D3 has 10 methods. Most are stubs. */
/* ================================================================
 * Minimal IDirect3DDevice3 stub (42 methods)
 * The game creates this but does its own software rendering via
 * locked DirectDraw surfaces, so all methods are no-ops.
 * ================================================================ */

typedef struct D3DDevObj D3DDevObj;
struct D3DDevObj { void **lpVtbl; };

/* Forward declarations for D3D objects used by device stubs */
static D3DObj g_d3d;
typedef struct D3DVPObj D3DVPObj;
struct D3DVPObj { void **lpVtbl; };
static D3DVPObj g_d3dvp;

/* Generic stubs by parameter count (stdcall, all params are 4 bytes on x86) */
static HRESULT WINAPI d3dd_stub1(void *s) { (void)s; return DD_OK; }
static HRESULT WINAPI d3dd_stub2(void *s, void *a) { (void)s; (void)a; return DD_OK; }
static HRESULT WINAPI d3dd_stub3(void *s, void *a, void *b) { (void)s; (void)a; (void)b; return DD_OK; }
static HRESULT WINAPI d3dd_stub4(void *s, void *a, void *b, void *c) { (void)s; (void)a; (void)b; (void)c; return DD_OK; }
static HRESULT WINAPI d3dd_stub5(void *s, void *a, void *b, void *c, void *d) { (void)s; (void)a; (void)b; (void)c; (void)d; return DD_OK; }
static HRESULT WINAPI d3dd_stub6(void *s, void *a, void *b, void *c, void *d, void *e) { (void)s; (void)a; (void)b; (void)c; (void)d; (void)e; return DD_OK; }
static HRESULT WINAPI d3dd_stub7(void *s, void *a, void *b, void *c, void *d, void *e, void *f) { (void)s; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return DD_OK; }
static HRESULT WINAPI d3dd_stub8(void *s, void *a, void *b, void *c, void *d, void *e, void *f, void *g) { (void)s; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; return DD_OK; }

static HRESULT WINAPI D3DDev_QueryInterface(D3DDevObj *self, REFIID riid, void **out) {
    if (guid_eq(riid, &MY_IID_IDirect3DDevice3) || guid_eq(riid, &MY_IID_IUnknown)) {
        *out = self; rdd_log("D3DDev_QI: returning self"); return S_OK;
    }
    *out = NULL;
    rdd_log("D3DDev_QI: E_NOINTERFACE {%08lx-...}", riid->Data1);
    return E_NOINTERFACE;
}
static ULONG WINAPI D3DDev_AddRef(D3DDevObj *self) { (void)self; return 2; }
static ULONG WINAPI D3DDev_Release(D3DDevObj *self) { (void)self; return 1; }

/* Fill a D3DDEVICEDESC (252 bytes / 63 DWORDs) with software renderer caps.
 * Layout:
 *   d[0]=dwSize  d[1]=dwFlags  d[2]=dcmColorModel  d[3]=dwDevCaps
 *   d[4..5]=dtcTransformCaps  d[6]=bClipping  d[7..10]=dlcLightingCaps
 *   d[11..24]=dpcLineCaps (D3DPRIMCAPS, 14 DWORDs)
 *   d[25..38]=dpcTriCaps  d[39]=dwDeviceRenderBitDepth  d[40]=dwDeviceZBufferBitDepth
 *   d[41]=dwMaxBufferSize  d[42]=dwMaxVertexCount
 *   d[43..44]=minTex WxH  d[45..46]=maxTex WxH  d[47..50]=stipple min/max
 *   d[51]=maxTexRepeat  d[52]=maxTexAspect  d[53]=maxAniso
 *   d[54..58]=guard band + extentsAdjust (floats)
 *   d[59]=stencilCaps  d[60]=fvfCaps  d[61]=texOpCaps
 *   d[62]=wMaxTexBlendStages(lo) | wMaxSimTextures(hi)
 */
static void fill_d3d_device_desc(void *desc) {
    DWORD *d = (DWORD *)desc;
    memset(d, 0, 252);

    d[0]  = 252;       /* dwSize */
    d[1]  = 0x7FF;     /* dwFlags: all capability fields valid */
    d[2]  = 1;         /* dcmColorModel: D3DCOLOR_RGB */
    d[3]  = 0x00000110 /* TEXTURESYSTEMMEMORY(0x100) | EXECUTESYSTEMMEMORY(0x10) */
          | 0x00000440;/* DRAWPRIMTLVERTEX(0x400) | TLVERTEXSYSTEMMEMORY(0x40) */
    d[4]  = 8;         /* dtcTransformCaps.dwSize */
    d[5]  = 1;         /* dtcTransformCaps.dwCaps: D3DTRANSFORMCAPS_CLIP */
    d[6]  = 1;         /* bClipping = TRUE */
    d[7]  = 16;        /* dlcLightingCaps.dwSize */
    d[8]  = 0x1F;      /* dlcLightingCaps.dwCaps: all light types */
    d[9]  = 1;         /* dlcLightingCaps.dwLightingModel: RGB */
    d[10] = 8;         /* dlcLightingCaps.dwNumLights */

    /* dpcLineCaps (d[11..24]) and dpcTriCaps (d[25..38]) - D3DPRIMCAPS, 14 DWORDs each */
    /* Fill both identically */
    {
        int base;
        for (base = 11; base <= 25; base += 14) {
            d[base+0]  = 56;       /* dwSize */
            d[base+1]  = 0x70;     /* dwMiscCaps: CULLNONE|CULLCW|CULLCCW */
            d[base+2]  = 0x21;     /* dwRasterCaps: DITHER | SUBPIXEL */
            d[base+3]  = 0xFF;     /* dwZCmpCaps: all compare functions */
            d[base+4]  = 0x1FFF;   /* dwSrcBlendCaps */
            d[base+5]  = 0x1FFF;   /* dwDestBlendCaps */
            d[base+6]  = 0xFF;     /* dwAlphaCmpCaps */
            d[base+7]  = 0x03FCFC; /* dwShadeCaps: gouraud + flat + specular */
            d[base+8]  = 0x0D;     /* dwTextureCaps: PERSPECTIVE|ALPHA|TRANSPARENCY */
            d[base+9]  = 0x0703;   /* dwTextureFilterCaps: nearest + linear + mip */
            d[base+10] = 0x07;     /* dwTextureBlendCaps: decal + modulate + add */
            d[base+11] = 0x03;     /* dwTextureAddressCaps: wrap + mirror */
        }
    }

    d[39] = 0x0400;    /* dwDeviceRenderBitDepth: DDBD_16 */
    d[40] = 0x0400;    /* dwDeviceZBufferBitDepth: DDBD_16 */
    d[42] = 65536;     /* dwMaxVertexCount */
    d[43] = 1;         /* dwMinTextureWidth */
    d[44] = 1;         /* dwMinTextureHeight */
    d[45] = 1024;      /* dwMaxTextureWidth */
    d[46] = 1024;      /* dwMaxTextureHeight */
    d[51] = 1024;      /* dwMaxTextureRepeat */
    d[52] = 1024;      /* dwMaxTextureAspectRatio */
    d[53] = 1;         /* dwMaxAnisotropy */
}

/* Individual device stubs with logging */
static HRESULT WINAPI D3DDev_GetCaps(void *s, void *hal, void *hel) {
    rdd_log("D3DDev[3] GetCaps");
    (void)s;
    if (hal) fill_d3d_device_desc(hal);
    if (hel) fill_d3d_device_desc(hel);
    return DD_OK;
}
static HRESULT WINAPI D3DDev_GetStats(void *s, void *a) { (void)s; if (a) memset(a, 0, 36); return DD_OK; }
static HRESULT WINAPI D3DDev_AddViewport(void *s, void *a) { rdd_log("D3DDev[5] AddViewport"); (void)s; (void)a; return DD_OK; }
static HRESULT WINAPI D3DDev_DeleteViewport(void *s, void *a) { rdd_log("D3DDev[6] DeleteViewport"); (void)s; (void)a; return DD_OK; }
static HRESULT WINAPI D3DDev_NextViewport(void *s, void *a, void *b, void *c) { rdd_log("D3DDev[7] NextViewport"); (void)s; (void)a; (void)b; (void)c; return DD_OK; }
static HRESULT WINAPI D3DDev_EnumTextureFormats(void *s, void *cb_raw, void *ctx) {
    typedef HRESULT (WINAPI *TexFmtCB)(DDPIXELFORMAT *, void *);
    TexFmtCB cb = (TexFmtCB)cb_raw;
    DDPIXELFORMAT pf;
    (void)s;
    rdd_log("D3DDev[8] EnumTextureFormats: calling callback");

    /* RGB565 (16-bit) */
    memset(&pf, 0, sizeof(pf));
    pf.dwSize = sizeof(DDPIXELFORMAT);
    pf.dwFlags = DDPF_RGB;
    pf.dwRGBBitCount = 16;
    pf.dwRBitMask = 0xF800;
    pf.dwGBitMask = 0x07E0;
    pf.dwBBitMask = 0x001F;
    if (cb(&pf, ctx) == 0) return DD_OK; /* callback returned DDENUMRET_CANCEL */

    /* ARGB1555 (16-bit with alpha) */
    memset(&pf, 0, sizeof(pf));
    pf.dwSize = sizeof(DDPIXELFORMAT);
    pf.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
    pf.dwRGBBitCount = 16;
    pf.dwRGBAlphaBitMask = 0x8000;
    pf.dwRBitMask = 0x7C00;
    pf.dwGBitMask = 0x03E0;
    pf.dwBBitMask = 0x001F;
    if (cb(&pf, ctx) == 0) return DD_OK;

    /* RGB555 (16-bit, no alpha) */
    memset(&pf, 0, sizeof(pf));
    pf.dwSize = sizeof(DDPIXELFORMAT);
    pf.dwFlags = DDPF_RGB;
    pf.dwRGBBitCount = 16;
    pf.dwRBitMask = 0x7C00;
    pf.dwGBitMask = 0x03E0;
    pf.dwBBitMask = 0x001F;
    if (cb(&pf, ctx) == 0) return DD_OK;

    /* ARGB4444 (16-bit, 4-bit alpha) */
    memset(&pf, 0, sizeof(pf));
    pf.dwSize = sizeof(DDPIXELFORMAT);
    pf.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
    pf.dwRGBBitCount = 16;
    pf.dwRGBAlphaBitMask = 0xF000;
    pf.dwRBitMask = 0x0F00;
    pf.dwGBitMask = 0x00F0;
    pf.dwBBitMask = 0x000F;
    cb(&pf, ctx);

    return DD_OK;
}
static HRESULT WINAPI D3DDev_BeginScene(void *s) { rdd_log("D3DDev[9] BeginScene"); (void)s; return DD_OK; }
static HRESULT WINAPI D3DDev_EndScene(void *s) { rdd_log("D3DDev[10] EndScene"); (void)s; return DD_OK; }
static HRESULT WINAPI D3DDev_GetDirect3D(void *s, void **out) { rdd_log("D3DDev[11] GetDirect3D"); (void)s; *out = &g_d3d; return DD_OK; }
static HRESULT WINAPI D3DDev_SetCurrentViewport(void *s, void *a) { rdd_log("D3DDev[12] SetCurrentViewport"); (void)s; (void)a; return DD_OK; }
static HRESULT WINAPI D3DDev_GetCurrentViewport(void *s, void **out) { rdd_log("D3DDev[13] GetCurrentViewport"); (void)s; *out = &g_d3dvp; return DD_OK; }
static HRESULT WINAPI D3DDev_SetRenderTarget(void *s, void *a, void *b) {
    (void)s; (void)b;
    if (a) g_render_target = (RDDSurface *)a;
    rdd_log("D3DDev[14] SetRenderTarget: rt=%p", a);
    return DD_OK;
}
static HRESULT WINAPI D3DDev_GetRenderTarget(void *s, void **out) {
    (void)s;
    if (g_render_target) {
        g_render_target->refcount++;
        *out = g_render_target;
    } else if (g_primary && g_primary->back_buffer) {
        g_primary->back_buffer->refcount++;
        *out = g_primary->back_buffer;
    } else {
        *out = NULL;
    }
    rdd_log("D3DDev[15] GetRenderTarget: rt=%p", *out);
    return DD_OK;
}
static HRESULT WINAPI D3DDev_Begin(void *s, void *a, void *b, void *c) { rdd_log("D3DDev[16] Begin"); (void)s; (void)a; (void)b; (void)c; return DD_OK; }
static HRESULT WINAPI D3DDev_BeginIndexed(void *s, void *a, void *b, void *c, void *d, void *e) { rdd_log("D3DDev[17] BeginIndexed"); (void)s; (void)a; (void)b; (void)c; (void)d; (void)e; return DD_OK; }
static HRESULT WINAPI D3DDev_Vertex(void *s, void *a) { (void)s; (void)a; return DD_OK; }
static HRESULT WINAPI D3DDev_Index(void *s, void *a) { (void)s; (void)a; return DD_OK; }
static HRESULT WINAPI D3DDev_End(void *s, void *a) { (void)s; (void)a; return DD_OK; }
static HRESULT WINAPI D3DDev_GetRenderState(void *s, void *a, void *b) { (void)s; (void)a; if (b) *(DWORD*)b = 0; return DD_OK; }
static HRESULT WINAPI D3DDev_SetRenderState(void *s, void *a, void *b) { (void)s; (void)a; (void)b; return DD_OK; }
static HRESULT WINAPI D3DDev_GetLightState(void *s, void *a, void *b) { (void)s; (void)a; if (b) *(DWORD*)b = 0; return DD_OK; }
static HRESULT WINAPI D3DDev_SetLightState(void *s, void *a, void *b) { (void)s; (void)a; (void)b; return DD_OK; }
static HRESULT WINAPI D3DDev_SetTransform(void *s, void *a, void *b) { (void)s; (void)a; (void)b; return DD_OK; }
static HRESULT WINAPI D3DDev_GetTransform(void *s, void *a, void *b) {
    /* Return identity matrix (4x4 floats) */
    (void)s; (void)a;
    if (b) {
        float *m = (float *)b;
        memset(m, 0, 16 * sizeof(float));
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }
    return DD_OK;
}
static HRESULT WINAPI D3DDev_MultiplyTransform(void *s, void *a, void *b) { (void)s; (void)a; (void)b; return DD_OK; }
static HRESULT WINAPI D3DDev_DrawPrimitive(void *s, void *a, void *b, void *c, void *d, void *e) { (void)s; (void)a; (void)b; (void)c; (void)d; (void)e; return DD_OK; }
static HRESULT WINAPI D3DDev_DrawIndexedPrimitive(void *s, void *a, void *b, void *c, void *d, void *e, void *f, void *g) { (void)s; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; return DD_OK; }
static HRESULT WINAPI D3DDev_SetClipStatus(void *s, void *a) { (void)s; (void)a; return DD_OK; }
static HRESULT WINAPI D3DDev_GetClipStatus(void *s, void *a) { (void)s; if (a) memset(a, 0, 32); return DD_OK; }
static HRESULT WINAPI D3DDev_DrawPrimitiveStrided(void *s, void *a, void *b, void *c, void *d, void *e) { (void)s; (void)a; (void)b; (void)c; (void)d; (void)e; return DD_OK; }
static HRESULT WINAPI D3DDev_DrawIndexedPrimitiveStrided(void *s, void *a, void *b, void *c, void *d, void *e, void *f, void *g) { (void)s; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; return DD_OK; }
static HRESULT WINAPI D3DDev_DrawPrimitiveVB(void *s, void *a, void *b, void *c, void *d, void *e) { (void)s; (void)a; (void)b; (void)c; (void)d; (void)e; return DD_OK; }
static HRESULT WINAPI D3DDev_DrawIndexedPrimitiveVB(void *s, void *a, void *b, void *c, void *d, void *e) { (void)s; (void)a; (void)b; (void)c; (void)d; (void)e; return DD_OK; }
static HRESULT WINAPI D3DDev_ComputeSphereVisibility(void *s, void *a, void *b, void *c, void *d, void *e) { (void)s; (void)a; (void)b; (void)c; (void)d; (void)e; return DD_OK; }
static HRESULT WINAPI D3DDev_GetTexture(void *s, void *a, void *b) { (void)s; (void)a; (void)b; return DD_OK; }
static HRESULT WINAPI D3DDev_SetTexture(void *s, void *a, void *b) { (void)s; (void)a; (void)b; return DD_OK; }
static HRESULT WINAPI D3DDev_GetTextureStageState(void *s, void *a, void *b, void *c) { (void)s; (void)a; (void)b; if (c) *(DWORD*)c = 0; return DD_OK; }
static HRESULT WINAPI D3DDev_SetTextureStageState(void *s, void *a, void *b, void *c) { (void)s; (void)a; (void)b; (void)c; return DD_OK; }
static HRESULT WINAPI D3DDev_ValidateDevice(void *s, void *a) { rdd_log("D3DDev[41] ValidateDevice"); (void)s; (void)a; return DD_OK; }

static void *g_d3ddev_vtbl[42] = {
    D3DDev_QueryInterface,         /* 0 */
    D3DDev_AddRef,                 /* 1 */
    D3DDev_Release,                /* 2 */
    D3DDev_GetCaps,                /* 3 */
    D3DDev_GetStats,               /* 4 */
    D3DDev_AddViewport,            /* 5 */
    D3DDev_DeleteViewport,         /* 6 */
    D3DDev_NextViewport,           /* 7 */
    D3DDev_EnumTextureFormats,     /* 8 */
    D3DDev_BeginScene,             /* 9 */
    D3DDev_EndScene,               /* 10 */
    D3DDev_GetDirect3D,            /* 11 */
    D3DDev_SetCurrentViewport,     /* 12 */
    D3DDev_GetCurrentViewport,     /* 13 */
    D3DDev_SetRenderTarget,        /* 14 */
    D3DDev_GetRenderTarget,        /* 15 */
    D3DDev_Begin,                  /* 16 */
    D3DDev_BeginIndexed,           /* 17 */
    D3DDev_Vertex,                 /* 18 */
    D3DDev_Index,                  /* 19 */
    D3DDev_End,                    /* 20 */
    D3DDev_GetRenderState,         /* 21 */
    D3DDev_SetRenderState,         /* 22 */
    D3DDev_GetLightState,          /* 23 */
    D3DDev_SetLightState,          /* 24 */
    D3DDev_SetTransform,           /* 25 */
    D3DDev_GetTransform,           /* 26 */
    D3DDev_MultiplyTransform,      /* 27 */
    D3DDev_DrawPrimitive,          /* 28 */
    D3DDev_DrawIndexedPrimitive,   /* 29 */
    D3DDev_SetClipStatus,          /* 30 */
    D3DDev_GetClipStatus,          /* 31 */
    D3DDev_DrawPrimitiveStrided,   /* 32 */
    D3DDev_DrawIndexedPrimitiveStrided, /* 33 */
    D3DDev_DrawPrimitiveVB,        /* 34 */
    D3DDev_DrawIndexedPrimitiveVB, /* 35 */
    D3DDev_ComputeSphereVisibility,/* 36 */
    D3DDev_GetTexture,             /* 37 */
    D3DDev_SetTexture,             /* 38 */
    D3DDev_GetTextureStageState,   /* 39 */
    D3DDev_SetTextureStageState,   /* 40 */
    D3DDev_ValidateDevice          /* 41 */
};

static D3DDevObj g_d3ddev = { g_d3ddev_vtbl };

/* ================================================================
 * Minimal IDirect3DViewport3 stub (21 methods)
 * ================================================================ */

static HRESULT WINAPI D3DVP_QI(D3DVPObj *s, REFIID r, void **o) {
    if (guid_eq(r, &MY_IID_IDirect3DViewport) || guid_eq(r, &MY_IID_IDirect3DViewport2) ||
        guid_eq(r, &MY_IID_IDirect3DViewport3) || guid_eq(r, &MY_IID_IUnknown)) {
        *o = s; rdd_log("D3DVP_QI: returning self"); return S_OK;
    }
    *o = NULL;
    rdd_log("D3DVP_QI: E_NOINTERFACE {%08lx-...}", r->Data1);
    return E_NOINTERFACE;
}
static ULONG WINAPI D3DVP_AddRef(D3DVPObj *s) { rdd_log("D3DVP_AddRef"); (void)s; return 2; }
static ULONG WINAPI D3DVP_Release(D3DVPObj *s) { rdd_log("D3DVP_Release"); (void)s; return 1; }

/* IDirect3DViewport3 vtable (21 entries):
 *  0 QI  1 AddRef  2 Release  3 Initialize(2)  4 GetViewport(2)
 *  5 SetViewport(2)  6 TransformVertices(5)  7 LightElements(3)
 *  8 SetBackground(2)  9 GetBackground(3)  10 SetBackgroundDepth(2)
 * 11 GetBackgroundDepth(3)  12 Clear(4)  13 AddLight(2)  14 DeleteLight(2)
 * 15 NextLight(4)  16 GetViewport2(2)  17 SetViewport2(2)
 * 18 SetBackgroundDepth2(2)  19 GetBackgroundDepth2(3)  20 Clear2(7)
 */
static void *g_d3dvp_vtbl[21] = {
    D3DVP_QI, D3DVP_AddRef, D3DVP_Release,
    d3dd_stub2,  /* 3  Initialize */
    d3dd_stub2,  /* 4  GetViewport */
    d3dd_stub2,  /* 5  SetViewport */
    d3dd_stub5,  /* 6  TransformVertices */
    d3dd_stub3,  /* 7  LightElements */
    d3dd_stub2,  /* 8  SetBackground */
    d3dd_stub3,  /* 9  GetBackground */
    d3dd_stub2,  /* 10 SetBackgroundDepth */
    d3dd_stub3,  /* 11 GetBackgroundDepth */
    d3dd_stub4,  /* 12 Clear */
    d3dd_stub2,  /* 13 AddLight */
    d3dd_stub2,  /* 14 DeleteLight */
    d3dd_stub4,  /* 15 NextLight */
    d3dd_stub2,  /* 16 GetViewport2 */
    d3dd_stub2,  /* 17 SetViewport2 */
    d3dd_stub2,  /* 18 SetBackgroundDepth2 */
    d3dd_stub3,  /* 19 GetBackgroundDepth2 */
    d3dd_stub7   /* 20 Clear2 */
};

static D3DVPObj g_d3dvp = { g_d3dvp_vtbl };

/* ================================================================
 * Minimal IDirect3DMaterial3 stub (8 methods)
 * 0 QI  1 AddRef  2 Release  3 SetMaterial(2)  4 GetMaterial(2)
 * 5 GetHandle(3)
 * ================================================================ */

typedef struct D3DMatObj D3DMatObj;
struct D3DMatObj { void **lpVtbl; };

static HRESULT WINAPI D3DMat_QI(D3DMatObj *s, REFIID r, void **o) { (void)s; (void)r; *o = NULL; return E_NOINTERFACE; }
static ULONG WINAPI D3DMat_AddRef(D3DMatObj *s) { (void)s; return 2; }
static ULONG WINAPI D3DMat_Release(D3DMatObj *s) { (void)s; return 1; }

static void *g_d3dmat_vtbl[6] = {
    D3DMat_QI, D3DMat_AddRef, D3DMat_Release,
    d3dd_stub2,  /* 3 SetMaterial */
    d3dd_stub2,  /* 4 GetMaterial */
    d3dd_stub3   /* 5 GetHandle */
};

static D3DMatObj g_d3dmat = { g_d3dmat_vtbl };

/* ================================================================
 * Minimal IDirect3DLight stub (7 methods)
 * 0 QI  1 AddRef  2 Release  3 Initialize(2)  4 SetLight(2)
 * 5 GetLight(2)
 * ================================================================ */

typedef struct D3DLightObj D3DLightObj;
struct D3DLightObj { void **lpVtbl; };

static HRESULT WINAPI D3DLight_QI(D3DLightObj *s, REFIID r, void **o) { (void)s; (void)r; *o = NULL; return E_NOINTERFACE; }
static ULONG WINAPI D3DLight_AddRef(D3DLightObj *s) { (void)s; return 2; }
static ULONG WINAPI D3DLight_Release(D3DLightObj *s) { (void)s; return 1; }

static void *g_d3dlight_vtbl[6] = {
    D3DLight_QI, D3DLight_AddRef, D3DLight_Release,
    d3dd_stub2,  /* 3 Initialize */
    d3dd_stub2,  /* 4 SetLight */
    d3dd_stub2   /* 5 GetLight */
};

static D3DLightObj g_d3dlight = { g_d3dlight_vtbl };

/* IDirect3D3 methods */
/* GUID for RGB software rasterizer (standard DirectX GUID) */
static const GUID MY_IID_IDirect3DRGBDevice =
    {0xA4665C60,0x2673,0x11CF,{0xA3,0x1A,0x00,0xAA,0x00,0xB9,0x33,0x56}};

static HRESULT WINAPI D3D_EnumDevices(D3DObj *s, void *cb_raw, void *ctx) {
    /* Call the callback with a software RGB rasterizer device.
     * The game needs this to initialize its 3D renderer. */
    typedef HRESULT (WINAPI *EnumDevCB)(GUID*, char*, char*, void*, void*, void*);
    EnumDevCB cb = (EnumDevCB)cb_raw;
    char hal_desc[252];
    char hel_desc[252];
    (void)s;
    fill_d3d_device_desc(hal_desc);
    fill_d3d_device_desc(hel_desc);
    rdd_log("D3D_EnumDevices: calling callback with RGB device");
    if (cb) cb((GUID*)&MY_IID_IDirect3DRGBDevice,
               "RGB Emulation", "Direct3D RGB Software Emulation",
               hal_desc, hel_desc, ctx);
    return DD_OK;
}
static HRESULT WINAPI D3D_CreateLight(D3DObj *s, void **l, void *o) { rdd_log("D3D_CreateLight"); (void)s; (void)o; *l = &g_d3dlight; return DD_OK; }
static HRESULT WINAPI D3D_CreateMaterial(D3DObj *s, void **m, void *o) { rdd_log("D3D_CreateMaterial"); (void)s; (void)o; *m = &g_d3dmat; return DD_OK; }
static HRESULT WINAPI D3D_CreateViewport(D3DObj *s, void **v, void *o) {
    (void)s; (void)o;
    rdd_log("D3D_CreateViewport: vp=%p vtbl=%p", (void*)&g_d3dvp, (void*)g_d3dvp.lpVtbl);
    *v = &g_d3dvp;
    return DD_OK;
}
static HRESULT WINAPI D3D_FindDevice(D3DObj *s, void *search, void *result) { rdd_log("D3D_FindDevice"); (void)s; (void)search; (void)result; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI D3D_CreateDevice(D3DObj *s, REFCLSID c, void *surf, void **dev, void *o) {
    (void)s; (void)c; (void)o;
    if (surf) g_render_target = (RDDSurface *)surf;
    rdd_log("D3D_CreateDevice: returning stub device, rt=%p", surf);
    *dev = &g_d3ddev;
    return DD_OK;
}
static HRESULT WINAPI D3D_CreateVertexBuffer(D3DObj *s, void *desc, void **vb, DWORD f, void *o) { rdd_log("D3D_CreateVertexBuffer"); (void)s; (void)desc; *vb = NULL; (void)f; (void)o; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI D3D_EnumZBufferFormats(D3DObj *s, REFCLSID c, void *cb, void *ctx) { rdd_log("D3D_EnumZBufferFormats"); (void)s; (void)c; (void)cb; (void)ctx; return DD_OK; }
static HRESULT WINAPI D3D_EvictManagedTextures(D3DObj *s) { rdd_log("D3D_EvictManagedTextures"); (void)s; return DD_OK; }

struct D3DVtbl {
    HRESULT (WINAPI *QueryInterface)(D3DObj*, REFIID, void**);
    ULONG   (WINAPI *AddRef)(D3DObj*);
    ULONG   (WINAPI *Release)(D3DObj*);
    HRESULT (WINAPI *EnumDevices)(D3DObj*, void*, void*);
    HRESULT (WINAPI *CreateLight)(D3DObj*, void**, void*);
    HRESULT (WINAPI *CreateMaterial)(D3DObj*, void**, void*);
    HRESULT (WINAPI *CreateViewport)(D3DObj*, void**, void*);
    HRESULT (WINAPI *FindDevice)(D3DObj*, void*, void*);
    HRESULT (WINAPI *CreateDevice)(D3DObj*, REFCLSID, void*, void**, void*);
    HRESULT (WINAPI *CreateVertexBuffer)(D3DObj*, void*, void**, DWORD, void*);
    HRESULT (WINAPI *EnumZBufferFormats)(D3DObj*, REFCLSID, void*, void*);
    HRESULT (WINAPI *EvictManagedTextures)(D3DObj*);
};

static D3DVtbl g_d3d_vtbl = {
    D3D_QueryInterface, D3D_AddRef, D3D_Release,
    D3D_EnumDevices, D3D_CreateLight, D3D_CreateMaterial,
    D3D_CreateViewport, D3D_FindDevice, D3D_CreateDevice,
    D3D_CreateVertexBuffer, D3D_EnumZBufferFormats, D3D_EvictManagedTextures
};

static D3DObj g_d3d = { &g_d3d_vtbl };

static HRESULT WINAPI D3D_QueryInterface(D3DObj *self, REFIID riid, void **out) {
    (void)self;
    if (guid_eq(riid, &MY_IID_IDirect3D) || guid_eq(riid, &MY_IID_IDirect3D2) ||
        guid_eq(riid, &MY_IID_IDirect3D3)) {
        g_dd_refcount++;
        *out = &g_d3d;
        return S_OK;
    }
    if (guid_eq(riid, &MY_IID_IDirectDraw4)) {
        g_dd_refcount++;
        *out = &g_dd4;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

/* ================================================================
 * IDirectDraw4 methods (28 total)
 * ================================================================ */

static HRESULT WINAPI DD4_QueryInterface(DD4Obj *self, REFIID riid, void **out) {
    (void)self;
    if (guid_eq(riid, &MY_IID_IDirectDraw) || guid_eq(riid, &MY_IID_IUnknown)) {
        g_dd_refcount++;
        *out = &g_dd1;
        rdd_log("DD4_QI: returning IDirectDraw");
        return S_OK;
    }
    if (guid_eq(riid, &MY_IID_IDirectDraw2) || guid_eq(riid, &MY_IID_IDirectDraw4)) {
        g_dd_refcount++;
        *out = &g_dd4;
        rdd_log("DD4_QI: returning IDirectDraw4");
        return S_OK;
    }
    if (guid_eq(riid, &MY_IID_IDirect3D) || guid_eq(riid, &MY_IID_IDirect3D2) ||
        guid_eq(riid, &MY_IID_IDirect3D3)) {
        g_dd_refcount++;
        *out = &g_d3d;
        rdd_log("DD4_QI: returning IDirect3D3 stub");
        return S_OK;
    }
    *out = NULL;
    rdd_log("DD4_QI: E_NOINTERFACE {%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
            riid->Data1, riid->Data2, riid->Data3,
            riid->Data4[0], riid->Data4[1], riid->Data4[2], riid->Data4[3],
            riid->Data4[4], riid->Data4[5], riid->Data4[6], riid->Data4[7]);
    return E_NOINTERFACE;
}

static ULONG WINAPI DD4_AddRef(DD4Obj *self) {
    (void)self;
    return ++g_dd_refcount;
}

static ULONG WINAPI DD4_Release(DD4Obj *self) {
    (void)self;
    if (g_dd_refcount > 0) g_dd_refcount--;
    return g_dd_refcount;
}

/* ================================================================
 * Minimal IDirectDrawClipper stub
 * ================================================================ */

typedef struct RDDClipper RDDClipper;
typedef struct RDDClipperVtbl RDDClipperVtbl;

struct RDDClipper {
    RDDClipperVtbl *lpVtbl;
    ULONG refcount;
    HWND hwnd;
};

static HRESULT WINAPI Clip_QueryInterface(RDDClipper *s, REFIID r, void **o)
    { (void)s; (void)r; *o = NULL; return E_NOINTERFACE; }
static ULONG WINAPI Clip_AddRef(RDDClipper *s) { return ++s->refcount; }
static ULONG WINAPI Clip_Release(RDDClipper *s) {
    ULONG ref = --s->refcount;
    if (ref == 0) free(s);
    return ref;
}
static HRESULT WINAPI Clip_GetClipList(RDDClipper *s, LPRECT r, void *rgn, DWORD *sz)
    { (void)s; (void)r; (void)rgn; (void)sz; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI Clip_GetHWnd(RDDClipper *s, HWND *h)
    { *h = s->hwnd; return DD_OK; }
static HRESULT WINAPI Clip_Initialize(RDDClipper *s, void *dd, DWORD f)
    { (void)s; (void)dd; (void)f; return DD_OK; }
static HRESULT WINAPI Clip_IsClipListChanged(RDDClipper *s, BOOL *changed)
    { (void)s; *changed = FALSE; return DD_OK; }
static HRESULT WINAPI Clip_SetClipList(RDDClipper *s, void *rgn, DWORD f)
    { (void)s; (void)rgn; (void)f; return DD_OK; }
static HRESULT WINAPI Clip_SetHWnd(RDDClipper *s, DWORD f, HWND h)
    { (void)f; s->hwnd = h; return DD_OK; }

struct RDDClipperVtbl {
    HRESULT (WINAPI *QueryInterface)(RDDClipper*, REFIID, void**);
    ULONG   (WINAPI *AddRef)(RDDClipper*);
    ULONG   (WINAPI *Release)(RDDClipper*);
    HRESULT (WINAPI *GetClipList)(RDDClipper*, LPRECT, void*, DWORD*);
    HRESULT (WINAPI *GetHWnd)(RDDClipper*, HWND*);
    HRESULT (WINAPI *Initialize)(RDDClipper*, void*, DWORD);
    HRESULT (WINAPI *IsClipListChanged)(RDDClipper*, BOOL*);
    HRESULT (WINAPI *SetClipList)(RDDClipper*, void*, DWORD);
    HRESULT (WINAPI *SetHWnd)(RDDClipper*, DWORD, HWND);
};

static RDDClipperVtbl g_clip_vtbl = {
    Clip_QueryInterface, Clip_AddRef, Clip_Release,
    Clip_GetClipList, Clip_GetHWnd, Clip_Initialize,
    Clip_IsClipListChanged, Clip_SetClipList, Clip_SetHWnd
};

/* DD4 stubs */
static HRESULT WINAPI DD4_Compact(DD4Obj *s)
    { (void)s; return DD_OK; }
static HRESULT WINAPI DD4_CreateClipper(DD4Obj *s, DWORD f, void **c, void *o) {
    RDDClipper *clip;
    (void)s; (void)f; (void)o;
    clip = (RDDClipper *)calloc(1, sizeof(RDDClipper));
    if (!clip) { *c = NULL; return DDERR_OUTOFMEMORY; }
    clip->lpVtbl = &g_clip_vtbl;
    clip->refcount = 1;
    clip->hwnd = g_hwnd;
    *c = clip;
    rdd_log("DD4_CreateClipper: created");
    return DD_OK;
}
static HRESULT WINAPI DD4_CreatePalette(DD4Obj *s, DWORD f, void *e, void **p, void *o)
    { (void)s; (void)f; (void)e; (void)o; *p = NULL; rdd_log("DD4_CreatePalette: unsupported"); return DDERR_UNSUPPORTED; }
static HRESULT WINAPI DD4_DuplicateSurface(DD4Obj *s, RDDSurface *a, RDDSurface **b)
    { rdd_log("DD4_DuplicateSurface: UNSUPPORTED"); (void)s; (void)a; *b = NULL; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI DD4_EnumSurfaces(DD4Obj *s, DWORD f, LPDDSURFACEDESC2 d, void *c, void *cb)
    { rdd_log("DD4_EnumSurfaces called"); (void)s; (void)f; (void)d; (void)c; (void)cb; return DD_OK; }
static HRESULT WINAPI DD4_FlipToGDISurface(DD4Obj *s)
    { (void)s; return DD_OK; }
static HRESULT WINAPI DD4_GetFourCCCodes(DD4Obj *s, DWORD *n, DWORD *c)
    { (void)s; (void)c; *n = 0; return DD_OK; }
static HRESULT WINAPI DD4_GetGDISurface(DD4Obj *s, RDDSurface **surf)
    { (void)s; *surf = NULL; return DDERR_NOTFOUND; }
static HRESULT WINAPI DD4_GetScanLine(DD4Obj *s, DWORD *sl)
    { (void)s; *sl = 0; return DD_OK; }
static HRESULT WINAPI DD4_Initialize(DD4Obj *s, GUID *g)
    { (void)s; (void)g; return DDERR_ALREADYINITIALIZED; }
static HRESULT WINAPI DD4_WaitForVerticalBlank(DD4Obj *s, DWORD f, HANDLE h)
    { (void)s; (void)f; (void)h; return DD_OK; }
static HRESULT WINAPI DD4_GetSurfaceFromDC(DD4Obj *s, HDC h, RDDSurface **surf)
    { (void)s; (void)h; *surf = NULL; return DDERR_NOTFOUND; }
static HRESULT WINAPI DD4_RestoreAllSurfaces(DD4Obj *s)
    { (void)s; return DD_OK; }
static HRESULT WINAPI DD4_TestCooperativeLevel(DD4Obj *s)
    { (void)s; return DD_OK; }

/* DD4 real implementations */

static HRESULT WINAPI DD4_GetCaps(DD4Obj *self, LPDDCAPS hal, LPDDCAPS hel) {
    (void)self;
    if (hal) {
        memset(hal, 0, sizeof(DDCAPS));
        hal->dwSize = sizeof(DDCAPS);
        hal->dwCaps = DDCAPS_BLT | DDCAPS_BLTCOLORFILL | DDCAPS_COLORKEY;
        hal->dwCKeyCaps = DDCKEYCAPS_SRCBLT;
        hal->dwVidMemTotal = 64 * 1024 * 1024;
        hal->dwVidMemFree  = 64 * 1024 * 1024;
    }
    if (hel) {
        memset(hel, 0, sizeof(DDCAPS));
        hel->dwSize = sizeof(DDCAPS);
        hel->dwCaps = DDCAPS_BLT | DDCAPS_BLTCOLORFILL | DDCAPS_COLORKEY;
        hel->dwCKeyCaps = DDCKEYCAPS_SRCBLT;
    }
    return DD_OK;
}

static HRESULT WINAPI DD4_GetDisplayMode(DD4Obj *self, LPDDSURFACEDESC2 desc) {
    (void)self;
    memset(desc, 0, sizeof(DDSURFACEDESC2));
    desc->dwSize = sizeof(DDSURFACEDESC2);
    desc->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
    desc->dwWidth = 640;
    desc->dwHeight = 480;
    desc->dwRefreshRate = 60;
    fill_pixelformat_rgb565(&desc->ddpfPixelFormat);
    return DD_OK;
}

static HRESULT WINAPI DD4_GetMonitorFrequency(DD4Obj *self, DWORD *freq) {
    (void)self;
    *freq = 60;
    return DD_OK;
}

static HRESULT WINAPI DD4_GetVerticalBlankStatus(DD4Obj *self, BOOL *status) {
    (void)self;
    *status = TRUE;
    return DD_OK;
}

static HRESULT WINAPI DD4_GetAvailableVidMem(DD4Obj *self, LPDDSCAPS2 caps, DWORD *total, DWORD *free_mem) {
    (void)self; (void)caps;
    if (total) *total = 64 * 1024 * 1024;
    if (free_mem) *free_mem = 64 * 1024 * 1024;
    return DD_OK;
}

static HRESULT WINAPI DD4_GetDeviceIdentifier(DD4Obj *self, void *di_raw, DWORD flags) {
    DDDEVICEIDENTIFIER *di = (DDDEVICEIDENTIFIER *)di_raw;
    (void)self; (void)flags;
    memset(di, 0, sizeof(DDDEVICEIDENTIFIER));
    strncpy(di->szDescription, "Revenant DDraw", sizeof(di->szDescription) - 1);
    strncpy(di->szDriver, "revenant_ddraw.dll", sizeof(di->szDriver) - 1);
    return DD_OK;
}

static HRESULT WINAPI DD4_RestoreDisplayMode(DD4Obj *self) {
    (void)self;
    rdd_log("DD4_RestoreDisplayMode");
    restore_window();
    return DD_OK;
}

static HRESULT WINAPI DD4_SetCooperativeLevel(DD4Obj *self, HWND hwnd, DWORD flags) {
    (void)self;
    rdd_log("DD4_SetCooperativeLevel: hwnd=%p flags=0x%lx", (void*)hwnd, flags);
    g_hwnd = hwnd;

    /* DDSCL_NORMAL (0x8): game is leaving exclusive mode (e.g. during exit) */
    if (flags & DDSCL_NORMAL)
        restore_window();

    return DD_OK;
}

static void restore_window(void) {
    if (!g_is_fullscreen || !g_hwnd) return;
    g_is_fullscreen = FALSE;
    SetWindowPos(g_hwnd, HWND_NOTOPMOST,
                 g_orig_rect.left, g_orig_rect.top,
                 g_orig_rect.right - g_orig_rect.left,
                 g_orig_rect.bottom - g_orig_rect.top,
                 SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    rdd_log("restore_window: restored to %ldx%ld",
            g_orig_rect.right - g_orig_rect.left,
            g_orig_rect.bottom - g_orig_rect.top);
}

static void setup_fullscreen_window(void) {
    int screen_w, screen_h;

    if (!g_hwnd) return;

    /* Save original window rect for restoration on exit */
    GetWindowRect(g_hwnd, &g_orig_rect);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);

    /* Just resize the window to fill the screen without changing its style.
     * Changing to WS_POPUP causes Wine/Porting Kit to intercept Esc as
     * "exit fullscreen", which kills the game. */
    SetWindowPos(g_hwnd, HWND_TOP, 0, 0, screen_w, screen_h,
                 SWP_SHOWWINDOW);

    g_is_fullscreen = TRUE;
    rdd_log("setup_fullscreen_window: %dx%d (saved orig %ldx%ld)",
            screen_w, screen_h,
            g_orig_rect.right - g_orig_rect.left,
            g_orig_rect.bottom - g_orig_rect.top);
}

static HRESULT WINAPI DD4_SetDisplayMode(DD4Obj *self, DWORD w, DWORD h, DWORD bpp,
                                          DWORD refresh, DWORD flags) {
    (void)self; (void)refresh; (void)flags;
    rdd_log("DD4_SetDisplayMode: %lux%lux%lu (accepted, no mode change)", w, h, bpp);

    /* Don't actually change the display mode. Just set up the window. */
    setup_fullscreen_window();

    return DD_OK;
}

static HRESULT WINAPI DD4_EnumDisplayModes(DD4Obj *self, DWORD flags, LPDDSURFACEDESC2 filter,
                                            void *ctx, void *cb_raw) {
    typedef HRESULT (WINAPI *EnumCB)(DDSURFACEDESC2 *, void *);
    EnumCB cb = (EnumCB)cb_raw;
    DDSURFACEDESC2 mode;
    (void)self; (void)flags; (void)filter;

    memset(&mode, 0, sizeof(mode));
    mode.dwSize = sizeof(mode);
    mode.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
    mode.dwWidth = 640;
    mode.dwHeight = 480;
    mode.dwRefreshRate = 60;
    fill_pixelformat_rgb565(&mode.ddpfPixelFormat);

    cb(&mode, ctx);
    return DD_OK;
}

static HRESULT WINAPI DD4_CreateSurface(DD4Obj *self, LPDDSURFACEDESC2 desc,
                                         RDDSurface **out, void *outer) {
    DWORD caps;
    (void)self; (void)outer;

    caps = desc->ddsCaps.dwCaps;
    rdd_log("DD4_CreateSurface: flags=0x%lx caps=0x%lx w=%lu h=%lu backbuf=%lu",
            desc->dwFlags, caps, desc->dwWidth, desc->dwHeight, desc->dwBackBufferCount);

    if (caps & DDSCAPS_PRIMARYSURFACE) {
        RDDSurface *primary = alloc_surface(SURF_PRIMARY, 640, 480, 16, NULL, 0);
        RDDSurface *backbuf = alloc_surface(SURF_BACKBUFFER, 640, 480, 16, NULL, 0);
        if (!primary || !backbuf) {
            if (primary) { free(primary->pixels); free(primary); }
            if (backbuf) { free(backbuf->pixels); free(backbuf); }
            return DDERR_OUTOFMEMORY;
        }
        primary->back_buffer = backbuf;
        g_primary = primary;
        *out = primary;
        rdd_log("DD4_CreateSurface: created primary + backbuffer");
        return DD_OK;
    }

    if ((caps & DDSCAPS_OFFSCREENPLAIN) || (caps & DDSCAPS_ZBUFFER) ||
        (caps & DDSCAPS_TEXTURE) || (caps & DDSCAPS_3DDEVICE)) {
        DWORD w = desc->dwWidth;
        DWORD h = desc->dwHeight;
        DWORD bpp = 16;
        const char *kind = (caps & DDSCAPS_ZBUFFER) ? "zbuffer"
                         : (caps & DDSCAPS_TEXTURE) ? "texture"
                         : (caps & DDSCAPS_3DDEVICE) ? "3ddevice"
                         : "offscreen";
        if (w == 0) w = 640;
        if (h == 0) h = 480;
        /* Use the requested pixel format's bit depth */
        if (desc->dwFlags & DDSD_PIXELFORMAT) {
            DDPIXELFORMAT *pf = &desc->ddpfPixelFormat;
            if (pf->dwRGBBitCount > 0)
                bpp = pf->dwRGBBitCount;
            else if (pf->dwZBufferBitDepth > 0)
                bpp = pf->dwZBufferBitDepth;
            else if (pf->dwFlags & DDPF_PALETTEINDEXED8)
                bpp = 8;
            else if (pf->dwFlags & DDPF_PALETTEINDEXED4)
                bpp = 4;
            rdd_log("  pixfmt: flags=0x%lx bpp=%lu R=0x%lx G=0x%lx B=0x%lx A=0x%lx",
                    pf->dwFlags, bpp, pf->dwRBitMask, pf->dwGBitMask,
                    pf->dwBBitMask, pf->dwRGBAlphaBitMask);
        }
        {
        DDPIXELFORMAT *fmt = (desc->dwFlags & DDSD_PIXELFORMAT) ? &desc->ddpfPixelFormat : NULL;
        *out = alloc_surface(SURF_OFFSCREEN, w, h, bpp, fmt, caps);
        }
        if (!*out) return DDERR_OUTOFMEMORY;
        rdd_log("DD4_CreateSurface: created %s %lux%lux%lu", kind, w, h, bpp);
        return DD_OK;
    }

    rdd_log("DD4_CreateSurface: unhandled caps=0x%lx", caps);
    *out = NULL;
    return DDERR_INVALIDCAPS;
}

/* ================================================================
 * DD4 vtable definition
 * ================================================================ */

struct DD4Vtbl {
    HRESULT (WINAPI *QueryInterface)(DD4Obj*, REFIID, void**);
    ULONG   (WINAPI *AddRef)(DD4Obj*);
    ULONG   (WINAPI *Release)(DD4Obj*);
    HRESULT (WINAPI *Compact)(DD4Obj*);
    HRESULT (WINAPI *CreateClipper)(DD4Obj*, DWORD, void**, void*);
    HRESULT (WINAPI *CreatePalette)(DD4Obj*, DWORD, void*, void**, void*);
    HRESULT (WINAPI *CreateSurface)(DD4Obj*, LPDDSURFACEDESC2, RDDSurface**, void*);
    HRESULT (WINAPI *DuplicateSurface)(DD4Obj*, RDDSurface*, RDDSurface**);
    HRESULT (WINAPI *EnumDisplayModes)(DD4Obj*, DWORD, LPDDSURFACEDESC2, void*, void*);
    HRESULT (WINAPI *EnumSurfaces)(DD4Obj*, DWORD, LPDDSURFACEDESC2, void*, void*);
    HRESULT (WINAPI *FlipToGDISurface)(DD4Obj*);
    HRESULT (WINAPI *GetCaps)(DD4Obj*, LPDDCAPS, LPDDCAPS);
    HRESULT (WINAPI *GetDisplayMode)(DD4Obj*, LPDDSURFACEDESC2);
    HRESULT (WINAPI *GetFourCCCodes)(DD4Obj*, DWORD*, DWORD*);
    HRESULT (WINAPI *GetGDISurface)(DD4Obj*, RDDSurface**);
    HRESULT (WINAPI *GetMonitorFrequency)(DD4Obj*, DWORD*);
    HRESULT (WINAPI *GetScanLine)(DD4Obj*, DWORD*);
    HRESULT (WINAPI *GetVerticalBlankStatus)(DD4Obj*, BOOL*);
    HRESULT (WINAPI *Initialize)(DD4Obj*, GUID*);
    HRESULT (WINAPI *RestoreDisplayMode)(DD4Obj*);
    HRESULT (WINAPI *SetCooperativeLevel)(DD4Obj*, HWND, DWORD);
    HRESULT (WINAPI *SetDisplayMode)(DD4Obj*, DWORD, DWORD, DWORD, DWORD, DWORD);
    HRESULT (WINAPI *WaitForVerticalBlank)(DD4Obj*, DWORD, HANDLE);
    HRESULT (WINAPI *GetAvailableVidMem)(DD4Obj*, LPDDSCAPS2, DWORD*, DWORD*);
    HRESULT (WINAPI *GetSurfaceFromDC)(DD4Obj*, HDC, RDDSurface**);
    HRESULT (WINAPI *RestoreAllSurfaces)(DD4Obj*);
    HRESULT (WINAPI *TestCooperativeLevel)(DD4Obj*);
    HRESULT (WINAPI *GetDeviceIdentifier)(DD4Obj*, void*, DWORD);
};

static DD4Vtbl g_dd4_vtbl = {
    DD4_QueryInterface,         /* 0 */
    DD4_AddRef,                 /* 1 */
    DD4_Release,                /* 2 */
    DD4_Compact,                /* 3 */
    DD4_CreateClipper,          /* 4 */
    DD4_CreatePalette,          /* 5 */
    DD4_CreateSurface,          /* 6 */
    DD4_DuplicateSurface,       /* 7 */
    DD4_EnumDisplayModes,       /* 8 */
    DD4_EnumSurfaces,           /* 9 */
    DD4_FlipToGDISurface,       /* 10 */
    DD4_GetCaps,                /* 11 */
    DD4_GetDisplayMode,         /* 12 */
    DD4_GetFourCCCodes,         /* 13 */
    DD4_GetGDISurface,          /* 14 */
    DD4_GetMonitorFrequency,    /* 15 */
    DD4_GetScanLine,            /* 16 */
    DD4_GetVerticalBlankStatus, /* 17 */
    DD4_Initialize,             /* 18 */
    DD4_RestoreDisplayMode,     /* 19 */
    DD4_SetCooperativeLevel,    /* 20 */
    DD4_SetDisplayMode,         /* 21 */
    DD4_WaitForVerticalBlank,   /* 22 */
    DD4_GetAvailableVidMem,     /* 23 */
    DD4_GetSurfaceFromDC,       /* 24 */
    DD4_RestoreAllSurfaces,     /* 25 */
    DD4_TestCooperativeLevel,   /* 26 */
    DD4_GetDeviceIdentifier     /* 27 */
};

/* ================================================================
 * IDirectDraw v1 methods (23 total, thin wrapper for QI upgrade)
 * ================================================================ */

static HRESULT WINAPI DD1_QueryInterface(DD1Obj *self, REFIID riid, void **out) {
    (void)self;
    if (guid_eq(riid, &MY_IID_IDirectDraw) || guid_eq(riid, &MY_IID_IUnknown)) {
        g_dd_refcount++;
        *out = &g_dd1;
        rdd_log("DD1_QI: returning IDirectDraw");
        return S_OK;
    }
    if (guid_eq(riid, &MY_IID_IDirectDraw2) || guid_eq(riid, &MY_IID_IDirectDraw4)) {
        g_dd_refcount++;
        *out = &g_dd4;
        rdd_log("DD1_QI: returning IDirectDraw4");
        return S_OK;
    }
    if (guid_eq(riid, &MY_IID_IDirect3D) || guid_eq(riid, &MY_IID_IDirect3D2) ||
        guid_eq(riid, &MY_IID_IDirect3D3)) {
        g_dd_refcount++;
        *out = &g_d3d;
        rdd_log("DD1_QI: returning IDirect3D3 stub");
        return S_OK;
    }
    *out = NULL;
    rdd_log("DD1_QI: E_NOINTERFACE {%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
            riid->Data1, riid->Data2, riid->Data3,
            riid->Data4[0], riid->Data4[1], riid->Data4[2], riid->Data4[3],
            riid->Data4[4], riid->Data4[5], riid->Data4[6], riid->Data4[7]);
    return E_NOINTERFACE;
}

static ULONG WINAPI DD1_AddRef(DD1Obj *self) { (void)self; return ++g_dd_refcount; }
static ULONG WINAPI DD1_Release(DD1Obj *self) {
    (void)self;
    if (g_dd_refcount > 0) g_dd_refcount--;
    return g_dd_refcount;
}

/* DD1 stubs - these use void* for v1-specific param types we don't implement */
static HRESULT WINAPI DD1_Compact(DD1Obj *s) { (void)s; return DD_OK; }
static HRESULT WINAPI DD1_CreateClipper(DD1Obj *s, DWORD f, void **c, void *o)
    { (void)s; (void)f; (void)o; *c = NULL; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI DD1_CreatePalette(DD1Obj *s, DWORD f, void *e, void **p, void *o)
    { (void)s; (void)f; (void)e; (void)o; *p = NULL; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI DD1_CreateSurface_v1(DD1Obj *s, void *desc, void **out, void *o)
    { (void)s; (void)desc; (void)o; *out = NULL; rdd_log("DD1_CreateSurface_v1: not implemented"); return DDERR_UNSUPPORTED; }
static HRESULT WINAPI DD1_DuplicateSurface(DD1Obj *s, void *a, void **b)
    { (void)s; (void)a; *b = NULL; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI DD1_EnumDisplayModes(DD1Obj *s, DWORD f, void *d, void *c, void *cb)
    { (void)s; (void)f; (void)d; (void)c; (void)cb; return DD_OK; }
static HRESULT WINAPI DD1_EnumSurfaces(DD1Obj *s, DWORD f, void *d, void *c, void *cb)
    { (void)s; (void)f; (void)d; (void)c; (void)cb; return DD_OK; }
static HRESULT WINAPI DD1_FlipToGDISurface(DD1Obj *s) { (void)s; return DD_OK; }
static HRESULT WINAPI DD1_GetCaps(DD1Obj *s, LPDDCAPS h, LPDDCAPS e)
    { (void)s; return DD4_GetCaps(NULL, h, e); }
static HRESULT WINAPI DD1_GetDisplayMode_v1(DD1Obj *s, void *desc) {
    /* Game shouldn't call this on v1 interface, but handle it */
    DDSURFACEDESC *d = (DDSURFACEDESC *)desc;
    (void)s;
    if (d) {
        memset(d, 0, sizeof(DDSURFACEDESC));
        d->dwSize = sizeof(DDSURFACEDESC);
        d->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        d->dwWidth = 640;
        d->dwHeight = 480;
        fill_pixelformat_rgb565(&d->ddpfPixelFormat);
    }
    return DD_OK;
}
static HRESULT WINAPI DD1_GetFourCCCodes(DD1Obj *s, DWORD *n, DWORD *c)
    { (void)s; (void)c; *n = 0; return DD_OK; }
static HRESULT WINAPI DD1_GetGDISurface(DD1Obj *s, void **surf)
    { (void)s; *surf = NULL; return DDERR_NOTFOUND; }
static HRESULT WINAPI DD1_GetMonitorFrequency(DD1Obj *s, DWORD *f)
    { (void)s; *f = 60; return DD_OK; }
static HRESULT WINAPI DD1_GetScanLine(DD1Obj *s, DWORD *sl)
    { (void)s; *sl = 0; return DD_OK; }
static HRESULT WINAPI DD1_GetVerticalBlankStatus(DD1Obj *s, BOOL *st)
    { (void)s; *st = TRUE; return DD_OK; }
static HRESULT WINAPI DD1_Initialize(DD1Obj *s, GUID *g)
    { (void)s; (void)g; return DDERR_ALREADYINITIALIZED; }
static HRESULT WINAPI DD1_RestoreDisplayMode(DD1Obj *s) { (void)s; return DD_OK; }

static HRESULT WINAPI DD1_SetCooperativeLevel(DD1Obj *self, HWND hwnd, DWORD flags) {
    (void)self;
    rdd_log("DD1_SetCooperativeLevel: hwnd=%p flags=0x%lx", (void*)hwnd, flags);
    g_hwnd = hwnd;
    return DD_OK;
}

static HRESULT WINAPI DD1_SetDisplayMode_v1(DD1Obj *self, DWORD w, DWORD h, DWORD bpp) {
    (void)self;
    rdd_log("DD1_SetDisplayMode_v1: %lux%lux%lu", w, h, bpp);
    setup_fullscreen_window();
    return DD_OK;
}

static HRESULT WINAPI DD1_WaitForVerticalBlank(DD1Obj *s, DWORD f, HANDLE h)
    { (void)s; (void)f; (void)h; return DD_OK; }

/* ================================================================
 * DD1 vtable definition
 * ================================================================ */

struct DD1Vtbl {
    HRESULT (WINAPI *QueryInterface)(DD1Obj*, REFIID, void**);
    ULONG   (WINAPI *AddRef)(DD1Obj*);
    ULONG   (WINAPI *Release)(DD1Obj*);
    HRESULT (WINAPI *Compact)(DD1Obj*);
    HRESULT (WINAPI *CreateClipper)(DD1Obj*, DWORD, void**, void*);
    HRESULT (WINAPI *CreatePalette)(DD1Obj*, DWORD, void*, void**, void*);
    HRESULT (WINAPI *CreateSurface_v1)(DD1Obj*, void*, void**, void*);
    HRESULT (WINAPI *DuplicateSurface)(DD1Obj*, void*, void**);
    HRESULT (WINAPI *EnumDisplayModes)(DD1Obj*, DWORD, void*, void*, void*);
    HRESULT (WINAPI *EnumSurfaces)(DD1Obj*, DWORD, void*, void*, void*);
    HRESULT (WINAPI *FlipToGDISurface)(DD1Obj*);
    HRESULT (WINAPI *GetCaps)(DD1Obj*, LPDDCAPS, LPDDCAPS);
    HRESULT (WINAPI *GetDisplayMode_v1)(DD1Obj*, void*);
    HRESULT (WINAPI *GetFourCCCodes)(DD1Obj*, DWORD*, DWORD*);
    HRESULT (WINAPI *GetGDISurface)(DD1Obj*, void**);
    HRESULT (WINAPI *GetMonitorFrequency)(DD1Obj*, DWORD*);
    HRESULT (WINAPI *GetScanLine)(DD1Obj*, DWORD*);
    HRESULT (WINAPI *GetVerticalBlankStatus)(DD1Obj*, BOOL*);
    HRESULT (WINAPI *Initialize)(DD1Obj*, GUID*);
    HRESULT (WINAPI *RestoreDisplayMode)(DD1Obj*);
    HRESULT (WINAPI *SetCooperativeLevel)(DD1Obj*, HWND, DWORD);
    HRESULT (WINAPI *SetDisplayMode_v1)(DD1Obj*, DWORD, DWORD, DWORD);
    HRESULT (WINAPI *WaitForVerticalBlank)(DD1Obj*, DWORD, HANDLE);
};

static DD1Vtbl g_dd1_vtbl = {
    DD1_QueryInterface,         /* 0 */
    DD1_AddRef,                 /* 1 */
    DD1_Release,                /* 2 */
    DD1_Compact,                /* 3 */
    DD1_CreateClipper,          /* 4 */
    DD1_CreatePalette,          /* 5 */
    DD1_CreateSurface_v1,       /* 6 */
    DD1_DuplicateSurface,       /* 7 */
    DD1_EnumDisplayModes,       /* 8 */
    DD1_EnumSurfaces,           /* 9 */
    DD1_FlipToGDISurface,       /* 10 */
    DD1_GetCaps,                /* 11 */
    DD1_GetDisplayMode_v1,      /* 12 */
    DD1_GetFourCCCodes,         /* 13 */
    DD1_GetGDISurface,          /* 14 */
    DD1_GetMonitorFrequency,    /* 15 */
    DD1_GetScanLine,            /* 16 */
    DD1_GetVerticalBlankStatus, /* 17 */
    DD1_Initialize,             /* 18 */
    DD1_RestoreDisplayMode,     /* 19 */
    DD1_SetCooperativeLevel,    /* 20 */
    DD1_SetDisplayMode_v1,      /* 21 */
    DD1_WaitForVerticalBlank    /* 22 */
};

/* ================================================================
 * DLL Exports
 * ================================================================ */

HRESULT WINAPI DirectDrawCreate(GUID *driver, LPDIRECTDRAW *ddraw, IUnknown *outer) {
    (void)driver; (void)outer;
    rdd_log("DirectDrawCreate called");

    if (!g_initialized) {
        init_lut();
        init_bmi();
        g_present_buf = (DWORD *)malloc(640 * 480 * sizeof(DWORD));
        g_dd4.lpVtbl = &g_dd4_vtbl;
        g_dd1.lpVtbl = &g_dd1_vtbl;
        g_initialized = TRUE;
    }

    g_dd_refcount++;
    *ddraw = (LPDIRECTDRAW)(void *)&g_dd1;
    return DD_OK;
}

HRESULT WINAPI DirectDrawCreateEx(GUID *driver, void **ddraw, REFIID iid, IUnknown *outer) {
    (void)driver; (void)outer;
    rdd_log("DirectDrawCreateEx called");

    if (!g_initialized) {
        init_lut();
        init_bmi();
        g_present_buf = (DWORD *)malloc(640 * 480 * sizeof(DWORD));
        g_dd4.lpVtbl = &g_dd4_vtbl;
        g_dd1.lpVtbl = &g_dd1_vtbl;
        g_initialized = TRUE;
    }

    g_dd_refcount++;
    if (guid_eq(iid, &MY_IID_IDirectDraw4)) {
        *ddraw = &g_dd4;
    } else {
        *ddraw = &g_dd1;
    }
    return DD_OK;
}

HRESULT WINAPI DirectDrawCreateClipper(DWORD flags, LPDIRECTDRAWCLIPPER *clipper, IUnknown *outer) {
    (void)flags; (void)outer;
    rdd_log("DirectDrawCreateClipper called (not supported)");
    *clipper = NULL;
    return DDERR_UNSUPPORTED;
}

HRESULT WINAPI DirectDrawEnumerateA(LPDDENUMCALLBACKA cb, void *ctx) {
    rdd_log("DirectDrawEnumerateA called");
    if (cb) cb(NULL, "Revenant DDraw", "display", ctx);
    return DD_OK;
}

HRESULT WINAPI DirectDrawEnumerateExA(LPDDENUMCALLBACKEXA cb, void *ctx, DWORD flags) {
    (void)flags;
    rdd_log("DirectDrawEnumerateExA called");
    if (cb) cb(NULL, "Revenant DDraw", "display", ctx, NULL);
    return DD_OK;
}

HRESULT WINAPI DirectDrawEnumerateW(LPDDENUMCALLBACKW cb, void *ctx) {
    rdd_log("DirectDrawEnumerateW called");
    if (cb) cb(NULL, L"Revenant DDraw", L"display", ctx);
    return DD_OK;
}

HRESULT WINAPI DirectDrawEnumerateExW(LPDDENUMCALLBACKEXW cb, void *ctx, DWORD flags) {
    (void)flags;
    rdd_log("DirectDrawEnumerateExW called");
    if (cb) cb(NULL, L"Revenant DDraw", L"display", ctx, NULL);
    return DD_OK;
}

/* COM class factory stubs (required by some DLL loaders) */
HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }
HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **out) {
    (void)rclsid; (void)riid;
    *out = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}

/* ================================================================
 * DllMain
 * ================================================================ */

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    (void)hinstDLL; (void)lpReserved;
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        rdd_log("=== revenant_ddraw loaded ===");
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        restore_window();
        rdd_log("=== revenant_ddraw unloaded ===");
        if (g_logfile) { fclose(g_logfile); g_logfile = NULL; }
        if (g_present_buf) { free(g_present_buf); g_present_buf = NULL; }
    }
    return TRUE;
}

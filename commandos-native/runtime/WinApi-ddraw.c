/*
 *  Commandos port: software DirectDraw (IDirectDraw, IDirectDrawSurface,
 *  IDirectDrawPalette, IDirectDrawClipper, all version 1).
 *
 *  Surfaces are plain memory in the 32-bit guest address space, so the
 *  game can lock them and draw directly. The primary surface is shown in an
 *  SDL window: on Flip, on a Blt to the primary surface and on Unlock of
 *  the primary surface, its pixels are converted to 32-bit and presented,
 *  scaled to the window with the aspect ratio kept.
 *
 *  The COM glue (vtables, *_asm2c procedures) is in
 *  llasm/WinApi-ddraw-asm.llasm (from the SR Septerra port) and
 *  llasm/WinApi-ddraw-extra-asm.llasm. MIT license, see README.md.
 */

#define _FILE_OFFSET_BITS 64
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Game-Config.h"
#include "Game-Memory.h"
#include "guest.h"
#include "llasm/llasm_cpu.h"

#define EXTERN_C extern "C"

#define DD_OK                   0
#define DDERR_GENERIC           0x80004005u
#define DDERR_UNSUPPORTED       0x80004001u
#define DDERR_INVALIDPARAMS     0x80070057u
#define DDERR_OUTOFMEMORY       0x8007000Eu
#define DDERR_NOTFOUND          0x887600FFu
#define DDERR_NOCOLORKEY        0x887600D7u
#define DDERR_NOCLIPPERATTACHED 0x88760238u
#define DDERR_NOPALETTEATTACHED 0x8876023Cu
#define DDERR_NOTLOCKED         0x887602E6u
#define E_NOINTERFACE           0x80004002u

#define DDSD_CAPS            0x00000001
#define DDSD_HEIGHT          0x00000002
#define DDSD_WIDTH           0x00000004
#define DDSD_PITCH           0x00000008
#define DDSD_BACKBUFFERCOUNT 0x00000020
#define DDSD_LPSURFACE       0x00000800
#define DDSD_PIXELFORMAT     0x00001000
#define DDSD_CKSRCBLT        0x00010000
#define DDSD_REFRESHRATE     0x00040000

#define DDSCAPS_BACKBUFFER     0x00000004
#define DDSCAPS_COMPLEX        0x00000008
#define DDSCAPS_FLIP           0x00000010
#define DDSCAPS_FRONTBUFFER    0x00000020
#define DDSCAPS_OFFSCREENPLAIN 0x00000040
#define DDSCAPS_PALETTE        0x00000100
#define DDSCAPS_PRIMARYSURFACE 0x00000200
#define DDSCAPS_SYSTEMMEMORY   0x00000800
#define DDSCAPS_VIDEOMEMORY    0x00004000
#define DDSCAPS_VISIBLE        0x00008000
#define DDSCAPS_LOCALVIDMEM    0x10000000

#define DDPF_PALETTEINDEXED8 0x00000020
#define DDPF_RGB             0x00000040

#define DDBLT_COLORFILL       0x00000400
#define DDBLT_KEYSRC          0x00008000
#define DDBLT_KEYSRCOVERRIDE  0x00010000

#define DDBLTFAST_SRCCOLORKEY 0x00000001

#define DDCKEY_SRCBLT         0x00000008

#define DDLOCK_SURFACEMEMORYPTR 0

/* ------------------------------------------------------------------ objects */
/* These structures live in guest memory; the first field is the vtable. */

struct IDirectDraw_c {
    uint32_t lpVtbl;
    uint32_t RefCount;
};

struct IDirectDrawPalette_c {
    uint32_t lpVtbl;
    uint32_t RefCount;
    uint32_t entries[256];          /* PALETTEENTRY: r, g, b, flags */
    uint32_t argb[256];             /* the same as 0xAARRGGBB */
};

struct IDirectDrawClipper_c {
    uint32_t lpVtbl;
    uint32_t RefCount;
    uint32_t hWnd;
};

struct IDirectDrawSurface_c {
    uint32_t lpVtbl;
    uint32_t RefCount;
    int32_t width, height, pitch, bpp;
    uint32_t caps;
    uint8_t *pixels;                /* host pointer to guest memory */
    uint8_t *alloc;
    int has_srckey;
    uint32_t srckey_low, srckey_high;
    struct IDirectDrawSurface_c *backbuffer;   /* primary: attached back buffer */
    struct IDirectDrawSurface_c *front;        /* back buffer: its primary */
    struct IDirectDrawPalette_c *palette;
    struct IDirectDrawClipper_c *clipper;
    int locked;
};

typedef struct IDirectDrawSurface_c dds_t;

EXTERN_C uint8_t IDirectDrawVtbl_asm2c[];
EXTERN_C uint8_t IDirectDrawSurfaceVtbl_asm2c[];
EXTERN_C uint8_t IDirectDrawPaletteVtbl_asm2c[];
EXTERN_C uint8_t IDirectDrawClipperVtbl_asm2c[];

extern int Commandos_ClientWidth;
extern int Commandos_ClientHeight;
EXTERN_C int display_virtual_keyboard(SDL_Renderer *renderer);

/* ------------------------------------------------------------- display state */

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static uint32_t *frame;              /* 32-bit copy of the primary surface */
static int mode_width = 640, mode_height = 480, mode_bpp = 16;
static dds_t *primary;
static struct IDirectDrawPalette_c *primary_palette;

static void fill_pixel_format(uint8_t *pf, int bpp)
{
    memset(pf, 0, 32);
    wr32(pf + 0, 32);
    if (bpp == 8)
    {
        wr32(pf + 4, DDPF_PALETTEINDEXED8 | DDPF_RGB);
        wr32(pf + 12, 8);
    }
    else if (bpp == 16)
    {
        wr32(pf + 4, DDPF_RGB);
        wr32(pf + 12, 16);
        wr32(pf + 16, 0xf800);
        wr32(pf + 20, 0x07e0);
        wr32(pf + 24, 0x001f);
    }
    else
    {
        wr32(pf + 4, DDPF_RGB);
        wr32(pf + 12, (uint32_t)bpp);
        wr32(pf + 16, 0xff0000);
        wr32(pf + 20, 0x00ff00);
        wr32(pf + 24, 0x0000ff);
    }
}

static int create_display(int width, int height)
{
    if (window == NULL)
    {
        int win_w = (Display_Width != 0) ? Display_Width : width * 2;
        int win_h = (Display_Height != 0) ? Display_Height : height * 2;
        Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
        if (Display_Mode == 1) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        if (Display_Mode == 2) flags |= SDL_WINDOW_FULLSCREEN;
        window = SDL_CreateWindow("Commandos: Behind Enemy Lines", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h, flags);
        if (window == NULL)
        {
            fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
            return -1;
        }
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | (Display_VSync ? SDL_RENDERER_PRESENTVSYNC : 0));
        if (renderer == NULL)
        {
            fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
            return -1;
        }
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, Display_ScalingQuality ? "linear" : "nearest");
    }
    if (texture != NULL)
    {
        SDL_DestroyTexture(texture);
    }
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    SDL_RenderSetLogicalSize(renderer, width, height);
    if (Display_IntegerScaling) SDL_RenderSetIntegerScale(renderer, SDL_TRUE);
    free(frame);
    frame = (uint32_t *)calloc((size_t)width * height, 4);
    Commandos_ClientWidth = width;
    Commandos_ClientHeight = height;

    /* the window procedure waits for focus, as on Windows */
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_WINDOWEVENT;
    event.window.event = SDL_WINDOWEVENT_SHOWN;
    event.window.windowID = SDL_GetWindowID(window);
    SDL_PushEvent(&event);
    return 0;
}

static void convert_to_frame(dds_t *s)
{
    int w = (s->width < mode_width) ? s->width : mode_width;
    int h = (s->height < mode_height) ? s->height : mode_height;

    for (int y = 0; y < h; y++)
    {
        uint8_t *src = s->pixels + (size_t)y * s->pitch;
        uint32_t *dst = frame + (size_t)y * mode_width;
        if (s->bpp == 8)
        {
            struct IDirectDrawPalette_c *pal = s->palette ? s->palette : primary_palette;
            if (pal == NULL)
            {
                for (int x = 0; x < w; x++) dst[x] = 0xff000000u | (src[x] * 0x010101u);
            }
            else
            {
                for (int x = 0; x < w; x++) dst[x] = pal->argb[src[x]];
            }
        }
        else if (s->bpp == 16)
        {
            uint16_t *p = (uint16_t *)src;
            for (int x = 0; x < w; x++)
            {
                uint32_t c = p[x];
                uint32_t r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
                dst[x] = 0xff000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
            }
        }
        else if (s->bpp == 32)
        {
            uint32_t *p = (uint32_t *)src;
            for (int x = 0; x < w; x++) dst[x] = 0xff000000u | p[x];
        }
        else if (s->bpp == 24)
        {
            for (int x = 0; x < w; x++) dst[x] = 0xff000000u | (src[3 * x + 2] << 16) | (src[3 * x + 1] << 8) | src[3 * x];
        }
    }
}

static void present_primary(void)
{
    if (primary == NULL || renderer == NULL || texture == NULL) return;
    convert_to_frame(primary);
    SDL_UpdateTexture(texture, NULL, frame, mode_width * 4);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    display_virtual_keyboard(renderer);
    SDL_RenderPresent(renderer);
    if (Display_DelayAfterFlip > 0) SDL_Delay(Display_DelayAfterFlip);
}

/* ------------------------------------------------------------------ surfaces */

static dds_t *new_surface(int width, int height, int bpp, uint32_t caps)
{
    dds_t *s = (dds_t *)x86_calloc(1, sizeof(dds_t));
    if (s == NULL) return NULL;
    s->lpVtbl = to_guest(IDirectDrawSurfaceVtbl_asm2c);
    s->RefCount = 1;
    s->width = width;
    s->height = height;
    s->bpp = bpp;
    s->pitch = ((width * ((bpp + 7) / 8)) + 7) & ~7;
    s->caps = caps;
    s->alloc = (uint8_t *)x86_calloc(1, (unsigned int)(s->pitch * height + 64));
    if (s->alloc == NULL)
    {
        x86_free(s);
        return NULL;
    }
    s->pixels = (uint8_t *)(((uintptr_t)s->alloc + 15) & ~(uintptr_t)15);
    return s;
}

static void free_surface(dds_t *s)
{
    if (s == primary) primary = NULL;
    x86_free(s->alloc);
    x86_free(s);
}

static void fill_surface_desc(dds_t *s, uint8_t *desc)
{
    uint32_t size = rd32(desc);
    if (size < 108) size = 108;
    memset(desc + 4, 0, size - 4);
    wr32(desc + 4, DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT);
    wr32(desc + 8, (uint32_t)s->height);
    wr32(desc + 12, (uint32_t)s->width);
    wr32(desc + 16, (uint32_t)s->pitch);
    if (s->backbuffer)
    {
        wr32(desc + 4, rd32(desc + 4) | DDSD_BACKBUFFERCOUNT);
        wr32(desc + 20, 1);
    }
    if (s->has_srckey)
    {
        wr32(desc + 4, rd32(desc + 4) | DDSD_CKSRCBLT);
        wr32(desc + 64, s->srckey_low);
        wr32(desc + 68, s->srckey_high);
    }
    fill_pixel_format(desc + 72, s->bpp);
    wr32(desc + 104, s->caps);
}

static uint32_t get_pixel(dds_t *s, int x, int y)
{
    uint8_t *p = s->pixels + (size_t)y * s->pitch;
    switch (s->bpp)
    {
        case 8: return p[x];
        case 16: return ((uint16_t *)p)[x];
        case 32: return ((uint32_t *)p)[x];
        default: return p[3 * x] | (p[3 * x + 1] << 8) | (p[3 * x + 2] << 16);
    }
}

static void put_pixel(dds_t *s, int x, int y, uint32_t c)
{
    uint8_t *p = s->pixels + (size_t)y * s->pitch;
    switch (s->bpp)
    {
        case 8: p[x] = (uint8_t)c; break;
        case 16: ((uint16_t *)p)[x] = (uint16_t)c; break;
        case 32: ((uint32_t *)p)[x] = c; break;
        default: p[3 * x] = (uint8_t)c; p[3 * x + 1] = (uint8_t)(c >> 8); p[3 * x + 2] = (uint8_t)(c >> 16); break;
    }
}

typedef struct { int32_t left, top, right, bottom; } rect_t;

static void read_rect(const void *r, rect_t *out, dds_t *s)
{
    if (r == NULL)
    {
        out->left = 0; out->top = 0; out->right = s->width; out->bottom = s->height;
    }
    else
    {
        memcpy(out, r, 16);
    }
}

/* Copy src_rect of src to dst_rect of dst; stretch if the sizes differ. */
static void blit(dds_t *dst, rect_t d, dds_t *src, rect_t sr, int use_key, uint32_t key_low, uint32_t key_high)
{
    int dw = d.right - d.left, dh = d.bottom - d.top;
    int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;

    int same_size = (dw == sw && dh == sh);
    for (int y = 0; y < dh; y++)
    {
        int dy = d.top + y;
        if (dy < 0 || dy >= dst->height) continue;
        int sy = sr.top + (same_size ? y : (int)((int64_t)y * sh / dh));
        if (sy < 0 || sy >= src->height) continue;

        if (same_size && !use_key && dst->bpp == src->bpp)
        {
            int x0 = 0, x1 = dw;
            if (d.left + x0 < 0) x0 = -d.left;
            if (sr.left + x0 < 0) x0 = -sr.left;
            if (d.left + x1 > dst->width) x1 = dst->width - d.left;
            if (sr.left + x1 > src->width) x1 = src->width - sr.left;
            if (x1 > x0)
            {
                int bypp = (dst->bpp + 7) / 8;
                memmove(dst->pixels + (size_t)dy * dst->pitch + (size_t)(d.left + x0) * bypp,
                        src->pixels + (size_t)sy * src->pitch + (size_t)(sr.left + x0) * bypp,
                        (size_t)(x1 - x0) * bypp);
            }
            continue;
        }

        for (int x = 0; x < dw; x++)
        {
            int dx = d.left + x;
            if (dx < 0 || dx >= dst->width) continue;
            int sx = sr.left + (same_size ? x : (int)((int64_t)x * sw / dw));
            if (sx < 0 || sx >= src->width) continue;
            uint32_t c = get_pixel(src, sx, sy);
            if (use_key && c >= key_low && c <= key_high) continue;
            put_pixel(dst, dx, dy, c);
        }
    }
}

static void color_fill(dds_t *dst, rect_t d, uint32_t color)
{
    if (d.left < 0) d.left = 0;
    if (d.top < 0) d.top = 0;
    if (d.right > dst->width) d.right = dst->width;
    if (d.bottom > dst->height) d.bottom = dst->height;
    for (int y = d.top; y < d.bottom; y++)
    {
        for (int x = d.left; x < d.right; x++) put_pixel(dst, x, y, color);
    }
}

static int is_visible(dds_t *s)
{
    return s == primary;
}

/* ------------------------------------------------------------- IDirectDraw */

EXTERN_C uint32_t DirectDrawCreate_c(void *lpGUID, uint32_t *lplpDD, void *pUnkOuter)
{
    struct IDirectDraw_c *dd;
    if (lplpDD == NULL) return DDERR_INVALIDPARAMS;
    dd = (struct IDirectDraw_c *)x86_calloc(1, sizeof(*dd));
    dd->lpVtbl = to_guest(IDirectDrawVtbl_asm2c);
    dd->RefCount = 1;
    *lplpDD = to_guest(dd);
    return DD_OK;
}

EXTERN_C uint32_t IDirectDraw_QueryInterface_c(struct IDirectDraw_c *lpThis, void *riid, uint32_t *ppvObj)
{
    if (ppvObj) *ppvObj = 0;
    return E_NOINTERFACE;
}

EXTERN_C uint32_t IDirectDraw_AddRef_c(struct IDirectDraw_c *lpThis) { return ++lpThis->RefCount; }

EXTERN_C uint32_t IDirectDraw_Release_c(struct IDirectDraw_c *lpThis)
{
    if (--lpThis->RefCount == 0)
    {
        x86_free(lpThis);
        return 0;
    }
    return lpThis->RefCount;
}

EXTERN_C uint32_t IDirectDraw_Compact_c(struct IDirectDraw_c *lpThis) { return DD_OK; }

EXTERN_C uint32_t IDirectDraw_CreateClipper_c(struct IDirectDraw_c *lpThis, uint32_t dwFlags, uint32_t *lplpDDClipper, void *pUnkOuter)
{
    struct IDirectDrawClipper_c *c = (struct IDirectDrawClipper_c *)x86_calloc(1, sizeof(*c));
    c->lpVtbl = to_guest(IDirectDrawClipperVtbl_asm2c);
    c->RefCount = 1;
    *lplpDDClipper = to_guest(c);
    return DD_OK;
}

static void palette_set(struct IDirectDrawPalette_c *p, uint32_t start, uint32_t count, const uint32_t *entries)
{
    for (uint32_t i = 0; i < count && start + i < 256; i++)
    {
        uint32_t e = entries[i];
        p->entries[start + i] = e;
        p->argb[start + i] = 0xff000000u | ((e & 0xff) << 16) | (e & 0xff00) | ((e >> 16) & 0xff);
    }
}

EXTERN_C uint32_t IDirectDraw_CreatePalette_c(struct IDirectDraw_c *lpThis, uint32_t dwFlags, void *lpColorTable, uint32_t *lplpDDPalette, void *pUnkOuter)
{
    struct IDirectDrawPalette_c *p = (struct IDirectDrawPalette_c *)x86_calloc(1, sizeof(*p));
    p->lpVtbl = to_guest(IDirectDrawPaletteVtbl_asm2c);
    p->RefCount = 1;
    if (lpColorTable) palette_set(p, 0, 256, (const uint32_t *)lpColorTable);
    *lplpDDPalette = to_guest(p);
    return DD_OK;
}

EXTERN_C uint32_t IDirectDraw_CreateSurface_c(struct IDirectDraw_c *lpThis, uint8_t *desc, uint32_t *lplpDDSurface, void *pUnkOuter)
{
    uint32_t flags, caps;
    int width, height, bpp;
    dds_t *s;

    if (desc == NULL || lplpDDSurface == NULL) return DDERR_INVALIDPARAMS;
    flags = rd32(desc + 4);
    caps = (flags & DDSD_CAPS) ? rd32(desc + 104) : 0;

    if (caps & DDSCAPS_PRIMARYSURFACE)
    {
        s = new_surface(mode_width, mode_height, mode_bpp, caps | DDSCAPS_FRONTBUFFER | DDSCAPS_VIDEOMEMORY | DDSCAPS_VISIBLE);
        if (s == NULL) return DDERR_OUTOFMEMORY;
        if ((flags & DDSD_BACKBUFFERCOUNT) && rd32(desc + 20) > 0)
        {
            dds_t *b = new_surface(mode_width, mode_height, mode_bpp, DDSCAPS_BACKBUFFER | DDSCAPS_VIDEOMEMORY | DDSCAPS_FLIP | DDSCAPS_COMPLEX);
            if (b == NULL) return DDERR_OUTOFMEMORY;
            s->backbuffer = b;
            b->front = s;
        }
        primary = s;
        *lplpDDSurface = to_guest(s);
        return DD_OK;
    }

    width = (flags & DDSD_WIDTH) ? (int)rd32(desc + 12) : mode_width;
    height = (flags & DDSD_HEIGHT) ? (int)rd32(desc + 8) : mode_height;
    bpp = mode_bpp;
    if (flags & DDSD_PIXELFORMAT)
    {
        uint32_t pf_flags = rd32(desc + 72 + 4);
        bpp = (pf_flags & DDPF_PALETTEINDEXED8) ? 8 : (int)rd32(desc + 72 + 12);
        if (bpp == 0) bpp = mode_bpp;
    }
    if (width <= 0 || height <= 0) return DDERR_INVALIDPARAMS;
    if (!(caps & (DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY))) caps |= DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM;
    s = new_surface(width, height, bpp, caps);
    if (s == NULL) return DDERR_OUTOFMEMORY;
    if (flags & DDSD_CKSRCBLT)
    {
        s->has_srckey = 1;
        s->srckey_low = rd32(desc + 64);
        s->srckey_high = rd32(desc + 68);
    }
    *lplpDDSurface = to_guest(s);
    return DD_OK;
}

EXTERN_C uint32_t IDirectDraw_DuplicateSurface_c(struct IDirectDraw_c *lpThis, dds_t *src, uint32_t *out)
{
    dds_t *s = new_surface(src->width, src->height, src->bpp, src->caps & ~(DDSCAPS_PRIMARYSURFACE | DDSCAPS_FRONTBUFFER));
    if (s == NULL) return DDERR_OUTOFMEMORY;
    memcpy(s->pixels, src->pixels, (size_t)src->pitch * src->height);
    *out = to_guest(s);
    return DD_OK;
}

EXTERN_C uint32_t CCALL CallX86Function(uint32_t address, int nargs, const uint32_t *args);

EXTERN_C uint32_t IDirectDraw_EnumDisplayModes_c(struct IDirectDraw_c *lpThis, uint32_t dwFlags, void *lpDDSurfaceDesc, void *lpContext, void *lpEnumModesCallback)
{
    static const int modes[][2] = { {640, 480}, {800, 600}, {1024, 768} };
    static const int depths[] = { 8, 16, 32 };
    uint8_t *desc = (uint8_t *)x86_calloc(1, 108);

    for (unsigned d = 0; d < sizeof(depths) / sizeof(depths[0]); d++)
    {
        for (unsigned m = 0; m < sizeof(modes) / sizeof(modes[0]); m++)
        {
            uint32_t args[2];
            memset(desc, 0, 108);
            wr32(desc, 108);
            wr32(desc + 4, DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE);
            wr32(desc + 8, (uint32_t)modes[m][1]);
            wr32(desc + 12, (uint32_t)modes[m][0]);
            wr32(desc + 16, (uint32_t)(modes[m][0] * depths[d] / 8));
            wr32(desc + 24, 60);
            fill_pixel_format(desc + 72, depths[d]);
            args[0] = to_guest(desc);
            args[1] = to_guest(lpContext);
            if (CallX86Function(to_guest(lpEnumModesCallback), 2, args) == 0)   /* DDENUMRET_CANCEL */
            {
                x86_free(desc);
                return DD_OK;
            }
        }
    }
    x86_free(desc);
    return DD_OK;
}

EXTERN_C uint32_t IDirectDraw_EnumSurfaces_c(struct IDirectDraw_c *lpThis, uint32_t a, void *b, void *c, void *d) { return DDERR_UNSUPPORTED; }
EXTERN_C uint32_t IDirectDraw_FlipToGDISurface_c(struct IDirectDraw_c *lpThis) { return DD_OK; }

static void fill_caps(uint8_t *caps)
{
    if (caps == NULL) return;
    uint32_t size = rd32(caps);
    if (size < 8) return;
    memset(caps + 4, 0, size - 4);
    wr32(caps + 4, 0x00000040 | 0x00000200 | 0x00010000 | 0x00400000 | 0x04000000 | 0x40000000); /* BLT, BLTSTRETCH, COLORKEY, PALETTE, BLTCOLORFILL, ... */
    wr32(caps + 12, 0x00000200);           /* DDCKEYCAPS_SRCBLT */
    wr32(caps + 24, 0x00000004);           /* DDPCAPS_8BIT */
    if (size >= 68)
    {
        wr32(caps + 60, 64u << 20);        /* dwVidMemTotal */
        wr32(caps + 64, 60u << 20);        /* dwVidMemFree */
    }
}

EXTERN_C uint32_t IDirectDraw_GetCaps_c(struct IDirectDraw_c *lpThis, uint8_t *lpDDDriverCaps, uint8_t *lpDDHELCaps)
{
    fill_caps(lpDDDriverCaps);
    fill_caps(lpDDHELCaps);
    return DD_OK;
}

EXTERN_C uint32_t IDirectDraw_GetDisplayMode_c(struct IDirectDraw_c *lpThis, uint8_t *desc)
{
    uint32_t size = rd32(desc);
    if (size < 108) size = 108;
    memset(desc + 4, 0, size - 4);
    wr32(desc + 4, DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE);
    wr32(desc + 8, (uint32_t)mode_height);
    wr32(desc + 12, (uint32_t)mode_width);
    wr32(desc + 16, (uint32_t)(mode_width * mode_bpp / 8));
    wr32(desc + 24, 60);
    fill_pixel_format(desc + 72, mode_bpp);
    return DD_OK;
}

EXTERN_C uint32_t IDirectDraw_GetFourCCCodes_c(struct IDirectDraw_c *lpThis, uint32_t *n, uint32_t *codes)
{
    if (n) *n = 0;
    return DD_OK;
}

EXTERN_C uint32_t IDirectDraw_GetGDISurface_c(struct IDirectDraw_c *lpThis, uint32_t *out)
{
    if (primary == NULL) return DDERR_NOTFOUND;
    *out = to_guest(primary);
    primary->RefCount++;
    return DD_OK;
}

EXTERN_C uint32_t IDirectDraw_GetMonitorFrequency_c(struct IDirectDraw_c *lpThis, uint32_t *f) { if (f) *f = 60; return DD_OK; }
EXTERN_C uint32_t IDirectDraw_GetScanLine_c(struct IDirectDraw_c *lpThis, uint32_t *l) { if (l) *l = 0; return DD_OK; }
EXTERN_C uint32_t IDirectDraw_GetVerticalBlankStatus_c(struct IDirectDraw_c *lpThis, uint32_t *b) { if (b) *b = 1; return DD_OK; }
EXTERN_C uint32_t IDirectDraw_Initialize_c(struct IDirectDraw_c *lpThis, void *guid) { return DD_OK; }
EXTERN_C uint32_t IDirectDraw_RestoreDisplayMode_c(struct IDirectDraw_c *lpThis) { return DD_OK; }

EXTERN_C uint32_t IDirectDraw_SetCooperativeLevel_c(struct IDirectDraw_c *lpThis, void *hWnd, uint32_t dwFlags)
{
    return DD_OK;
}

EXTERN_C uint32_t IDirectDraw_SetDisplayMode_c(struct IDirectDraw_c *lpThis, uint32_t dwWidth, uint32_t dwHeight, uint32_t dwBPP)
{
    fprintf(stderr, "DirectDraw: display mode %ux%u, %u bits\n", dwWidth, dwHeight, dwBPP);
    if (dwBPP != 8 && dwBPP != 16 && dwBPP != 24 && dwBPP != 32) return DDERR_UNSUPPORTED;
    mode_width = (int)dwWidth;
    mode_height = (int)dwHeight;
    mode_bpp = (int)dwBPP;
    if (create_display(mode_width, mode_height) != 0) return DDERR_GENERIC;
    return DD_OK;
}

EXTERN_C uint32_t IDirectDraw_WaitForVerticalBlank_c(struct IDirectDraw_c *lpThis, uint32_t dwFlags, void *hEvent)
{
    return DD_OK;
}

/* ------------------------------------------------------- IDirectDrawSurface */

EXTERN_C uint32_t IDirectDrawSurface_QueryInterface_c(dds_t *lpThis, void *riid, uint32_t *ppvObj)
{
    if (ppvObj) *ppvObj = 0;
    return E_NOINTERFACE;
}

EXTERN_C uint32_t IDirectDrawSurface_AddRef_c(dds_t *lpThis) { return ++lpThis->RefCount; }

EXTERN_C uint32_t IDirectDrawSurface_Release_c(dds_t *lpThis)
{
    if (lpThis->RefCount > 0) lpThis->RefCount--;
    if (lpThis->RefCount == 0)
    {
        if (lpThis->front) return 0;      /* back buffer: freed with its primary */
        if (lpThis->backbuffer) free_surface(lpThis->backbuffer);
        free_surface(lpThis);
        return 0;
    }
    return lpThis->RefCount;
}

EXTERN_C uint32_t IDirectDrawSurface_AddAttachedSurface_c(dds_t *lpThis, dds_t *att)
{
    if (att->caps & DDSCAPS_BACKBUFFER)
    {
        lpThis->backbuffer = att;
        att->front = lpThis;
        return DD_OK;
    }
    return DDERR_UNSUPPORTED;
}

EXTERN_C uint32_t IDirectDrawSurface_AddOverlayDirtyRect_c(dds_t *lpThis, void *r) { return DDERR_UNSUPPORTED; }

EXTERN_C uint32_t IDirectDrawSurface_Blt_c(dds_t *lpThis, void *lpDestRect, dds_t *src, void *lpSrcRect, uint32_t dwFlags, uint8_t *fx)
{
    rect_t d, s;
    read_rect(lpDestRect, &d, lpThis);

    if (dwFlags & DDBLT_COLORFILL)
    {
        color_fill(lpThis, d, fx ? rd32(fx + 80) : 0);
    }
    else if (src != NULL)
    {
        int use_key = 0;
        uint32_t lo = 0, hi = 0;
        read_rect(lpSrcRect, &s, src);
        if ((dwFlags & DDBLT_KEYSRCOVERRIDE) && fx)
        {
            use_key = 1; lo = rd32(fx + 92); hi = rd32(fx + 96);
        }
        else if ((dwFlags & DDBLT_KEYSRC) && src->has_srckey)
        {
            use_key = 1; lo = src->srckey_low; hi = src->srckey_high;
        }
        blit(lpThis, d, src, s, use_key, lo, hi);
    }
    else
    {
        LOG_ONCE("IDirectDrawSurface_Blt: flags 0x%x without source not supported\n", dwFlags);
    }

    if (is_visible(lpThis)) present_primary();
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawSurface_BltBatch_c(dds_t *lpThis, void *a, uint32_t b, uint32_t c) { return DDERR_UNSUPPORTED; }

EXTERN_C uint32_t IDirectDrawSurface_BltFast_c(dds_t *lpThis, uint32_t x, uint32_t y, dds_t *src, void *lpSrcRect, uint32_t dwTrans)
{
    rect_t s, d;
    if (src == NULL) return DDERR_INVALIDPARAMS;
    read_rect(lpSrcRect, &s, src);
    d.left = (int32_t)x; d.top = (int32_t)y;
    d.right = d.left + (s.right - s.left); d.bottom = d.top + (s.bottom - s.top);
    blit(lpThis, d, src, s, (dwTrans & DDBLTFAST_SRCCOLORKEY) && src->has_srckey, src->srckey_low, src->srckey_high);
    if (is_visible(lpThis)) present_primary();
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawSurface_DeleteAttachedSurface_c(dds_t *lpThis, uint32_t f, dds_t *att)
{
    if (lpThis->backbuffer == att || att == NULL) lpThis->backbuffer = NULL;
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawSurface_EnumAttachedSurfaces_c(dds_t *lpThis, void *ctx, void *cb)
{
    if (lpThis->backbuffer)
    {
        uint8_t *desc = (uint8_t *)x86_calloc(1, 108);
        uint32_t args[3];
        wr32(desc, 108);
        fill_surface_desc(lpThis->backbuffer, desc);
        lpThis->backbuffer->RefCount++;
        args[0] = to_guest(lpThis->backbuffer);
        args[1] = to_guest(desc);
        args[2] = to_guest(ctx);
        CallX86Function(to_guest(cb), 3, args);
        x86_free(desc);
    }
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawSurface_EnumOverlayZOrders_c(dds_t *lpThis, uint32_t a, void *b, void *c) { return DDERR_UNSUPPORTED; }

EXTERN_C uint32_t IDirectDrawSurface_Flip_c(dds_t *lpThis, dds_t *target, uint32_t dwFlags)
{
    dds_t *back = lpThis->backbuffer;
    if (back != NULL)
    {
        /* swap the memory of the front and back buffers, as DirectDraw does */
        uint8_t *p = lpThis->pixels, *a = lpThis->alloc;
        lpThis->pixels = back->pixels; lpThis->alloc = back->alloc;
        back->pixels = p; back->alloc = a;
    }
    present_primary();
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawSurface_GetAttachedSurface_c(dds_t *lpThis, uint32_t *caps, uint32_t *out)
{
    if (lpThis->backbuffer && caps && (*caps & (DDSCAPS_BACKBUFFER | DDSCAPS_FLIP)))
    {
        lpThis->backbuffer->RefCount++;
        *out = to_guest(lpThis->backbuffer);
        return DD_OK;
    }
    return DDERR_NOTFOUND;
}

EXTERN_C uint32_t IDirectDrawSurface_GetBltStatus_c(dds_t *lpThis, uint32_t f) { return DD_OK; }

EXTERN_C uint32_t IDirectDrawSurface_GetCaps_c(dds_t *lpThis, uint32_t *caps)
{
    *caps = lpThis->caps;
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawSurface_GetClipper_c(dds_t *lpThis, uint32_t *out)
{
    if (lpThis->clipper == NULL) return DDERR_NOCLIPPERATTACHED;
    lpThis->clipper->RefCount++;
    *out = to_guest(lpThis->clipper);
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawSurface_GetColorKey_c(dds_t *lpThis, uint32_t dwFlags, uint32_t *key)
{
    if ((dwFlags & DDCKEY_SRCBLT) && lpThis->has_srckey)
    {
        key[0] = lpThis->srckey_low;
        key[1] = lpThis->srckey_high;
        return DD_OK;
    }
    return DDERR_NOCOLORKEY;
}

EXTERN_C uint32_t IDirectDrawSurface_GetDC_c(dds_t *lpThis, uint32_t *hdc)
{
    LOG_ONCE("IDirectDrawSurface_GetDC: GDI drawing on surfaces is not supported\n");
    *hdc = 0xfdd0;
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawSurface_GetFlipStatus_c(dds_t *lpThis, uint32_t f) { return DD_OK; }
EXTERN_C uint32_t IDirectDrawSurface_GetOverlayPosition_c(dds_t *lpThis, void *x, void *y) { return DDERR_UNSUPPORTED; }

EXTERN_C uint32_t IDirectDrawSurface_GetPalette_c(dds_t *lpThis, uint32_t *out)
{
    if (lpThis->palette == NULL) return DDERR_NOPALETTEATTACHED;
    lpThis->palette->RefCount++;
    *out = to_guest(lpThis->palette);
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawSurface_GetPixelFormat_c(dds_t *lpThis, uint8_t *pf)
{
    fill_pixel_format(pf, lpThis->bpp);
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawSurface_GetSurfaceDesc_c(dds_t *lpThis, uint8_t *desc)
{
    fill_surface_desc(lpThis, desc);
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawSurface_Initialize_c(dds_t *lpThis, void *dd, void *desc) { return DD_OK; }
EXTERN_C uint32_t IDirectDrawSurface_IsLost_c(dds_t *lpThis) { return DD_OK; }

EXTERN_C uint32_t IDirectDrawSurface_Lock_c(dds_t *lpThis, void *lpDestRect, uint8_t *desc, uint32_t dwFlags, void *hEvent)
{
    uint8_t *p = lpThis->pixels;
    if (desc == NULL) return DDERR_INVALIDPARAMS;
    if (lpDestRect)
    {
        rect_t r;
        memcpy(&r, lpDestRect, 16);
        p += (size_t)r.top * lpThis->pitch + (size_t)r.left * ((lpThis->bpp + 7) / 8);
    }
    fill_surface_desc(lpThis, desc);
    wr32(desc + 4, rd32(desc + 4) | DDSD_LPSURFACE);
    wr32(desc + 36, to_guest(p));
    lpThis->locked++;
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawSurface_ReleaseDC_c(dds_t *lpThis, void *hdc) { return DD_OK; }
EXTERN_C uint32_t IDirectDrawSurface_Restore_c(dds_t *lpThis) { return DD_OK; }

EXTERN_C uint32_t IDirectDrawSurface_SetClipper_c(dds_t *lpThis, struct IDirectDrawClipper_c *c)
{
    lpThis->clipper = c;
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawSurface_SetColorKey_c(dds_t *lpThis, uint32_t dwFlags, uint32_t *key)
{
    if (dwFlags & DDCKEY_SRCBLT)
    {
        if (key == NULL)
        {
            lpThis->has_srckey = 0;
        }
        else
        {
            lpThis->has_srckey = 1;
            lpThis->srckey_low = key[0];
            lpThis->srckey_high = key[1];
        }
    }
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawSurface_SetOverlayPosition_c(dds_t *lpThis, uint32_t x, uint32_t y) { return DDERR_UNSUPPORTED; }

EXTERN_C uint32_t IDirectDrawSurface_SetPalette_c(dds_t *lpThis, struct IDirectDrawPalette_c *pal)
{
    lpThis->palette = pal;
    if (lpThis == primary)
    {
        primary_palette = pal;
        if (lpThis->backbuffer) lpThis->backbuffer->palette = pal;
    }
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawSurface_Unlock_c(dds_t *lpThis, void *lpSurfaceData)
{
    if (lpThis->locked > 0) lpThis->locked--;
    if (is_visible(lpThis)) present_primary();
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawSurface_UpdateOverlay_c(dds_t *lpThis, void *a, void *b, void *c, uint32_t d, void *e) { return DDERR_UNSUPPORTED; }
EXTERN_C uint32_t IDirectDrawSurface_UpdateOverlayDisplay_c(dds_t *lpThis, uint32_t f) { return DDERR_UNSUPPORTED; }
EXTERN_C uint32_t IDirectDrawSurface_UpdateOverlayZOrder_c(dds_t *lpThis, uint32_t f, void *r) { return DDERR_UNSUPPORTED; }

/* ------------------------------------------------------- IDirectDrawPalette */

EXTERN_C uint32_t IDirectDrawPalette_QueryInterface_c(struct IDirectDrawPalette_c *lpThis, void *riid, uint32_t *ppv)
{
    if (ppv) *ppv = 0;
    return E_NOINTERFACE;
}

EXTERN_C uint32_t IDirectDrawPalette_AddRef_c(struct IDirectDrawPalette_c *lpThis) { return ++lpThis->RefCount; }

EXTERN_C uint32_t IDirectDrawPalette_Release_c(struct IDirectDrawPalette_c *lpThis)
{
    if (lpThis->RefCount > 0) lpThis->RefCount--;
    if (lpThis->RefCount == 0)
    {
        if (primary_palette == lpThis) primary_palette = NULL;
        if (primary && primary->palette == lpThis) primary->palette = NULL;
        x86_free(lpThis);
        return 0;
    }
    return lpThis->RefCount;
}

EXTERN_C uint32_t IDirectDrawPalette_GetCaps_c(struct IDirectDrawPalette_c *lpThis, uint32_t *caps)
{
    *caps = 0x00000004;   /* DDPCAPS_8BIT */
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawPalette_GetEntries_c(struct IDirectDrawPalette_c *lpThis, uint32_t dwFlags, uint32_t start, uint32_t count, uint32_t *entries)
{
    for (uint32_t i = 0; i < count && start + i < 256; i++) entries[i] = lpThis->entries[start + i];
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawPalette_Initialize_c(struct IDirectDrawPalette_c *lpThis, void *dd, uint32_t f, void *t) { return DD_OK; }

EXTERN_C uint32_t IDirectDrawPalette_SetEntries_c(struct IDirectDrawPalette_c *lpThis, uint32_t dwFlags, uint32_t start, uint32_t count, uint32_t *entries)
{
    palette_set(lpThis, start, count, entries);
    if (primary && primary_palette == lpThis && primary->bpp == 8) present_primary();
    return DD_OK;
}

/* ------------------------------------------------------- IDirectDrawClipper */

EXTERN_C uint32_t IDirectDrawClipper_QueryInterface_c(struct IDirectDrawClipper_c *lpThis, void *riid, uint32_t *ppv)
{
    if (ppv) *ppv = 0;
    return E_NOINTERFACE;
}

EXTERN_C uint32_t IDirectDrawClipper_AddRef_c(struct IDirectDrawClipper_c *lpThis) { return ++lpThis->RefCount; }

EXTERN_C uint32_t IDirectDrawClipper_Release_c(struct IDirectDrawClipper_c *lpThis)
{
    if (lpThis->RefCount > 0) lpThis->RefCount--;
    if (lpThis->RefCount == 0)
    {
        x86_free(lpThis);
        return 0;
    }
    return lpThis->RefCount;
}

EXTERN_C uint32_t IDirectDrawClipper_GetClipList_c(struct IDirectDrawClipper_c *lpThis, void *r, void *list, uint32_t *size) { return DDERR_UNSUPPORTED; }

EXTERN_C uint32_t IDirectDrawClipper_GetHWnd_c(struct IDirectDrawClipper_c *lpThis, uint32_t *hwnd)
{
    *hwnd = lpThis->hWnd;
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawClipper_Initialize_c(struct IDirectDrawClipper_c *lpThis, void *dd, uint32_t f) { return DD_OK; }

EXTERN_C uint32_t IDirectDrawClipper_IsClipListChanged_c(struct IDirectDrawClipper_c *lpThis, uint32_t *changed)
{
    *changed = 0;
    return DD_OK;
}

EXTERN_C uint32_t IDirectDrawClipper_SetClipList_c(struct IDirectDrawClipper_c *lpThis, void *list, uint32_t f) { return DD_OK; }

EXTERN_C uint32_t IDirectDrawClipper_SetHWnd_c(struct IDirectDrawClipper_c *lpThis, uint32_t f, uint32_t hwnd)
{
    lpThis->hWnd = hwnd;
    return DD_OK;
}

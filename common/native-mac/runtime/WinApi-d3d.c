/*
 *  Native port: Direct3D 6 immediate mode (IDirect3D3, IDirect3DDevice3,
 *  IDirect3DViewport3, IDirect3DLight, IDirect3DMaterial3, IDirect3DTexture2)
 *  with a software rasterizer. First needed by Revenant, which draws its 3D
 *  characters and effects with Direct3D over a 2D background whose depth it
 *  writes into the z-buffer itself.
 *
 *  The rasterizer draws into the DirectDraw surfaces of WinApi-ddraw.c:
 *  16-bit color targets (565 or 555) and a 16-bit z-buffer (0 = near,
 *  0xffff = far, as Direct3D maps z = 0..1). It does the Direct3D transform
 *  and lighting pipeline for untransformed vertices (world, view and
 *  projection matrices, directional/point/spot lights, materials), clipping
 *  against the near and far planes, Gouraud shading, perspective-correct
 *  texture mapping, color keys, alpha test, alpha blending and z test.
 *
 *  The COM glue comes from com/d3d3.com. MIT license, see the README.md of
 *  the repository.
 */

#include "game-info.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "WinApi.h"
#include "Game-Memory.h"
#include "guest.h"
#include "platform.h"

#define D3D_OK            0u
#define DDERR_INVALIDPARAMS 0x80070057u
#define DDERR_OUTOFMEMORY 0x8007000Eu
#define E_NOINTERFACE     0x80004002u
#define DDERR_UNSUPPORTED 0x80004001u
#define D3DERR_INVALIDVERTEXTYPE 0x887602ADu

EXTERN_C uint8_t IDirect3D3Vtbl_asm2c[];
EXTERN_C uint8_t IDirect3DDevice3Vtbl_asm2c[];
EXTERN_C uint8_t IDirect3DViewport3Vtbl_asm2c[];
EXTERN_C uint8_t IDirect3DLightVtbl_asm2c[];
EXTERN_C uint8_t IDirect3DMaterial3Vtbl_asm2c[];
EXTERN_C uint8_t IDirect3DTexture2Vtbl_asm2c[];

EXTERN_C uint32_t CCALL CallX86Function(uint32_t address, int nargs, const uint32_t *args);

/* WinApi-ddraw.c */
EXTERN_C int DDraw_SurfaceInfo4(void *surface, uint8_t **pixels, int *pitch, int *width, int *height, uint32_t *caps,
                                void **zbuffer, uint32_t *texture, uint8_t *pf);
EXTERN_C void DDraw_SetSurfaceTexture(void *surface, uint32_t texture);
EXTERN_C int DDraw_SurfaceColorKey(void *surface, uint32_t *low, uint32_t *high);
EXTERN_C uint32_t IDirectDrawSurface4_AddRef_c(void *s);
EXTERN_C uint32_t IDirectDrawSurface4_Release_c(void *s);

static int trace_d3d(void)
{
    static int trace = -1;
    if (trace < 0)
    {
        const char *v = game_getenv("TRACE_D3D");
        trace = v ? atoi(v) : 0;
        if (v && trace == 0) trace = 1;
    }
    return trace;
}
#define TRACE1(...) do { if (trace_d3d() >= 1) fprintf(stderr, __VA_ARGS__); } while (0)
#define TRACE2(...) do { if (trace_d3d() >= 2) fprintf(stderr, __VA_ARGS__); } while (0)

static int same_guid(const void *riid, uint32_t d1)
{
    return riid && rd32(riid) == d1;
}

static float rdf(const void *p) { float f; memcpy(&f, p, 4); return f; }
static void wrf(void *p, float f) { memcpy(p, &f, 4); }
static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* ------------------------------------------------------------------ objects */

typedef struct { float r, g, b, a; } color4;

typedef struct d3d_light {
    uint32_t lpVtbl, RefCount;
    uint32_t type;              /* 1 point, 2 spot, 3 directional, 4 parallel point */
    color4 color;
    float pos[3], dir[3];
    float range, falloff, att0, att1, att2, theta, phi;
    uint32_t flags;             /* D3DLIGHT_ACTIVE 1, D3DLIGHT_NO_SPECULAR 2 */
} d3d_light;

typedef struct d3d_material {
    uint32_t lpVtbl, RefCount;
    color4 diffuse, ambient, specular, emissive;
    float power;
    uint32_t texture_handle;
} d3d_material;

#define MAX_LIGHTS 16

typedef struct d3d_viewport {
    uint32_t lpVtbl, RefCount;
    int version;                /* 1: D3DVIEWPORT, 2: D3DVIEWPORT2 */
    uint32_t x, y, width, height;
    float scale_x, scale_y, max_x, max_y;               /* D3DVIEWPORT */
    float clip_x, clip_y, clip_w, clip_h;               /* D3DVIEWPORT2 */
    float min_z, max_z;
    uint32_t background;        /* material handle */
    void *background_depth;     /* host pointer of a z surface */
    d3d_light *lights[MAX_LIGHTS];
    int num_lights;
} d3d_viewport;

typedef struct d3d_texture {
    uint32_t lpVtbl, RefCount;
    void *surface;              /* host pointer of the dds_t */
} d3d_texture;

typedef struct d3d_device {
    uint32_t lpVtbl, RefCount;
    void *target;               /* render target surface (host pointer) */
    d3d_viewport *viewport;
    uint32_t rs[256];           /* render states */
    uint32_t ls[16];            /* light states */
    uint32_t tss[8][32];        /* texture stage states */
    d3d_texture *texture[8];
    float world[16], view[16], proj[16];
    int matrices_dirty;
    float wvp[16];              /* world * view * projection */
    uint32_t begin_type, begin_fvf, begin_count;
    uint8_t *begin_buf;
    uint32_t begin_flags;
    /* D3DCLIPSTATUS: the screen extents of what DrawPrimitive drew since the
     * last SetClipStatus (minx, maxx, miny, maxy, minz, maxz). The game
     * reads them after each object and repaints that screen area in the
     * next frame, so empty extents leave old pictures on the screen. */
    uint32_t clip_status;
    float extents[6];
    int no_extents;             /* D3DDP_DONOTUPDATEEXTENTS for this draw */
} d3d_device;

typedef struct d3d_direct3d {
    uint32_t lpVtbl, RefCount;
} d3d_direct3d;

static d3d_device *current_device;

/* ---------------------------------------------------------------- helpers */

static void identity(float *m)
{
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/* Direct3D matrices are row-major with row vectors: v' = v * M */
static void matmul(float *out, const float *a, const float *b)
{
    float r[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] + a[i * 4 + 1] * b[1 * 4 + j] +
                           a[i * 4 + 2] * b[2 * 4 + j] + a[i * 4 + 3] * b[3 * 4 + j];
    memcpy(out, r, 64);
}

static void read_color(color4 *c, const uint8_t *p)
{
    c->r = rdf(p); c->g = rdf(p + 4); c->b = rdf(p + 8); c->a = rdf(p + 12);
}

static void write_color(uint8_t *p, const color4 *c)
{
    wrf(p, c->r); wrf(p + 4, c->g); wrf(p + 8, c->b); wrf(p + 12, c->a);
}

static uint32_t new_object(size_t size, uint8_t *vtbl, uint32_t *ppv)
{
    uint32_t *o = (uint32_t *)x86_calloc(1, (unsigned int)size);
    if (o == NULL) return DDERR_OUTOFMEMORY;
    o[0] = to_guest(vtbl);
    o[1] = 1;
    if (ppv) *ppv = to_guest(o);
    return D3D_OK;
}

/* ------------------------------------------------------------ IDirect3D3 */

EXTERN_C uint32_t D3D_CreateDirect3D3(uint32_t *ppv)
{
    TRACE1("D3D: IDirect3D3 created\n");
    return new_object(sizeof(d3d_direct3d), IDirect3D3Vtbl_asm2c, ppv);
}

EXTERN_C uint32_t IDirect3D3_QueryInterface_c(d3d_direct3d *o, void *riid, uint32_t *ppv)
{
    if (ppv == NULL) return DDERR_INVALIDPARAMS;
    o->RefCount++;
    *ppv = to_guest(o);
    return D3D_OK;
}
EXTERN_C uint32_t IDirect3D3_AddRef_c(d3d_direct3d *o) { return ++o->RefCount; }
EXTERN_C uint32_t IDirect3D3_Release_c(d3d_direct3d *o)
{
    if (--o->RefCount == 0) { x86_free(o); return 0; }
    return o->RefCount;
}

#define D3DCOLOR_MONO 1
#define D3DCOLOR_RGB  2

/* D3DDEVICEDESC (DX6: 252 bytes) */
static void fill_device_desc(uint8_t *d, uint32_t color_model)
{
    uint32_t size = rd32(d);
    if (size < 4 || size > 252) size = 252;
    memset(d, 0, size);
    wr32(d, size);
    if (color_model == 0) return;
    wr32(d + 4, 0x7fff);                 /* dwFlags: all fields valid */
    wr32(d + 8, color_model);
    wr32(d + 12, 0x00000010 | 0x00000040 | 0x00000080 | 0x00000100 | 0x00000200 | 0x00000400);
                                          /* dwDevCaps: EXECUTESYSTEMMEMORY, TLVERTEXSYSTEMMEMORY, TEXTURESYSTEMMEMORY, DRAWPRIMTLVERTEX, ... */
    wr32(d + 16, 8); wr32(d + 20, 1);    /* dtcTransformCaps: D3DTRANSFORMCAPS_CLIP */
    wr32(d + 24, 1);                     /* bClipping */
    wr32(d + 28, 16); wr32(d + 32, 7); wr32(d + 36, color_model == D3DCOLOR_MONO ? 2 : 1); wr32(d + 40, 8);
                                          /* dlcLightingCaps: point/spot/directional, model, 8 lights */
    for (int k = 0; k < 2; k++)
    {
        uint8_t *pc = d + 44 + 56 * k;   /* dpcLineCaps, dpcTriCaps */
        wr32(pc + 0, 56);
        wr32(pc + 4, 0x7f);              /* dwMiscCaps: mask Z, culling */
        wr32(pc + 8, 0x00000001 | 0x00000004 | 0x00000010 | 0x00000080 | 0x00000100);   /* dither, z test, subpixel, fog */
        wr32(pc + 12, 0xff);             /* all z compare functions */
        wr32(pc + 16, 0x1fff);           /* source blend factors */
        wr32(pc + 20, 0x1fff);           /* destination blend factors */
        wr32(pc + 24, 0xff);             /* alpha compare functions */
        wr32(pc + 28, 0x000fffff);       /* shade caps */
        wr32(pc + 32, 0x00000001 | 0x00000004 | 0x00000020 | 0x00000040 | 0x00000080);   /* perspective, alpha, transparency (color key), ... */
        wr32(pc + 36, 0x3f);             /* nearest / linear filters */
        wr32(pc + 40, 0xff);             /* texture blend: decal, modulate, ... */
        wr32(pc + 44, 0x1f);             /* wrap, mirror, clamp, border */
    }
    wr32(d + 156, 0x00000400 | 0x00000800 | 0x00000100);   /* render: DDBD_16, DDBD_8... (16 matters) */
    wr32(d + 160, 0x00000400);           /* z-buffer: DDBD_16 */
    wr32(d + 164, 0);
    wr32(d + 168, 65535);                /* dwMaxVertexCount */
    wr32(d + 172, 1); wr32(d + 176, 1); wr32(d + 180, 2048); wr32(d + 184, 2048);
    wr32(d + 204, 2048); wr32(d + 208, 2048);
    if (size >= 252)
    {
        wrf(d + 216, -2048); wrf(d + 220, -2048); wrf(d + 224, 2048); wrf(d + 228, 2048);
        wr32(d + 240, 0x00000008);       /* dwFVFCaps: 8 texture coordinate sets */
        wr32(d + 244, 0x0003ffff);       /* dwTextureOpCaps */
        d[248] = 1; d[250] = 1;          /* blend stages, simultaneous textures */
    }
}

/* The device GUIDs stay valid after EnumDevices: games keep the pointer. */
static uint8_t *device_guids;
static const uint8_t guid_ramp[16] = { 0x20,0x6b,0x08,0xf2,0x9f,0x25,0xcf,0x11,0xa3,0x1a,0x00,0xaa,0x00,0xb9,0x33,0x56 };
static const uint8_t guid_rgb[16]  = { 0x60,0x5c,0x66,0xa4,0x73,0x26,0xcf,0x11,0xa3,0x1a,0x00,0xaa,0x00,0xb9,0x33,0x56 };
static const uint8_t guid_hal[16]  = { 0xe0,0x3d,0xe6,0x84,0xaa,0x46,0xcf,0x11,0x81,0x6f,0x00,0x00,0xc0,0x20,0x15,0x6e };

EXTERN_C uint32_t IDirect3D3_EnumDevices_c(d3d_direct3d *o, uint32_t callback, void *ctx)
{
    static const char *names[3][2] = {
        { "Ramp Emulation", "Ramp Emulation" },
        { "RGB Emulation", "RGB Emulation" },
        { "Direct3D HAL", "Direct3D HAL" },
    };
    if (device_guids == NULL)
    {
        device_guids = (uint8_t *)x86_calloc(1, 3 * 16 + 3 * 2 * 32);
        memcpy(device_guids, guid_ramp, 16);
        memcpy(device_guids + 16, guid_rgb, 16);
        memcpy(device_guids + 32, guid_hal, 16);
        for (int i = 0; i < 3; i++)
        {
            strcpy((char *)device_guids + 48 + i * 64, names[i][0]);
            strcpy((char *)device_guids + 48 + i * 64 + 32, names[i][1]);
        }
    }
    uint8_t *hw = (uint8_t *)x86_calloc(1, 252), *hel = (uint8_t *)x86_calloc(1, 252);
    for (int i = 0; i < 3; i++)
    {
        wr32(hw, 252);
        wr32(hel, 252);
        fill_device_desc(hw, i == 2 ? D3DCOLOR_RGB : 0);
        fill_device_desc(hel, i == 0 ? D3DCOLOR_MONO : (i == 1 ? D3DCOLOR_RGB : 0));
        uint32_t args[6] = { to_guest(device_guids + 16 * i), to_guest(device_guids + 48 + i * 64),
                             to_guest(device_guids + 48 + i * 64 + 32), to_guest(hw), to_guest(hel), to_guest(ctx) };
        if (CallX86Function(callback, 6, args) == 0) break;   /* D3DENUMRET_CANCEL */
    }
    x86_free(hw);
    x86_free(hel);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3D3_CreateLight_c(d3d_direct3d *o, uint32_t *out, void *u)
{
    return new_object(sizeof(d3d_light), IDirect3DLightVtbl_asm2c, out);
}

EXTERN_C uint32_t IDirect3D3_CreateMaterial_c(d3d_direct3d *o, uint32_t *out, void *u)
{
    return new_object(sizeof(d3d_material), IDirect3DMaterial3Vtbl_asm2c, out);
}

EXTERN_C uint32_t IDirect3D3_CreateViewport_c(d3d_direct3d *o, uint32_t *out, void *u)
{
    uint32_t r = new_object(sizeof(d3d_viewport), IDirect3DViewport3Vtbl_asm2c, out);
    if (r == D3D_OK)
    {
        d3d_viewport *v = (d3d_viewport *)from_guest(*out);
        v->max_z = 1.0f;
    }
    return r;
}

EXTERN_C uint32_t IDirect3D3_FindDevice_c(d3d_direct3d *o, uint8_t *search, uint8_t *result)
{
    /* D3DFINDDEVICERESULT: dwSize, guid, ddHwDesc, ddSwDesc */
    if (result == NULL) return DDERR_INVALIDPARAMS;
    memcpy(result + 4, guid_rgb, 16);
    wr32(result + 20, 252);
    fill_device_desc(result + 20, 0);
    wr32(result + 20 + 252, 252);
    fill_device_desc(result + 20 + 252, D3DCOLOR_RGB);
    return D3D_OK;
}

static void reset_device_state(d3d_device *d)
{
    memset(d->rs, 0, sizeof(d->rs));
    d->rs[3] = 1;       /* TEXTUREADDRESS: WRAP */
    d->rs[4] = 1;       /* TEXTUREPERSPECTIVE */
    d->rs[7] = 1;       /* ZENABLE (a z-buffer is attached) */
    d->rs[8] = 3;       /* FILLMODE: SOLID */
    d->rs[9] = 2;       /* SHADEMODE: GOURAUD */
    d->rs[14] = 1;      /* ZWRITEENABLE */
    d->rs[16] = 1;      /* LASTPIXEL */
    d->rs[17] = 1;      /* TEXTUREMAG: NEAREST */
    d->rs[18] = 1;      /* TEXTUREMIN: NEAREST */
    d->rs[19] = 2;      /* SRCBLEND: ONE */
    d->rs[20] = 1;      /* DESTBLEND: ZERO */
    d->rs[21] = 4;      /* TEXTUREMAPBLEND: MODULATE (DX6 default) */
    d->rs[22] = 3;      /* CULLMODE: CCW */
    d->rs[23] = 4;      /* ZFUNC: LESSEQUAL */
    d->rs[25] = 8;      /* ALPHAFUNC: ALWAYS */
    d->rs[44] = d->rs[45] = 1;   /* TEXTUREADDRESSU/V: WRAP */
    d->rs[60] = 0xffffffff;      /* TEXTUREFACTOR */
    memset(d->ls, 0, sizeof(d->ls));
    d->ls[3] = D3DCOLOR_RGB;
    d->ls[8] = 1;       /* COLORVERTEX */
    memset(d->tss, 0, sizeof(d->tss));
    for (int s = 0; s < 8; s++)
    {
        d->tss[s][1] = s == 0 ? 4 : 1;    /* COLOROP: MODULATE / DISABLE */
        d->tss[s][2] = 2;                 /* COLORARG1: TEXTURE */
        d->tss[s][3] = 1;                 /* COLORARG2: CURRENT */
        d->tss[s][4] = s == 0 ? 2 : 1;    /* ALPHAOP: SELECTARG1 / DISABLE */
        d->tss[s][5] = 2;                 /* ALPHAARG1: TEXTURE */
        d->tss[s][6] = 1;                 /* ALPHAARG2: CURRENT */
        d->tss[s][11] = (uint32_t)s;      /* TEXCOORDINDEX */
        d->tss[s][13] = d->tss[s][14] = 1;   /* ADDRESSU/V: WRAP */
        d->tss[s][16] = 1;                /* MAGFILTER: POINT */
        d->tss[s][17] = 1;                /* MINFILTER: POINT */
        d->tss[s][18] = 1;                /* MIPFILTER: NONE */
    }
    identity(d->world);
    identity(d->view);
    identity(d->proj);
    d->matrices_dirty = 1;
    d->clip_status = 0;
    d->extents[0] = d->extents[2] = d->extents[4] = 1e30f;
    d->extents[1] = d->extents[3] = d->extents[5] = -1e30f;
}

EXTERN_C uint32_t IDirect3D3_CreateDevice_c(d3d_direct3d *o, void *rclsid, void *surface, uint32_t *out, void *u)
{
    uint32_t r = new_object(sizeof(d3d_device), IDirect3DDevice3Vtbl_asm2c, out);
    if (r != D3D_OK) return r;
    d3d_device *d = (d3d_device *)from_guest(*out);
    d->target = surface;
    if (surface) IDirectDrawSurface4_AddRef_c(surface);
    reset_device_state(d);
    current_device = d;
    TRACE1("D3D: CreateDevice(%08x) on surface %p\n", rclsid ? rd32(rclsid) : 0, surface);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3D3_EnumZBufferFormats_c(d3d_direct3d *o, void *riid, uint32_t callback, void *ctx)
{
    uint8_t *pf = (uint8_t *)x86_calloc(1, 32);
    wr32(pf, 32);
    wr32(pf + 4, 0x00000400);    /* DDPF_ZBUFFER */
    wr32(pf + 12, 16);
    wr32(pf + 20, 0xffff);
    uint32_t args[2] = { to_guest(pf), to_guest(ctx) };
    CallX86Function(callback, 2, args);
    x86_free(pf);
    return D3D_OK;
}

/* ------------------------------------------------------- IDirect3DLight */

EXTERN_C uint32_t IDirect3DLight_QueryInterface_c(d3d_light *o, void *riid, uint32_t *ppv) { if (ppv) *ppv = 0; return E_NOINTERFACE; }
EXTERN_C uint32_t IDirect3DLight_AddRef_c(d3d_light *o) { return ++o->RefCount; }
EXTERN_C uint32_t IDirect3DLight_Release_c(d3d_light *o)
{
    if (--o->RefCount == 0) { x86_free(o); return 0; }
    return o->RefCount;
}
EXTERN_C uint32_t IDirect3DLight_Initialize_c(d3d_light *o, void *d3d) { return D3D_OK; }

/* D3DLIGHT2: dwSize, dltType, dcvColor(16), dvPosition(12), dvDirection(12),
 * dvRange, dvFalloff, dvAttenuation0..2, dvTheta, dvPhi, dwFlags (72 bytes) */
EXTERN_C uint32_t IDirect3DLight_SetLight_c(d3d_light *o, uint8_t *l)
{
    if (l == NULL) return DDERR_INVALIDPARAMS;
    uint32_t size = rd32(l);
    o->type = rd32(l + 4);
    read_color(&o->color, l + 8);
    for (int i = 0; i < 3; i++) { o->pos[i] = rdf(l + 24 + 4 * i); o->dir[i] = rdf(l + 36 + 4 * i); }
    o->range = rdf(l + 48);
    o->falloff = rdf(l + 52);
    o->att0 = rdf(l + 56);
    o->att1 = rdf(l + 60);
    o->att2 = rdf(l + 64);
    o->theta = rdf(l + 68);
    o->phi = rdf(l + 72);
    o->flags = size >= 80 ? rd32(l + 76) : 1;   /* D3DLIGHT (v1) has no flags: always active */
    TRACE2("D3D: SetLight type %u color %.2f %.2f %.2f pos %.1f %.1f %.1f dir %.2f %.2f %.2f range %.1f att %.3f %.3f %.3f flags %u\n",
           o->type, o->color.r, o->color.g, o->color.b, o->pos[0], o->pos[1], o->pos[2], o->dir[0], o->dir[1], o->dir[2],
           o->range, o->att0, o->att1, o->att2, o->flags);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DLight_GetLight_c(d3d_light *o, uint8_t *l)
{
    if (l == NULL) return DDERR_INVALIDPARAMS;
    uint32_t size = rd32(l);
    wr32(l + 4, o->type);
    write_color(l + 8, &o->color);
    for (int i = 0; i < 3; i++) { wrf(l + 24 + 4 * i, o->pos[i]); wrf(l + 36 + 4 * i, o->dir[i]); }
    wrf(l + 48, o->range); wrf(l + 52, o->falloff); wrf(l + 56, o->att0); wrf(l + 60, o->att1);
    wrf(l + 64, o->att2); wrf(l + 68, o->theta); wrf(l + 72, o->phi);
    if (size >= 80) wr32(l + 76, o->flags);
    return D3D_OK;
}

/* ---------------------------------------------------- IDirect3DMaterial3 */

EXTERN_C uint32_t IDirect3DMaterial3_QueryInterface_c(d3d_material *o, void *riid, uint32_t *ppv)
{
    if (ppv == NULL) return DDERR_INVALIDPARAMS;
    o->RefCount++;
    *ppv = to_guest(o);
    return D3D_OK;
}
EXTERN_C uint32_t IDirect3DMaterial3_AddRef_c(d3d_material *o) { return ++o->RefCount; }
EXTERN_C uint32_t IDirect3DMaterial3_Release_c(d3d_material *o)
{
    if (--o->RefCount == 0) { x86_free(o); return 0; }
    return o->RefCount;
}

/* D3DMATERIAL: dwSize, diffuse, ambient, specular, emissive (16 each), power, hTexture, dwRampSize */
EXTERN_C uint32_t IDirect3DMaterial3_SetMaterial_c(d3d_material *o, uint8_t *m)
{
    if (m == NULL) return DDERR_INVALIDPARAMS;
    read_color(&o->diffuse, m + 4);
    read_color(&o->ambient, m + 20);
    read_color(&o->specular, m + 36);
    read_color(&o->emissive, m + 52);
    o->power = rdf(m + 68);
    o->texture_handle = rd32(m + 72);
    TRACE2("D3D: SetMaterial %p diffuse %.2f %.2f %.2f %.2f ambient %.2f %.2f %.2f emissive %.2f %.2f %.2f tex %08x\n", (void *)o,
           o->diffuse.r, o->diffuse.g, o->diffuse.b, o->diffuse.a, o->ambient.r, o->ambient.g, o->ambient.b,
           o->emissive.r, o->emissive.g, o->emissive.b, o->texture_handle);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DMaterial3_GetMaterial_c(d3d_material *o, uint8_t *m)
{
    if (m == NULL) return DDERR_INVALIDPARAMS;
    write_color(m + 4, &o->diffuse);
    write_color(m + 20, &o->ambient);
    write_color(m + 36, &o->specular);
    write_color(m + 52, &o->emissive);
    wrf(m + 68, o->power);
    wr32(m + 72, o->texture_handle);
    return D3D_OK;
}

/* Handles are the guest addresses of the objects. */
EXTERN_C uint32_t IDirect3DMaterial3_GetHandle_c(d3d_material *o, void *device, uint32_t *handle)
{
    if (handle) *handle = to_guest(o);
    return D3D_OK;
}

/* ----------------------------------------------------- IDirect3DTexture2 */

EXTERN_C uint32_t D3D_TextureOfSurface(void *surface, uint32_t *ppv)
{
    uint8_t *pixels;
    int pitch, w, h;
    uint32_t tex;
    DDraw_SurfaceInfo4(surface, &pixels, &pitch, &w, &h, NULL, NULL, &tex, NULL);
    if (tex == 0)
    {
        uint32_t r = new_object(sizeof(d3d_texture), IDirect3DTexture2Vtbl_asm2c, &tex);
        if (r != D3D_OK) return r;
        ((d3d_texture *)from_guest(tex))->surface = surface;
        ((d3d_texture *)from_guest(tex))->RefCount = 0;
        DDraw_SetSurfaceTexture(surface, tex);
    }
    d3d_texture *t = (d3d_texture *)from_guest(tex);
    t->RefCount++;
    IDirectDrawSurface4_AddRef_c(surface);
    *ppv = tex;
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DTexture2_QueryInterface_c(d3d_texture *o, void *riid, uint32_t *ppv)
{
    if (ppv == NULL) return DDERR_INVALIDPARAMS;
    if (same_guid(riid, 0x0b2b8630))   /* IID_IDirectDrawSurface4 */
    {
        IDirectDrawSurface4_AddRef_c(o->surface);
        *ppv = to_guest(o->surface);
        return D3D_OK;
    }
    o->RefCount++;
    IDirectDrawSurface4_AddRef_c(o->surface);
    *ppv = to_guest(o);
    return D3D_OK;
}
EXTERN_C uint32_t IDirect3DTexture2_AddRef_c(d3d_texture *o)
{
    IDirectDrawSurface4_AddRef_c(o->surface);
    return ++o->RefCount;
}
EXTERN_C uint32_t IDirect3DTexture2_Release_c(d3d_texture *o)
{
    /* the texture object lives as long as its surface */
    if (o->RefCount > 0) o->RefCount--;
    IDirectDrawSurface4_Release_c(o->surface);
    return o->RefCount;
}
EXTERN_C uint32_t IDirect3DTexture2_GetHandle_c(d3d_texture *o, void *device, uint32_t *handle)
{
    if (handle) *handle = to_guest(o);
    return D3D_OK;
}
EXTERN_C uint32_t IDirect3DTexture2_PaletteChanged_c(d3d_texture *o, uint32_t a, uint32_t b) { return D3D_OK; }

/* Load: copy the pixels of the source texture into this one */
EXTERN_C uint32_t IDirect3DTexture2_Load_c(d3d_texture *o, d3d_texture *src)
{
    uint8_t *dp, *sp;
    int dpitch, spitch, dw, dh, sw, sh;
    if (src == NULL) return DDERR_INVALIDPARAMS;
    DDraw_SurfaceInfo4(o->surface, &dp, &dpitch, &dw, &dh, NULL, NULL, NULL, NULL);
    DDraw_SurfaceInfo4(src->surface, &sp, &spitch, &sw, &sh, NULL, NULL, NULL, NULL);
    int h = dh < sh ? dh : sh;
    int bytes = (dpitch < spitch ? dpitch : spitch);
    for (int y = 0; y < h; y++) memcpy(dp + (size_t)y * dpitch, sp + (size_t)y * spitch, (size_t)bytes);
    uint32_t lo, hi;
    (void)lo; (void)hi;
    return D3D_OK;
}

/* ---------------------------------------------------- IDirect3DViewport3 */

EXTERN_C uint32_t IDirect3DViewport3_QueryInterface_c(d3d_viewport *o, void *riid, uint32_t *ppv)
{
    if (ppv == NULL) return DDERR_INVALIDPARAMS;
    /* IDirect3DViewport, 2 and 3: one vtable (the older ones are prefixes) */
    o->RefCount++;
    *ppv = to_guest(o);
    return D3D_OK;
}
EXTERN_C uint32_t IDirect3DViewport3_AddRef_c(d3d_viewport *o) { return ++o->RefCount; }
EXTERN_C uint32_t IDirect3DViewport3_Release_c(d3d_viewport *o)
{
    if (--o->RefCount == 0)
    {
        if (current_device && current_device->viewport == o) current_device->viewport = NULL;
        x86_free(o);
        return 0;
    }
    return o->RefCount;
}
EXTERN_C uint32_t IDirect3DViewport3_Initialize_c(d3d_viewport *o, void *d3d) { return D3D_OK; }

/* D3DVIEWPORT: dwSize, dwX, dwY, dwWidth, dwHeight, dvScaleX, dvScaleY, dvMaxX, dvMaxY, dvMinZ, dvMaxZ */
EXTERN_C uint32_t IDirect3DViewport3_SetViewport_c(d3d_viewport *o, uint8_t *v)
{
    if (v == NULL) return DDERR_INVALIDPARAMS;
    o->version = 1;
    o->x = rd32(v + 4); o->y = rd32(v + 8); o->width = rd32(v + 12); o->height = rd32(v + 16);
    o->scale_x = rdf(v + 20); o->scale_y = rdf(v + 24); o->max_x = rdf(v + 28); o->max_y = rdf(v + 32);
    o->min_z = rdf(v + 36); o->max_z = rdf(v + 40);
    TRACE1("D3D: SetViewport %u,%u %ux%u scale %.1f %.1f max %.2f %.2f z %.2f..%.2f\n", o->x, o->y, o->width, o->height,
           o->scale_x, o->scale_y, o->max_x, o->max_y, o->min_z, o->max_z);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DViewport3_GetViewport_c(d3d_viewport *o, uint8_t *v)
{
    if (v == NULL) return DDERR_INVALIDPARAMS;
    wr32(v + 4, o->x); wr32(v + 8, o->y); wr32(v + 12, o->width); wr32(v + 16, o->height);
    float sx = o->scale_x, sy = o->scale_y, mx = o->max_x, my = o->max_y;
    if (o->version == 2)
    {
        sx = o->width / o->clip_w; sy = o->height / o->clip_h;
        mx = o->clip_x + o->clip_w; my = o->clip_y;
    }
    wrf(v + 20, sx); wrf(v + 24, sy); wrf(v + 28, mx); wrf(v + 32, my);
    wrf(v + 36, o->min_z); wrf(v + 40, o->max_z);
    return D3D_OK;
}

/* D3DVIEWPORT2: dwSize, dwX, dwY, dwWidth, dwHeight, dvClipX, dvClipY, dvClipWidth, dvClipHeight, dvMinZ, dvMaxZ */
EXTERN_C uint32_t IDirect3DViewport3_SetViewport2_c(d3d_viewport *o, uint8_t *v)
{
    if (v == NULL) return DDERR_INVALIDPARAMS;
    o->version = 2;
    o->x = rd32(v + 4); o->y = rd32(v + 8); o->width = rd32(v + 12); o->height = rd32(v + 16);
    o->clip_x = rdf(v + 20); o->clip_y = rdf(v + 24); o->clip_w = rdf(v + 28); o->clip_h = rdf(v + 32);
    o->min_z = rdf(v + 36); o->max_z = rdf(v + 40);
    TRACE1("D3D: SetViewport2 %u,%u %ux%u clip %.2f %.2f %.2f %.2f z %.2f..%.2f\n", o->x, o->y, o->width, o->height,
           o->clip_x, o->clip_y, o->clip_w, o->clip_h, o->min_z, o->max_z);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DViewport3_GetViewport2_c(d3d_viewport *o, uint8_t *v)
{
    if (v == NULL) return DDERR_INVALIDPARAMS;
    wr32(v + 4, o->x); wr32(v + 8, o->y); wr32(v + 12, o->width); wr32(v + 16, o->height);
    float cx = o->clip_x, cy = o->clip_y, cw = o->clip_w, ch = o->clip_h;
    if (o->version == 1)
    {
        cw = o->width / o->scale_x; ch = o->height / o->scale_y;
        cx = -cw / 2; cy = ch / 2;
    }
    wrf(v + 20, cx); wrf(v + 24, cy); wrf(v + 28, cw); wrf(v + 32, ch);
    wrf(v + 36, o->min_z); wrf(v + 40, o->max_z);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DViewport3_SetBackground_c(d3d_viewport *o, uint32_t hMat) { o->background = hMat; return D3D_OK; }
EXTERN_C uint32_t IDirect3DViewport3_GetBackground_c(d3d_viewport *o, uint32_t *h, uint32_t *valid)
{
    if (h) *h = o->background;
    if (valid) *valid = o->background != 0;
    return D3D_OK;
}
EXTERN_C uint32_t IDirect3DViewport3_SetBackgroundDepth_c(d3d_viewport *o, void *s) { o->background_depth = s; return D3D_OK; }
EXTERN_C uint32_t IDirect3DViewport3_SetBackgroundDepth2_c(d3d_viewport *o, void *s) { o->background_depth = s; return D3D_OK; }
EXTERN_C uint32_t IDirect3DViewport3_GetBackgroundDepth_c(d3d_viewport *o, uint32_t *s, uint32_t *valid)
{
    if (s) *s = to_guest(o->background_depth);
    if (valid) *valid = o->background_depth != NULL;
    return D3D_OK;
}
EXTERN_C uint32_t IDirect3DViewport3_GetBackgroundDepth2_c(d3d_viewport *o, uint32_t *s, uint32_t *valid)
{
    return IDirect3DViewport3_GetBackgroundDepth_c(o, s, valid);
}

EXTERN_C uint32_t IDirect3DViewport3_AddLight_c(d3d_viewport *o, d3d_light *l)
{
    if (l == NULL) return DDERR_INVALIDPARAMS;
    for (int i = 0; i < o->num_lights; i++) if (o->lights[i] == l) return D3D_OK;
    if (o->num_lights >= MAX_LIGHTS) return DDERR_UNSUPPORTED;
    o->lights[o->num_lights++] = l;
    l->RefCount++;
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DViewport3_DeleteLight_c(d3d_viewport *o, d3d_light *l)
{
    for (int i = 0; i < o->num_lights; i++)
    {
        if (o->lights[i] == l)
        {
            memmove(&o->lights[i], &o->lights[i + 1], (size_t)(o->num_lights - i - 1) * sizeof(o->lights[0]));
            o->num_lights--;
            IDirect3DLight_Release_c(l);
            return D3D_OK;
        }
    }
    return DDERR_INVALIDPARAMS;
}

EXTERN_C uint32_t IDirect3DViewport3_NextLight_c(d3d_viewport *o, d3d_light *l, uint32_t *out, uint32_t flags)
{
    int i = -1;
    if (flags == 0x2 /* D3DNEXT_HEAD */) i = 0;
    else if (flags == 0x4 /* D3DNEXT_TAIL */) i = o->num_lights - 1;
    else for (int k = 0; k < o->num_lights; k++) if (o->lights[k] == l) i = k + 1;
    if (i < 0 || i >= o->num_lights) { if (out) *out = 0; return DDERR_INVALIDPARAMS; }
    o->lights[i]->RefCount++;
    if (out) *out = to_guest(o->lights[i]);
    return D3D_OK;
}

static void clear_viewport(d3d_viewport *o, uint32_t count, const int32_t *rects, uint32_t flags, uint32_t color, float z, int use_color);

EXTERN_C uint32_t IDirect3DViewport3_Clear_c(d3d_viewport *o, uint32_t count, int32_t *rects, uint32_t flags)
{
    clear_viewport(o, count, rects, flags, 0, 1.0f, 0);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DViewport3_Clear2_c(d3d_viewport *o, uint32_t count, int32_t *rects, uint32_t flags, uint32_t color, uint32_t z, uint32_t stencil)
{
    clear_viewport(o, count, rects, flags, color, u2f(z), 1);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DViewport3_TransformVertices_c(d3d_viewport *o, uint32_t n, void *data, uint32_t flags, uint32_t *offscreen)
{
    LOG_ONCE("D3D: TransformVertices is not implemented\n");
    if (offscreen) *offscreen = 0;
    return D3D_OK;
}
EXTERN_C uint32_t IDirect3DViewport3_LightElements_c(d3d_viewport *o, uint32_t n, void *data) { return DDERR_UNSUPPORTED; }

/* ======================================================== the rasterizer */

typedef struct {
    uint8_t *pixels;
    int pitch, width, height;
    int r_shift, g_shift, b_shift;     /* 16-bit target: 565 or 555 */
    int r_bits, g_bits, b_bits;
    uint16_t *z;
    int zpitch;                         /* in uint16_t */
} target_info;

typedef struct {
    uint8_t *pixels;
    int pitch, width, height;
    uint32_t rmask, gmask, bmask, amask;
    int rs, gs, bs, as;                 /* shifts to the top bit of an 8-bit value */
    int rb, gb, bb, ab;                 /* bit counts */
    int bpp;
    int has_key;
    uint32_t key_lo, key_hi;
} texture_info;

static int mask_shift(uint32_t m) { int s = 0; if (!m) return 0; while (!(m & 1)) { m >>= 1; s++; } return s; }
static int mask_bits(uint32_t m) { int b = 0; while (m) { b += m & 1; m >>= 1; } return b; }

static int get_target(void *surface, target_info *t)
{
    uint8_t pf[32];
    void *zs = NULL;
    uint32_t caps;
    if (surface == NULL) return 0;
    DDraw_SurfaceInfo4(surface, &t->pixels, &t->pitch, &t->width, &t->height, &caps, &zs, NULL, pf);
    uint32_t rm = rd32(pf + 16), gm = rd32(pf + 20), bm = rd32(pf + 24);
    if (rd32(pf + 12) != 16 || rm == 0) { rm = 0xf800; gm = 0x07e0; bm = 0x001f; }
    t->r_shift = mask_shift(rm); t->g_shift = mask_shift(gm); t->b_shift = mask_shift(bm);
    t->r_bits = mask_bits(rm); t->g_bits = mask_bits(gm); t->b_bits = mask_bits(bm);
    t->z = NULL;
    if (zs)
    {
        uint8_t *zp;
        int zpitch, zw, zh;
        DDraw_SurfaceInfo4(zs, &zp, &zpitch, &zw, &zh, NULL, NULL, NULL, NULL);
        if (zw >= t->width && zh >= t->height)
        {
            t->z = (uint16_t *)zp;
            t->zpitch = zpitch / 2;
        }
    }
    return 1;
}

static void get_texture(d3d_texture *tex, texture_info *ti)
{
    uint8_t pf[32];
    DDraw_SurfaceInfo4(tex->surface, &ti->pixels, &ti->pitch, &ti->width, &ti->height, NULL, NULL, NULL, pf);
    ti->bpp = (int)rd32(pf + 12);
    uint32_t flags = rd32(pf + 4);
    ti->rmask = rd32(pf + 16); ti->gmask = rd32(pf + 20); ti->bmask = rd32(pf + 24);
    ti->amask = (flags & 1) ? rd32(pf + 28) : 0;
    if (ti->bpp == 16 && ti->rmask == 0) { ti->rmask = 0xf800; ti->gmask = 0x07e0; ti->bmask = 0x001f; }
    ti->rs = mask_shift(ti->rmask); ti->gs = mask_shift(ti->gmask); ti->bs = mask_shift(ti->bmask); ti->as = mask_shift(ti->amask);
    ti->rb = mask_bits(ti->rmask); ti->gb = mask_bits(ti->gmask); ti->bb = mask_bits(ti->bmask); ti->ab = mask_bits(ti->amask);
    ti->has_key = DDraw_SurfaceColorKey(tex->surface, &ti->key_lo, &ti->key_hi);
}

static inline uint32_t expand(uint32_t v, int bits)
{
    if (bits <= 0) return 255;
    if (bits >= 8) return v >> (bits - 8);
    v <<= (8 - bits);
    return v | (v >> bits);
}

/* texel as 0xAARRGGBB; *keyed = 1 if it matches the color key */
static inline uint32_t texel(const texture_info *ti, int x, int y, int *keyed)
{
    uint32_t raw;
    const uint8_t *row = ti->pixels + (size_t)y * ti->pitch;
    if (ti->bpp == 16) raw = ((const uint16_t *)row)[x];
    else if (ti->bpp == 32) raw = ((const uint32_t *)row)[x];
    else if (ti->bpp == 8) raw = row[x] * 0x010101u;
    else raw = row[3 * x] | (row[3 * x + 1] << 8) | (row[3 * x + 2] << 16);
    *keyed = ti->has_key && raw >= ti->key_lo && raw <= ti->key_hi;
    uint32_t r = expand((raw & ti->rmask) >> ti->rs, ti->rb);
    uint32_t g = expand((raw & ti->gmask) >> ti->gs, ti->gb);
    uint32_t b = expand((raw & ti->bmask) >> ti->bs, ti->bb);
    uint32_t a = ti->amask ? expand((raw & ti->amask) >> ti->as, ti->ab) : 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* A vertex after transform: screen position, depth, 1/w and attributes */
typedef struct {
    float x, y, z, w;          /* clip space (before the divide) */
    float sx, sy, sz, rhw;     /* screen space */
    float r, g, b, a;          /* diffuse 0..1 */
    float sr, sg, sb, fog;     /* specular 0..1, fog factor (specular alpha) */
    float u, v;
} vtx;

typedef struct {
    int width, height;
    int vx0, vy0, vx1, vy1;    /* viewport rectangle (scissor) */
} raster_ctx;

static target_info cur_target;
static texture_info cur_tex;
static int cur_has_tex;
static d3d_device *cur_dev;
static uint64_t pixels_drawn, triangles_drawn;

static inline int blend_factor(uint32_t f, int c_src, int a_src, int c_dst, int a_dst, int *out_is_dst)
{
    (void)out_is_dst;
    switch (f)
    {
        case 1: return 0;                 /* ZERO */
        case 2: return 255;               /* ONE */
        case 3: return c_src;             /* SRCCOLOR */
        case 4: return 255 - c_src;       /* INVSRCCOLOR */
        case 5: return a_src;             /* SRCALPHA */
        case 6: return 255 - a_src;       /* INVSRCALPHA */
        case 7: return a_dst;             /* DESTALPHA */
        case 8: return 255 - a_dst;       /* INVDESTALPHA */
        case 9: return c_dst;             /* DESTCOLOR */
        case 10: return 255 - c_dst;      /* INVDESTCOLOR */
        case 11: { int f2 = 255 - a_dst; return a_src < f2 ? a_src : f2; }   /* SRCALPHASAT */
        default: return 255;
    }
}

static inline int cmp_pass(uint32_t func, uint32_t a, uint32_t b)
{
    switch (func)
    {
        case 1: return 0;
        case 2: return a < b;
        case 3: return a == b;
        case 4: return a <= b;
        case 5: return a > b;
        case 6: return a != b;
        case 7: return a >= b;
        default: return 1;
    }
}

static inline uint32_t clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (uint32_t)v); }

/* Combine the texture stage 0 color/alpha operation. c = 0xAARRGGBB values. */
static inline uint32_t stage_arg(uint32_t arg, uint32_t diffuse, uint32_t tex, uint32_t spec, uint32_t factor)
{
    uint32_t v;
    switch (arg & 0x0f)
    {
        case 0: v = diffuse; break;       /* D3DTA_DIFFUSE */
        case 1: v = diffuse; break;       /* D3DTA_CURRENT (stage 0: diffuse) */
        case 2: v = tex; break;           /* D3DTA_TEXTURE */
        case 3: v = factor; break;        /* D3DTA_TFACTOR */
        case 4: v = spec; break;          /* D3DTA_SPECULAR */
        default: v = diffuse; break;
    }
    if (arg & 0x10) v = ~v;               /* D3DTA_COMPLEMENT */
    if (arg & 0x20) { uint32_t a = v >> 24; v = (a << 24) | (a << 16) | (a << 8) | a; }   /* ALPHAREPLICATE */
    return v;
}

static inline uint32_t apply_op(uint32_t op, uint32_t a1, uint32_t a2, int shift)
{
    uint32_t c1 = (a1 >> shift) & 0xff, c2 = (a2 >> shift) & 0xff;
    switch (op)
    {
        case 2: return c1;                                  /* SELECTARG1 */
        case 3: return c2;                                  /* SELECTARG2 */
        case 4: return (c1 * c2 + 127) / 255;               /* MODULATE */
        case 5: return clamp8((int)(c1 * c2 * 2 / 255));    /* MODULATE2X */
        case 6: return clamp8((int)(c1 * c2 * 4 / 255));    /* MODULATE4X */
        case 7: return clamp8((int)(c1 + c2));              /* ADD */
        case 8: return clamp8((int)c1 + (int)c2 - 128);     /* ADDSIGNED */
        case 10: return clamp8((int)c1 - (int)c2);          /* SUBTRACT */
        default: return c2;                                 /* DISABLE and the others: current */
    }
}

/* Draw one span of a triangle. Attributes are linear in screen space:
 * z, rhw, u*rhw, v*rhw, colors (Gouraud). */
typedef struct {
    float z, rhw, uw, vw, r, g, b, a, sr, sg, sb;
} span_attr;

static void draw_span(int y, int x0, int x1, const span_attr *s, const span_attr *d)
{
    d3d_device *dev = cur_dev;
    const uint32_t *rs = dev->rs;
    target_info *t = &cur_target;
    uint16_t *row = (uint16_t *)(t->pixels + (size_t)y * t->pitch);
    uint16_t *zrow = t->z ? t->z + (size_t)y * t->zpitch : NULL;
    int zenable = rs[7] != 0 && zrow != NULL;
    int zwrite = rs[14] != 0 && zrow != NULL;
    uint32_t zfunc = rs[23];
    int alpha_test = rs[15] != 0;
    uint32_t alpha_ref = rs[24] & 0xff, alpha_func = rs[25];
    int blend = rs[27] != 0;
    uint32_t src_blend = rs[19], dst_blend = rs[20];
    if (src_blend == 12) { src_blend = 5; dst_blend = 6; }        /* BOTHSRCALPHA */
    else if (src_blend == 13) { src_blend = 6; dst_blend = 5; }   /* BOTHINVSRCALPHA */
    int color_key = rs[41] != 0 && cur_has_tex && cur_tex.has_key;
    int specular = rs[29] != 0;
    int flat = rs[9] == 1;
    uint32_t colorop = dev->tss[0][1], alphaop = dev->tss[0][4];
    uint32_t carg1 = dev->tss[0][2], carg2 = dev->tss[0][3], aarg1 = dev->tss[0][5], aarg2 = dev->tss[0][6];
    int has_tex = cur_has_tex && colorop != 1;
    int bilinear = dev->tss[0][16] == 2 || dev->tss[0][16] == 3;
    uint32_t factor = rs[60];
    int wrap_u = dev->tss[0][13] != 3, wrap_v = dev->tss[0][14] != 3;   /* 3 = CLAMP */
    span_attr a = *s;

    for (int x = x0; x < x1; x++, a.z += d->z, a.rhw += d->rhw, a.uw += d->uw, a.vw += d->vw,
         a.r += d->r, a.g += d->g, a.b += d->b, a.a += d->a, a.sr += d->sr, a.sg += d->sg, a.sb += d->sb)
    {
        uint32_t zval = 0;
        if (zrow)
        {
            float zf = a.z < 0 ? 0 : (a.z > 1 ? 1 : a.z);
            zval = (uint32_t)(zf * 65535.0f + 0.5f);
            if (zenable && !cmp_pass(zfunc, zval, zrow[x])) continue;
        }
        uint32_t diffuse;
        if (flat) diffuse = (clamp8((int)(s->a * 255)) << 24) | (clamp8((int)(s->r * 255)) << 16) | (clamp8((int)(s->g * 255)) << 8) | clamp8((int)(s->b * 255));
        else diffuse = (clamp8((int)(a.a * 255)) << 24) | (clamp8((int)(a.r * 255)) << 16) | (clamp8((int)(a.g * 255)) << 8) | clamp8((int)(a.b * 255));
        uint32_t spec = (clamp8((int)(a.sr * 255)) << 16) | (clamp8((int)(a.sg * 255)) << 8) | clamp8((int)(a.sb * 255));
        uint32_t color;
        if (has_tex)
        {
            float w = 1.0f / a.rhw;
            float u = a.uw * w, v = a.vw * w;
            int keyed;
            uint32_t tc;
            float fu = u * cur_tex.width - (bilinear ? 0.5f : 0.0f), fv = v * cur_tex.height - (bilinear ? 0.5f : 0.0f);
            int iu = (int)floorf(fu), iv = (int)floorf(fv);
            if (!bilinear)
            {
                if (wrap_u) iu = ((iu % cur_tex.width) + cur_tex.width) % cur_tex.width;
                else iu = iu < 0 ? 0 : (iu >= cur_tex.width ? cur_tex.width - 1 : iu);
                if (wrap_v) iv = ((iv % cur_tex.height) + cur_tex.height) % cur_tex.height;
                else iv = iv < 0 ? 0 : (iv >= cur_tex.height ? cur_tex.height - 1 : iv);
                tc = texel(&cur_tex, iu, iv, &keyed);
            }
            else
            {
                int fx = (int)((fu - iu) * 256), fy = (int)((fv - iv) * 256);
                uint32_t c[4];
                int k[4];
                for (int j = 0; j < 4; j++)
                {
                    int xx = iu + (j & 1), yy = iv + (j >> 1);
                    if (wrap_u) xx = ((xx % cur_tex.width) + cur_tex.width) % cur_tex.width;
                    else xx = xx < 0 ? 0 : (xx >= cur_tex.width ? cur_tex.width - 1 : xx);
                    if (wrap_v) yy = ((yy % cur_tex.height) + cur_tex.height) % cur_tex.height;
                    else yy = yy < 0 ? 0 : (yy >= cur_tex.height ? cur_tex.height - 1 : yy);
                    c[j] = texel(&cur_tex, xx, yy, &k[j]);
                }
                /* the nearest texel decides the color key */
                keyed = k[(fy >= 128 ? 2 : 0) + (fx >= 128 ? 1 : 0)];
                tc = 0;
                for (int sh = 0; sh < 32; sh += 8)
                {
                    uint32_t top = ((c[0] >> sh) & 0xff) * (256 - fx) + ((c[1] >> sh) & 0xff) * fx;
                    uint32_t bot = ((c[2] >> sh) & 0xff) * (256 - fx) + ((c[3] >> sh) & 0xff) * fx;
                    tc |= ((top * (256 - fy) + bot * fy) >> 16) << sh;
                }
            }
            if (keyed && color_key) continue;
            uint32_t c1 = stage_arg(carg1, diffuse, tc, spec, factor), c2 = stage_arg(carg2, diffuse, tc, spec, factor);
            uint32_t al1 = stage_arg(aarg1, diffuse, tc, spec, factor), al2 = stage_arg(aarg2, diffuse, tc, spec, factor);
            color = (apply_op(colorop, c1, c2, 16) << 16) | (apply_op(colorop, c1, c2, 8) << 8) | apply_op(colorop, c1, c2, 0);
            color |= (alphaop == 1 ? (diffuse >> 24) : apply_op(alphaop, al1, al2, 24)) << 24;
        }
        else
        {
            color = diffuse;
        }
        if (specular)
        {
            color = (color & 0xff000000u) | (clamp8((int)((color >> 16) & 0xff) + (int)((spec >> 16) & 0xff)) << 16) |
                    (clamp8((int)((color >> 8) & 0xff) + (int)((spec >> 8) & 0xff)) << 8) | clamp8((int)(color & 0xff) + (int)(spec & 0xff));
        }
        uint32_t alpha = color >> 24;
        if (alpha_test && !cmp_pass(alpha_func, alpha, alpha_ref)) continue;
        uint32_t r = (color >> 16) & 0xff, g = (color >> 8) & 0xff, b = color & 0xff;
        if (blend)
        {
            uint16_t p = row[x];
            uint32_t dr = expand((p >> t->r_shift) & ((1u << t->r_bits) - 1), t->r_bits);
            uint32_t dg = expand((p >> t->g_shift) & ((1u << t->g_bits) - 1), t->g_bits);
            uint32_t db = expand((p >> t->b_shift) & ((1u << t->b_bits) - 1), t->b_bits);
            int fs_r = blend_factor(src_blend, (int)r, (int)alpha, (int)dr, 255, NULL);
            int fs_g = blend_factor(src_blend, (int)g, (int)alpha, (int)dg, 255, NULL);
            int fs_b = blend_factor(src_blend, (int)b, (int)alpha, (int)db, 255, NULL);
            int fd_r = blend_factor(dst_blend, (int)r, (int)alpha, (int)dr, 255, NULL);
            int fd_g = blend_factor(dst_blend, (int)g, (int)alpha, (int)dg, 255, NULL);
            int fd_b = blend_factor(dst_blend, (int)b, (int)alpha, (int)db, 255, NULL);
            r = clamp8((int)((r * fs_r + dr * fd_r) / 255));
            g = clamp8((int)((g * fs_g + dg * fd_g) / 255));
            b = clamp8((int)((b * fs_b + db * fd_b) / 255));
        }
        row[x] = (uint16_t)(((r >> (8 - t->r_bits)) << t->r_shift) | ((g >> (8 - t->g_bits)) << t->g_shift) | ((b >> (8 - t->b_bits)) << t->b_shift));
        if (zwrite) zrow[x] = (uint16_t)zval;
        pixels_drawn++;
    }
}

static void set_attr(span_attr *a, const vtx *v)
{
    a->z = v->sz; a->rhw = v->rhw; a->uw = v->u * v->rhw; a->vw = v->v * v->rhw;
    a->r = v->r; a->g = v->g; a->b = v->b; a->a = v->a; a->sr = v->sr; a->sg = v->sg; a->sb = v->sb;
}

#define ATTR_N 11

static void raster_triangle(const vtx *v0, const vtx *v1, const vtx *v2, const raster_ctx *rc)
{
    const vtx *v[3] = { v0, v1, v2 };
    /* sort by y */
    if (v[1]->sy < v[0]->sy) { const vtx *t = v[0]; v[0] = v[1]; v[1] = t; }
    if (v[2]->sy < v[0]->sy) { const vtx *t = v[0]; v[0] = v[2]; v[2] = t; }
    if (v[2]->sy < v[1]->sy) { const vtx *t = v[1]; v[1] = v[2]; v[2] = t; }

    float area = (v[1]->sx - v[0]->sx) * (v[2]->sy - v[0]->sy) - (v[2]->sx - v[0]->sx) * (v[1]->sy - v[0]->sy);
    if (fabsf(area) < 1e-6f) return;

    span_attr A[3];
    for (int i = 0; i < 3; i++) set_attr(&A[i], v[i]);
    float *a0 = (float *)&A[0], *a1 = (float *)&A[1], *a2 = (float *)&A[2];

    /* attribute gradients along x (constant for the triangle) */
    span_attr dx;
    float *pdx = (float *)&dx;
    for (int k = 0; k < ATTR_N; k++)
    {
        float d1 = a1[k] - a0[k], d2 = a2[k] - a0[k];
        pdx[k] = (d1 * (v[2]->sy - v[0]->sy) - d2 * (v[1]->sy - v[0]->sy)) / area;
    }

    int ystart = (int)ceilf(v[0]->sy - 0.5f), yend = (int)ceilf(v[2]->sy - 0.5f);
    if (ystart < rc->vy0) ystart = rc->vy0;
    if (yend > rc->vy1) yend = rc->vy1;
    triangles_drawn++;

    for (int y = ystart; y < yend; y++)
    {
        float py = y + 0.5f;
        /* long edge 0-2, short edge 0-1 or 1-2 */
        float t02 = (py - v[0]->sy) / (v[2]->sy - v[0]->sy);
        float xl = v[0]->sx + (v[2]->sx - v[0]->sx) * t02;
        float xr;
        if (py < v[1]->sy)
        {
            float t = (v[1]->sy - v[0]->sy) > 0 ? (py - v[0]->sy) / (v[1]->sy - v[0]->sy) : 0;
            xr = v[0]->sx + (v[1]->sx - v[0]->sx) * t;
        }
        else
        {
            float t = (v[2]->sy - v[1]->sy) > 0 ? (py - v[1]->sy) / (v[2]->sy - v[1]->sy) : 0;
            xr = v[1]->sx + (v[2]->sx - v[1]->sx) * t;
        }
        /* attributes at the long edge, then move along x */
        span_attr at;
        float *pat = (float *)&at;
        for (int k = 0; k < ATTR_N; k++) pat[k] = a0[k] + (a2[k] - a0[k]) * t02;
        float x0f = xl < xr ? xl : xr, x1f = xl < xr ? xr : xl;
        int x0 = (int)ceilf(x0f - 0.5f), x1 = (int)ceilf(x1f - 0.5f);
        if (x0 < rc->vx0) x0 = rc->vx0;
        if (x1 > rc->vx1) x1 = rc->vx1;
        if (x0 >= x1) continue;
        float step = (x0 + 0.5f) - xl;
        for (int k = 0; k < ATTR_N; k++) pat[k] += pdx[k] * step;
        draw_span(y, x0, x1, &at, &dx);
    }
}

/* ------------------------------------------------- transform and lighting */

typedef struct {
    float m[16];
} mat4;

static void transform_point(const float *m, const float *p, float *out)
{
    for (int j = 0; j < 4; j++)
        out[j] = p[0] * m[0 * 4 + j] + p[1] * m[1 * 4 + j] + p[2] * m[2 * 4 + j] + m[3 * 4 + j];
}

static void light_vertex(d3d_device *d, const float *pos_w, const float *nrm_w, vtx *out)
{
    d3d_material *mat = d->ls[1] ? (d3d_material *)from_guest(d->ls[1]) : NULL;
    uint32_t amb = d->ls[2];
    color4 ambient = { ((amb >> 16) & 0xff) / 255.0f, ((amb >> 8) & 0xff) / 255.0f, (amb & 0xff) / 255.0f, 1 };
    color4 md = { 1, 1, 1, 1 }, ma = { 1, 1, 1, 1 }, me = { 0, 0, 0, 0 };
    if (mat) { md = mat->diffuse; ma = mat->ambient; me = mat->emissive; }
    float r = me.r + ambient.r * ma.r, g = me.g + ambient.g * ma.g, b = me.b + ambient.b * ma.b;
    d3d_viewport *vp = d->viewport;
    if (vp)
    {
        for (int i = 0; i < vp->num_lights; i++)
        {
            d3d_light *l = vp->lights[i];
            if (!(l->flags & 1)) continue;
            float L[3], att = 1.0f;
            if (l->type == 3)   /* directional */
            {
                L[0] = -l->dir[0]; L[1] = -l->dir[1]; L[2] = -l->dir[2];
            }
            else
            {
                L[0] = l->pos[0] - pos_w[0]; L[1] = l->pos[1] - pos_w[1]; L[2] = l->pos[2] - pos_w[2];
                float dist = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
                /* Direct3D 6 and older (D3DLIGHT2) use the distance as a part
                 * of the range, 1 at the light and 0 at the range, and
                 * multiply the light by att0 + att1 * d + att2 * d * d.
                 * (Direct3D 7 divides by the same sum of the real distance.)
                 * Wine does the same for these versions ("legacy lighting"
                 * in dlls/wined3d/glsl_shader.c). Revenant's inventory light
                 * has att 0.1 0.8 1.0: with the Direct3D 7 rule it is almost
                 * zero, and the figure has no shading. */
                if (l->type != 4)
                {
                    if (l->range <= 0 || dist >= l->range) continue;
                    float dn = (l->range - dist) / l->range;
                    att = l->att0 + l->att1 * dn + l->att2 * dn * dn;
                }
                if (dist > 0) { L[0] /= dist; L[1] /= dist; L[2] /= dist; }
                if (l->type == 2)   /* spot */
                {
                    float dl = sqrtf(l->dir[0] * l->dir[0] + l->dir[1] * l->dir[1] + l->dir[2] * l->dir[2]);
                    float rho = dl > 0 ? -(L[0] * l->dir[0] + L[1] * l->dir[1] + L[2] * l->dir[2]) / dl : 1;
                    float ct = cosf(l->theta * 0.5f), cp = cosf(l->phi * 0.5f);
                    if (rho <= cp) continue;
                    if (rho < ct && ct > cp) att *= powf((rho - cp) / (ct - cp), l->falloff > 0 ? l->falloff : 1);
                }
            }
            float ln = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
            if (ln > 0) { L[0] /= ln; L[1] /= ln; L[2] /= ln; }
            float ndl = nrm_w[0] * L[0] + nrm_w[1] * L[1] + nrm_w[2] * L[2];
            if (ndl <= 0) continue;
            r += l->color.r * md.r * ndl * att;
            g += l->color.g * md.g * ndl * att;
            b += l->color.b * md.b * ndl * att;
        }
    }
    if (d->ls[3] == D3DCOLOR_MONO)
    {
        float i = (r * 0.30f + g * 0.59f + b * 0.11f);
        r = g = b = i;
    }
    out->r = r > 1 ? 1 : r; out->g = g > 1 ? 1 : g; out->b = b > 1 ? 1 : b;
    out->a = md.a > 1 ? 1 : (md.a < 0 ? 0 : md.a);
    out->sr = out->sg = out->sb = 0;
    out->fog = 1;
}

static void update_matrices(d3d_device *d)
{
    if (!d->matrices_dirty) return;
    float wv[16];
    matmul(wv, d->world, d->view);
    matmul(d->wvp, wv, d->proj);
    d->matrices_dirty = 0;
}

/* screen mapping of a clip-space vertex */
static void to_screen(d3d_device *d, vtx *v)
{
    d3d_viewport *vp = d->viewport;
    float rhw = 1.0f / v->w;
    float nx = v->x * rhw, ny = v->y * rhw, nz = v->z * rhw;
    if (vp && vp->version == 1)
    {
        v->sx = vp->x + vp->width * 0.5f + nx * vp->scale_x;
        v->sy = vp->y + vp->height * 0.5f - ny * vp->scale_y;
    }
    else if (vp)
    {
        v->sx = vp->x + (nx - vp->clip_x) / vp->clip_w * vp->width;
        v->sy = vp->y + (vp->clip_y - ny) / vp->clip_h * vp->height;
    }
    else
    {
        v->sx = (nx + 1) * 0.5f * cur_target.width;
        v->sy = (1 - ny) * 0.5f * cur_target.height;
    }
    /* The depth range is always 0..1, as in Wine (dlls/ddraw/viewport.c):
     * a D3DVIEWPORT ignores dvMinZ/dvMaxZ (Revenant sets both to 0), and a
     * D3DVIEWPORT2 maps dvMinZ..dvMaxZ to 0..1. */
    v->sz = nz;
    if (vp && vp->version != 1 && vp->max_z != vp->min_z) v->sz = (nz - vp->min_z) / (vp->max_z - vp->min_z);
    v->rhw = rhw;
}

static void lerp_vtx(vtx *o, const vtx *a, const vtx *b, float t)
{
    const float *pa = (const float *)a, *pb = (const float *)b;
    float *po = (float *)o;
    for (size_t k = 0; k < sizeof(vtx) / sizeof(float); k++) po[k] = pa[k] + (pb[k] - pa[k]) * t;
}

/* Clip a polygon (clip space) against z >= 0 and z <= w (and w > eps). */
static int clip_polygon(vtx *in, int n, vtx *out)
{
    vtx tmp[16];
    vtx *src = in, *dst = tmp;
    for (int plane = 0; plane < 2; plane++)
    {
        int m = 0;
        for (int i = 0; i < n; i++)
        {
            vtx *a = &src[i], *b = &src[(i + 1) % n];
            float da = plane == 0 ? a->z : a->w - a->z;
            float db = plane == 0 ? b->z : b->w - b->z;
            if (da >= 0) dst[m++] = *a;
            if ((da >= 0) != (db >= 0) && m < 15)
            {
                float t = da / (da - db);
                lerp_vtx(&dst[m++], a, b, t);
            }
        }
        n = m;
        if (n < 3) return 0;
        if (plane == 0) { src = dst; dst = out; }
        else src = dst;
    }
    if (src != out) memcpy(out, src, sizeof(vtx) * (size_t)n);
    return n;
}

static void draw_triangle_clip(d3d_device *d, vtx *a, vtx *b, vtx *c, int pretransformed, const raster_ctx *rc)
{
    vtx poly[16];
    int n;
    if (pretransformed)
    {
        poly[0] = *a; poly[1] = *b; poly[2] = *c;
        n = 3;
    }
    else
    {
        vtx in[3] = { *a, *b, *c };
        /* everything behind the eye or outside the w > 0 half space */
        if (in[0].w <= 1e-6f && in[1].w <= 1e-6f && in[2].w <= 1e-6f) return;
        n = clip_polygon(in, 3, poly);
        if (n < 3) return;
        for (int i = 0; i < n; i++)
        {
            if (poly[i].w <= 1e-6f) return;
            to_screen(d, &poly[i]);
        }
    }
    if (!d->no_extents)
    {
        float *e = d->extents;
        for (int i = 0; i < n; i++)
        {
            if (poly[i].sx < e[0]) e[0] = poly[i].sx;
            if (poly[i].sx > e[1]) e[1] = poly[i].sx;
            if (poly[i].sy < e[2]) e[2] = poly[i].sy;
            if (poly[i].sy > e[3]) e[3] = poly[i].sy;
            if (poly[i].sz < e[4]) e[4] = poly[i].sz;
            if (poly[i].sz > e[5]) e[5] = poly[i].sz;
        }
    }
    /* culling in screen space (y down): D3D culls CCW by default */
    float cross = (poly[1].sx - poly[0].sx) * (poly[2].sy - poly[0].sy) - (poly[2].sx - poly[0].sx) * (poly[1].sy - poly[0].sy);
    uint32_t cull = d->rs[22];
    if (cull == 2 && cross > 0) return;   /* CW: remove clockwise (on screen) */
    if (cull == 3 && cross < 0) return;   /* CCW */
    for (int i = 1; i + 1 < n; i++) raster_triangle(&poly[0], &poly[i], &poly[i + 1], rc);
}

/* Decode an FVF vertex into vtx (pre-lighting); returns 0 on an unknown format */
typedef struct {
    int pos_type;      /* 0x2 XYZ, 0x4 XYZRHW */
    int off_normal, off_diffuse, off_specular, off_tex;
    int stride;
} fvf_layout;

static int fvf_decode(uint32_t fvf, fvf_layout *l)
{
    int off = 0;
    l->pos_type = (int)(fvf & 0x0e);
    if (l->pos_type == 0x2) off = 12;
    else if (l->pos_type == 0x4) off = 16;
    else if (l->pos_type >= 0x6) off = 12 + 4 * ((l->pos_type - 0x4) / 2);   /* XYZB1.. (blend weights) */
    else return 0;
    l->off_normal = (fvf & 0x10) ? off : -1; if (fvf & 0x10) off += 12;
    if (fvf & 0x20) off += 4;                /* D3DFVF_RESERVED1 (LVERTEX) */
    l->off_diffuse = (fvf & 0x40) ? off : -1; if (fvf & 0x40) off += 4;
    l->off_specular = (fvf & 0x80) ? off : -1; if (fvf & 0x80) off += 4;
    int ntex = (int)((fvf >> 8) & 0xf);
    l->off_tex = ntex > 0 ? off : -1;
    for (int i = 0; i < ntex; i++)
    {
        int fmt = (int)((fvf >> (16 + 2 * i)) & 3);    /* 0: 2 floats, 1: 3, 2: 4, 3: 1 */
        off += fmt == 0 ? 8 : (fmt == 1 ? 12 : (fmt == 2 ? 16 : 4));
    }
    l->stride = off;
    return 1;
}

/* Old D3DVERTEXTYPE values used through DrawPrimitive by DX5 code */
static uint32_t vertex_type_to_fvf(uint32_t vt)
{
    switch (vt)
    {
        case 1: return 0x112;    /* D3DVT_VERTEX */
        case 2: return 0x1e2;    /* D3DVT_LVERTEX */
        case 3: return 0x1c4;    /* D3DVT_TLVERTEX */
        default: return vt;
    }
}

static int prepare_draw(d3d_device *d, raster_ctx *rc)
{
    if (!get_target(d->target, &cur_target)) return 0;
    cur_dev = d;
    d3d_texture *tex = d->texture[0];
    if (tex == NULL && d->rs[1]) tex = (d3d_texture *)from_guest(d->rs[1]);   /* TEXTUREHANDLE */
    cur_has_tex = tex != NULL;
    if (cur_has_tex) get_texture(tex, &cur_tex);
    rc->width = cur_target.width;
    rc->height = cur_target.height;
    rc->vx0 = 0; rc->vy0 = 0; rc->vx1 = cur_target.width; rc->vy1 = cur_target.height;
    if (d->viewport)
    {
        d3d_viewport *vp = d->viewport;
        rc->vx0 = (int)vp->x; rc->vy0 = (int)vp->y;
        rc->vx1 = (int)(vp->x + vp->width); rc->vy1 = (int)(vp->y + vp->height);
        if (rc->vx1 > cur_target.width) rc->vx1 = cur_target.width;
        if (rc->vy1 > cur_target.height) rc->vy1 = cur_target.height;
        if (rc->vx0 < 0) rc->vx0 = 0;
        if (rc->vy0 < 0) rc->vy0 = 0;
    }
    update_matrices(d);
    return 1;
}

static uint32_t draw_primitive(d3d_device *d, uint32_t type, uint32_t fvf, const uint8_t *verts, uint32_t count,
                               const uint16_t *indices, uint32_t icount)
{
    fvf_layout l;
    raster_ctx rc;
    fvf = vertex_type_to_fvf(fvf);
    if (!fvf_decode(fvf, &l))
    {
        LOG_ONCE("D3D: vertex format 0x%x is not supported\n", fvf);
        return D3DERR_INVALIDVERTEXTYPE;
    }
    if (count == 0 || verts == NULL || !prepare_draw(d, &rc)) return D3D_OK;
    {
        static uint32_t seen[16];
        int known = 0;
        for (int i = 0; i < 16 && seen[i]; i++) if (seen[i] == (fvf | (type << 28))) known = 1;
        if (!known)
        {
            for (int i = 0; i < 16; i++) if (!seen[i]) { seen[i] = fvf | (type << 28); break; }
            TRACE1("D3D: first draw with type %u fvf 0x%x (%s, lights %d, texture %d)\n", type, fvf,
                   indices ? "indexed" : "plain", d->viewport ? d->viewport->num_lights : -1, cur_has_tex);
        }
    }

    vtx *tv = (vtx *)malloc(sizeof(vtx) * count);
    if (tv == NULL) return DDERR_OUTOFMEMORY;
    int pretransformed = l.pos_type == 0x4;
    int lighting = !pretransformed && l.off_normal >= 0 && d->rs[137] != 0xdead;   /* DX6: lighting on for normals */
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t *p = verts + (size_t)i * l.stride;
        vtx *o = &tv[i];
        float pos[3] = { rdf(p), rdf(p + 4), rdf(p + 8) };
        if (pretransformed)
        {
            o->sx = pos[0]; o->sy = pos[1]; o->sz = pos[2];
            o->rhw = rdf(p + 12);
            if (o->rhw == 0) o->rhw = 1;
            o->x = o->sx; o->y = o->sy; o->z = o->sz; o->w = 1 / o->rhw;
        }
        else
        {
            float c[4];
            transform_point(d->wvp, pos, c);
            o->x = c[0]; o->y = c[1]; o->z = c[2]; o->w = c[3];
        }
        if (lighting)
        {
            float pw[4], n[3] = { rdf(p + l.off_normal), rdf(p + l.off_normal + 4), rdf(p + l.off_normal + 8) };
            transform_point(d->world, pos, pw);
            float nw[3];
            for (int j = 0; j < 3; j++) nw[j] = n[0] * d->world[0 * 4 + j] + n[1] * d->world[1 * 4 + j] + n[2] * d->world[2 * 4 + j];
            float len = sqrtf(nw[0] * nw[0] + nw[1] * nw[1] + nw[2] * nw[2]);
            if (len > 0) { nw[0] /= len; nw[1] /= len; nw[2] /= len; }
            light_vertex(d, pw, nw, o);
        }
        else if (l.off_diffuse >= 0)
        {
            uint32_t c = rd32(p + l.off_diffuse);
            o->a = (c >> 24) / 255.0f; o->r = ((c >> 16) & 0xff) / 255.0f; o->g = ((c >> 8) & 0xff) / 255.0f; o->b = (c & 0xff) / 255.0f;
        }
        else
        {
            o->r = o->g = o->b = o->a = 1;
        }
        if (l.off_specular >= 0 && !lighting)
        {
            uint32_t c = rd32(p + l.off_specular);
            o->sr = ((c >> 16) & 0xff) / 255.0f; o->sg = ((c >> 8) & 0xff) / 255.0f; o->sb = (c & 0xff) / 255.0f;
            o->fog = (c >> 24) / 255.0f;
        }
        else if (!lighting)
        {
            o->sr = o->sg = o->sb = 0; o->fog = 1;
        }
        if (l.off_tex >= 0) { o->u = rdf(p + l.off_tex); o->v = rdf(p + l.off_tex + 4); }
        else { o->u = o->v = 0; }
    }

    uint32_t n = indices ? icount : count;
#define IDX(k) (indices ? indices[k] : (k))
    switch (type)
    {
        case 4:   /* TRIANGLELIST */
            for (uint32_t k = 0; k + 2 < n; k += 3)
            {
                uint32_t i0 = IDX(k), i1 = IDX(k + 1), i2 = IDX(k + 2);
                if (i0 < count && i1 < count && i2 < count) draw_triangle_clip(d, &tv[i0], &tv[i1], &tv[i2], pretransformed, &rc);
            }
            break;
        case 5:   /* TRIANGLESTRIP */
            for (uint32_t k = 0; k + 2 < n; k++)
            {
                uint32_t i0 = IDX(k), i1 = IDX(k + 1), i2 = IDX(k + 2);
                if (i0 >= count || i1 >= count || i2 >= count) continue;
                if (k & 1) draw_triangle_clip(d, &tv[i1], &tv[i0], &tv[i2], pretransformed, &rc);
                else draw_triangle_clip(d, &tv[i0], &tv[i1], &tv[i2], pretransformed, &rc);
            }
            break;
        case 6:   /* TRIANGLEFAN */
            for (uint32_t k = 1; k + 1 < n; k++)
            {
                uint32_t i0 = IDX(0), i1 = IDX(k), i2 = IDX(k + 1);
                if (i0 < count && i1 < count && i2 < count) draw_triangle_clip(d, &tv[i0], &tv[i1], &tv[i2], pretransformed, &rc);
            }
            break;
        default:
            LOG_ONCE("D3D: primitive type %u (points/lines) is not drawn\n", type);
            break;
    }
#undef IDX
    free(tv);
    return D3D_OK;
}

static void clear_viewport(d3d_viewport *o, uint32_t count, const int32_t *rects, uint32_t flags, uint32_t color, float z, int use_color)
{
    d3d_device *d = current_device;
    target_info t;
    if (d == NULL || !get_target(d->target, &t)) return;
    if (!use_color && (flags & 1))
    {
        d3d_material *m = o->background ? (d3d_material *)from_guest(o->background) : NULL;
        color = m ? ((clamp8((int)(m->diffuse.r * 255)) << 16) | (clamp8((int)(m->diffuse.g * 255)) << 8) | clamp8((int)(m->diffuse.b * 255))) : 0;
    }
    uint16_t pix = (uint16_t)(((((color >> 16) & 0xff) >> (8 - t.r_bits)) << t.r_shift) |
                              ((((color >> 8) & 0xff) >> (8 - t.g_bits)) << t.g_shift) | (((color & 0xff) >> (8 - t.b_bits)) << t.b_shift));
    uint16_t zval = (uint16_t)((z < 0 ? 0 : (z > 1 ? 1 : z)) * 65535.0f);
    uint16_t *bg = NULL;
    int bgpitch = 0;
    if ((flags & 2) && o->background_depth && !use_color)
    {
        uint8_t *bp;
        int bpitch, bw, bh;
        DDraw_SurfaceInfo4(o->background_depth, &bp, &bpitch, &bw, &bh, NULL, NULL, NULL, NULL);
        if (bw >= t.width && bh >= t.height) { bg = (uint16_t *)bp; bgpitch = bpitch / 2; }
    }
    for (uint32_t i = 0; i < (count ? count : 1); i++)
    {
        int x0 = 0, y0 = 0, x1 = t.width, y1 = t.height;
        if (count && rects) { x0 = rects[4 * i]; y0 = rects[4 * i + 1]; x1 = rects[4 * i + 2]; y1 = rects[4 * i + 3]; }
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > t.width) x1 = t.width;
        if (y1 > t.height) y1 = t.height;
        for (int y = y0; y < y1; y++)
        {
            if (flags & 1)
            {
                uint16_t *row = (uint16_t *)(t.pixels + (size_t)y * t.pitch);
                for (int x = x0; x < x1; x++) row[x] = pix;
            }
            if ((flags & 2) && t.z)
            {
                uint16_t *zr = t.z + (size_t)y * t.zpitch;
                if (bg) memcpy(zr + x0, bg + (size_t)y * bgpitch + x0, (size_t)(x1 - x0) * 2);
                else for (int x = x0; x < x1; x++) zr[x] = zval;
            }
        }
    }
}

/* ------------------------------------------------------ IDirect3DDevice3 */

EXTERN_C uint32_t IDirect3DDevice3_QueryInterface_c(d3d_device *d, void *riid, uint32_t *ppv)
{
    if (ppv == NULL) return DDERR_INVALIDPARAMS;
    d->RefCount++;
    *ppv = to_guest(d);
    return D3D_OK;
}
EXTERN_C uint32_t IDirect3DDevice3_AddRef_c(d3d_device *d) { return ++d->RefCount; }
EXTERN_C uint32_t IDirect3DDevice3_Release_c(d3d_device *d)
{
    if (--d->RefCount == 0)
    {
        if (current_device == d) current_device = NULL;
        if (d->target) IDirectDrawSurface4_Release_c(d->target);
        TRACE1("D3D: device released (%llu triangles, %llu pixels)\n", (unsigned long long)triangles_drawn, (unsigned long long)pixels_drawn);
        x86_free(d);
        return 0;
    }
    return d->RefCount;
}

EXTERN_C uint32_t IDirect3DDevice3_GetCaps_c(d3d_device *d, uint8_t *hw, uint8_t *hel)
{
    if (hw) fill_device_desc(hw, D3DCOLOR_RGB);
    if (hel) fill_device_desc(hel, D3DCOLOR_RGB);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DDevice3_GetStats_c(d3d_device *d, uint8_t *st)
{
    if (st) { uint32_t size = rd32(st); memset(st + 4, 0, size > 4 ? size - 4 : 0); }
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DDevice3_AddViewport_c(d3d_device *d, d3d_viewport *v)
{
    if (v == NULL) return DDERR_INVALIDPARAMS;
    v->RefCount++;
    if (d->viewport == NULL) d->viewport = v;
    current_device = d;
    return D3D_OK;
}
EXTERN_C uint32_t IDirect3DDevice3_DeleteViewport_c(d3d_device *d, d3d_viewport *v)
{
    if (d->viewport == v) d->viewport = NULL;
    if (v) IDirect3DViewport3_Release_c(v);
    return D3D_OK;
}
EXTERN_C uint32_t IDirect3DDevice3_NextViewport_c(d3d_device *d, d3d_viewport *v, uint32_t *out, uint32_t f)
{
    if (out) *out = 0;
    return DDERR_INVALIDPARAMS;
}

static uint32_t enum_texture_format(uint32_t cb, void *ctx, uint32_t flags, uint32_t bits, uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    uint8_t *pf = (uint8_t *)x86_calloc(1, 32);
    wr32(pf, 32);
    wr32(pf + 4, flags);
    wr32(pf + 12, bits);
    wr32(pf + 16, r); wr32(pf + 20, g); wr32(pf + 24, b); wr32(pf + 28, a);
    uint32_t args[2] = { to_guest(pf), to_guest(ctx) };
    uint32_t ret = CallX86Function(cb, 2, args);
    x86_free(pf);
    return ret;
}

EXTERN_C uint32_t IDirect3DDevice3_EnumTextureFormats_c(d3d_device *d, uint32_t cb, void *ctx)
{
    /* DDPF_RGB 0x40, DDPF_ALPHAPIXELS 0x1 */
    if (!enum_texture_format(cb, ctx, 0x40, 16, 0xf800, 0x07e0, 0x001f, 0)) return D3D_OK;
    if (!enum_texture_format(cb, ctx, 0x41, 16, 0x7c00, 0x03e0, 0x001f, 0x8000)) return D3D_OK;
    if (!enum_texture_format(cb, ctx, 0x41, 16, 0x0f00, 0x00f0, 0x000f, 0xf000)) return D3D_OK;
    if (!enum_texture_format(cb, ctx, 0x40, 16, 0x7c00, 0x03e0, 0x001f, 0)) return D3D_OK;
    enum_texture_format(cb, ctx, 0x41, 32, 0xff0000, 0x00ff00, 0x0000ff, 0xff000000);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DDevice3_GetDirect3D_c(d3d_device *d, uint32_t *out) { return D3D_CreateDirect3D3(out); }

EXTERN_C uint32_t IDirect3DDevice3_SetCurrentViewport_c(d3d_device *d, d3d_viewport *v)
{
    d->viewport = v;
    current_device = d;
    return D3D_OK;
}
EXTERN_C uint32_t IDirect3DDevice3_GetCurrentViewport_c(d3d_device *d, uint32_t *out)
{
    if (d->viewport == NULL) { if (out) *out = 0; return DDERR_INVALIDPARAMS; }
    d->viewport->RefCount++;
    if (out) *out = to_guest(d->viewport);
    return D3D_OK;
}
EXTERN_C uint32_t IDirect3DDevice3_SetRenderTarget_c(d3d_device *d, void *s, uint32_t f)
{
    if (s) IDirectDrawSurface4_AddRef_c(s);
    if (d->target) IDirectDrawSurface4_Release_c(d->target);
    d->target = s;
    return D3D_OK;
}
EXTERN_C uint32_t IDirect3DDevice3_GetRenderTarget_c(d3d_device *d, uint32_t *out)
{
    if (d->target) IDirectDrawSurface4_AddRef_c(d->target);
    if (out) *out = to_guest(d->target);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DDevice3_GetRenderState_c(d3d_device *d, uint32_t s, uint32_t *v)
{
    if (s >= 256 || v == NULL) return DDERR_INVALIDPARAMS;
    if (s == 1) *v = d->texture[0] ? to_guest(d->texture[0]) : d->rs[1];
    else *v = d->rs[s];
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DDevice3_SetRenderState_c(d3d_device *d, uint32_t s, uint32_t v)
{
    if (s >= 256) return DDERR_INVALIDPARAMS;
    TRACE2("D3D: SetRenderState %u = 0x%x\n", s, v);
    d->rs[s] = v;
    if (s == 1)   /* TEXTUREHANDLE: the same as SetTexture(0) */
        d->texture[0] = v ? (d3d_texture *)from_guest(v) : NULL;
    if (s == 21)  /* TEXTUREMAPBLEND: map to the stage 0 operations */
    {
        switch (v)
        {
            case 1: case 7:  /* DECAL, COPY */
                d->tss[0][1] = 2; d->tss[0][2] = 2; d->tss[0][4] = 2; d->tss[0][5] = 2; break;
            case 2:          /* MODULATE: alpha from the texture if it has alpha */
                d->tss[0][1] = 4; d->tss[0][2] = 2; d->tss[0][3] = 0; d->tss[0][4] = 2; d->tss[0][5] = 2; break;
            case 3:          /* DECALALPHA */
                d->tss[0][1] = 2; d->tss[0][2] = 2; d->tss[0][4] = 3; d->tss[0][6] = 0; break;
            case 4:          /* MODULATEALPHA */
                d->tss[0][1] = 4; d->tss[0][2] = 2; d->tss[0][3] = 0; d->tss[0][4] = 4; d->tss[0][5] = 2; d->tss[0][6] = 0; break;
            case 8:          /* ADD */
                d->tss[0][1] = 7; d->tss[0][2] = 2; d->tss[0][3] = 0; d->tss[0][4] = 3; d->tss[0][6] = 0; break;
            default: break;
        }
    }
    if (s == 17 || s == 18)   /* TEXTUREMAG/MIN: 1 NEAREST, 2 LINEAR, 3.. mip variants */
        d->tss[0][s == 17 ? 16 : 17] = (v == 1 || v == 3 || v == 5) ? 1 : 2;
    if (s == 44) d->tss[0][13] = v;
    if (s == 45) d->tss[0][14] = v;
    if (s == 3) { d->tss[0][13] = d->tss[0][14] = v; }
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DDevice3_GetLightState_c(d3d_device *d, uint32_t s, uint32_t *v)
{
    if (s >= 16 || v == NULL) return DDERR_INVALIDPARAMS;
    *v = d->ls[s];
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DDevice3_SetLightState_c(d3d_device *d, uint32_t s, uint32_t v)
{
    if (s >= 16) return DDERR_INVALIDPARAMS;
    TRACE2("D3D: SetLightState %u = 0x%x\n", s, v);
    d->ls[s] = v;
    return D3D_OK;
}

static float *transform_of(d3d_device *d, uint32_t t)
{
    switch (t)
    {
        case 1: return d->world;
        case 2: return d->view;
        case 3: return d->proj;
        default: return NULL;
    }
}

EXTERN_C uint32_t IDirect3DDevice3_SetTransform_c(d3d_device *d, uint32_t t, uint8_t *m)
{
    float *dst = transform_of(d, t);
    if (dst == NULL || m == NULL) return DDERR_INVALIDPARAMS;
    for (int i = 0; i < 16; i++) dst[i] = rdf(m + 4 * i);
    d->matrices_dirty = 1;
    TRACE2("D3D: SetTransform %u: %g %g %g %g / %g %g %g %g / %g %g %g %g / %g %g %g %g\n", t,
           dst[0], dst[1], dst[2], dst[3], dst[4], dst[5], dst[6], dst[7], dst[8], dst[9], dst[10], dst[11], dst[12], dst[13], dst[14], dst[15]);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DDevice3_GetTransform_c(d3d_device *d, uint32_t t, uint8_t *m)
{
    float *src = transform_of(d, t);
    if (src == NULL || m == NULL) return DDERR_INVALIDPARAMS;
    for (int i = 0; i < 16; i++) wrf(m + 4 * i, src[i]);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DDevice3_MultiplyTransform_c(d3d_device *d, uint32_t t, uint8_t *m)
{
    float *dst = transform_of(d, t), mm[16];
    if (dst == NULL || m == NULL) return DDERR_INVALIDPARAMS;
    for (int i = 0; i < 16; i++) mm[i] = rdf(m + 4 * i);
    matmul(dst, mm, dst);
    d->matrices_dirty = 1;
    return D3D_OK;
}

/* D3DDP_DONOTUPDATEEXTENTS: the draw does not change the clip status extents */
#define D3DDP_DONOTUPDATEEXTENTS 0x8

EXTERN_C uint32_t IDirect3DDevice3_DrawPrimitive_c(d3d_device *d, uint32_t type, uint32_t fvf, uint8_t *verts, uint32_t count, uint32_t flags)
{
    d->no_extents = (flags & D3DDP_DONOTUPDATEEXTENTS) != 0;
    return draw_primitive(d, type, fvf, verts, count, NULL, 0);
}

EXTERN_C uint32_t IDirect3DDevice3_DrawIndexedPrimitive_c(d3d_device *d, uint32_t type, uint32_t fvf, uint8_t *verts, uint32_t count,
                                                          uint16_t *indices, uint32_t icount, uint32_t flags)
{
    d->no_extents = (flags & D3DDP_DONOTUPDATEEXTENTS) != 0;
    return draw_primitive(d, type, fvf, verts, count, indices, icount);
}

/* D3DCLIPSTATUS: dwFlags, dwStatus, minx, maxx, miny, maxy, minz, maxz */
#define D3DCLIPSTATUS_STATUS   0x1
#define D3DCLIPSTATUS_EXTENTS2 0x2
#define D3DCLIPSTATUS_EXTENTS3 0x4

EXTERN_C uint32_t IDirect3DDevice3_SetClipStatus_c(d3d_device *d, uint8_t *cs)
{
    if (cs == NULL) return DDERR_INVALIDPARAMS;
    uint32_t flags = rd32(cs);
    if (flags & D3DCLIPSTATUS_STATUS) d->clip_status = rd32(cs + 4);
    if (flags & (D3DCLIPSTATUS_EXTENTS2 | D3DCLIPSTATUS_EXTENTS3))
    {
        for (int i = 0; i < 4; i++) d->extents[i] = u2f(rd32(cs + 8 + 4 * i));
        if (flags & D3DCLIPSTATUS_EXTENTS3) { d->extents[4] = u2f(rd32(cs + 24)); d->extents[5] = u2f(rd32(cs + 28)); }
    }
    TRACE2("D3D: SetClipStatus flags %x extents %.1f %.1f %.1f %.1f\n", flags, d->extents[0], d->extents[1], d->extents[2], d->extents[3]);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DDevice3_GetClipStatus_c(d3d_device *d, uint8_t *cs)
{
    if (cs == NULL) return DDERR_INVALIDPARAMS;
    wr32(cs, D3DCLIPSTATUS_EXTENTS2);
    wr32(cs + 4, d->clip_status);
    for (int i = 0; i < 6; i++) wr32(cs + 8 + 4 * i, f2u(d->extents[i]));
    TRACE2("D3D: GetClipStatus extents %.1f %.1f %.1f %.1f\n", d->extents[0], d->extents[1], d->extents[2], d->extents[3]);
    return D3D_OK;
}

/* Begin / Vertex / End: collect the vertices, then DrawPrimitive */
EXTERN_C uint32_t IDirect3DDevice3_Begin_c(d3d_device *d, uint32_t type, uint32_t fvf, uint32_t flags)
{
    fvf_layout l;
    if (!fvf_decode(vertex_type_to_fvf(fvf), &l)) return D3DERR_INVALIDVERTEXTYPE;
    d->begin_type = type;
    d->begin_fvf = vertex_type_to_fvf(fvf);
    d->begin_count = 0;
    d->begin_flags = flags;
    free(d->begin_buf);
    d->begin_buf = (uint8_t *)malloc((size_t)l.stride * 1024);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DDevice3_Vertex_c(d3d_device *d, uint8_t *v)
{
    fvf_layout l;
    if (d->begin_buf == NULL || !fvf_decode(d->begin_fvf, &l) || d->begin_count >= 1024) return DDERR_INVALIDPARAMS;
    memcpy(d->begin_buf + (size_t)l.stride * d->begin_count++, v, (size_t)l.stride);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DDevice3_End_c(d3d_device *d, uint32_t flags)
{
    uint32_t r = D3D_OK;
    d->no_extents = (d->begin_flags & D3DDP_DONOTUPDATEEXTENTS) != 0;
    if (d->begin_buf) r = draw_primitive(d, d->begin_type, d->begin_fvf, d->begin_buf, d->begin_count, NULL, 0);
    free(d->begin_buf);
    d->begin_buf = NULL;
    return r;
}

EXTERN_C uint32_t IDirect3DDevice3_GetTexture_c(d3d_device *d, uint32_t stage, uint32_t *out)
{
    if (stage >= 8 || out == NULL) return DDERR_INVALIDPARAMS;
    *out = d->texture[stage] ? to_guest(d->texture[stage]) : 0;
    if (d->texture[stage]) IDirect3DTexture2_AddRef_c(d->texture[stage]);
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DDevice3_SetTexture_c(d3d_device *d, uint32_t stage, d3d_texture *t)
{
    if (stage >= 8) return DDERR_INVALIDPARAMS;
    d->texture[stage] = t;
    if (stage == 0) d->rs[1] = t ? to_guest(t) : 0;
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DDevice3_GetTextureStageState_c(d3d_device *d, uint32_t stage, uint32_t s, uint32_t *v)
{
    if (stage >= 8 || s >= 32 || v == NULL) return DDERR_INVALIDPARAMS;
    *v = d->tss[stage][s];
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DDevice3_SetTextureStageState_c(d3d_device *d, uint32_t stage, uint32_t s, uint32_t v)
{
    if (stage >= 8 || s >= 32) return DDERR_INVALIDPARAMS;
    TRACE2("D3D: SetTextureStageState %u %u = 0x%x\n", stage, s, v);
    d->tss[stage][s] = v;
    return D3D_OK;
}

EXTERN_C uint32_t IDirect3DDevice3_ValidateDevice_c(d3d_device *d, uint32_t *passes)
{
    if (passes) *passes = 1;
    return D3D_OK;
}

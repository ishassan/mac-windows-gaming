/*
 *  Port change: Direct3D 8 on DXVK and MoltenVK: the hand-written methods
 *  (see WinApi-d3d8.h for the wrapper objects; WinApi-d3d8-gen.c has the
 *  methods that only pass the call on).
 *
 *  - Direct3DCreate8 loads DXVK (libdxvk_d3d8), the Vulkan loader and
 *    MoltenVK from GAME_DXVK_DIR (<P>DXVK_DIR, default: the Frameworks
 *    folder of the app).
 *  - The window: DXVK draws into an SDL window made with SDL_WINDOW_VULKAN
 *    (its SDL3 window system code takes the SDL_Window * as HWND). The game
 *    always gets "windowed" from DXVK; full screen is the desktop-size
 *    window of SDL (Cmd+Return switches).
 *  - Locks: the x86 code must get guest memory, and DXVK memory is outside
 *    the 32-bit guest range. A lock keeps the native lock open and gives a
 *    guest buffer; the unlock copies the buffer into the native memory.
 *  - Structures with pointers or handles (D3DPRESENT_PARAMETERS,
 *    D3DLOCKED_RECT, D3DLOCKED_BOX, D3DDEVICE_CREATION_PARAMETERS) are
 *    converted between the 32-bit guest layout and the native layout.
 *
 *  MIT license, see README.md.
 */

#include "game-info.h"
#include "WinApi-d3d8.h"
#include <SDL.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <mach-o/dyld.h>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "Game-Memory.h"
#include "Game-Config.h"

/* vtables (gen_com.py from com/d3d8.com) */
EXTERN_C uint8_t IDirect3D8Vtbl_asm2c[];
EXTERN_C uint8_t IDirect3DDevice8Vtbl_asm2c[];
EXTERN_C uint8_t IDirect3DSwapChain8Vtbl_asm2c[];
EXTERN_C uint8_t IDirect3DSurface8Vtbl_asm2c[];
EXTERN_C uint8_t IDirect3DVolume8Vtbl_asm2c[];
EXTERN_C uint8_t IDirect3DVertexBuffer8Vtbl_asm2c[];
EXTERN_C uint8_t IDirect3DIndexBuffer8Vtbl_asm2c[];
EXTERN_C uint8_t IDirect3DTexture8Vtbl_asm2c[];
EXTERN_C uint8_t IDirect3DCubeTexture8Vtbl_asm2c[];
EXTERN_C uint8_t IDirect3DVolumeTexture8Vtbl_asm2c[];

EXTERN_C void User32_SetClientWindow(SDL_Window *window, int width, int height);
EXTERN_C int Display_TakeShotRequest(char *name, int size);
EXTERN_C void Winapi_DocumentsHostPath(char *out, size_t size);

int d3d8_trace_level(void)
{
    static int level = -1;
    if (level < 0)
    {
        const char *v = game_getenv("TRACE_D3D8");
        level = (v != NULL) ? atoi(v) : 0;
    }
    return level;
}

/* ================================================================ wrappers */

struct lock_info {
    uint8_t *guest = nullptr;      /* the guest buffer */
    void *native = nullptr;        /* the native locked memory */
    uint32_t size = 0;             /* bytes (buffers), or rows x pitch */
    int32_t pitch = 0, slice = 0;   /* pitch: the guest pitch (rows of the guest buffer) */
    int32_t native_pitch = 0;      /* 0: the same as pitch */
    uint32_t row_bytes = 0;        /* bytes of one row to copy when the pitches differ */
    uint32_t rows = 0, depth = 1;
    uint32_t flags = 0;
    bool whole = false;            /* the lock was for the whole surface */
    uint32_t format = 0, width = 0; /* surfaces: for GENERALSZH_DUMP_TEXTURES */
};

struct d3d8_state {
    uint32_t guest = 0;                         /* the wrapper */
    void *native = nullptr;
    std::map<uint32_t, lock_info> locks;        /* key: level | face << 16 (0 for buffers) */
    std::vector<void *> children;               /* surfaces of a texture */
    uint32_t usage = 0;                         /* buffers: D3DUSAGE */
};

static std::recursive_mutex wrap_lock;
static std::unordered_map<void *, uint32_t> wrappers;   /* native -> guest wrapper */

static uint32_t make_wrapper(void *native, uint8_t *vtbl)
{
    if (native == nullptr) return 0;
    std::lock_guard<std::recursive_mutex> g(wrap_lock);
    auto it = wrappers.find(native);
    if (it != wrappers.end())
    {
        /* The device can hold the last reference (SetIndices, SetTexture),
           so an object can be freed in DXVK without a Release here. A new
           object at the same address then finds the old wrapper. Reuse it
           only for the same interface; otherwise make a new wrapper. */
        if (rd32(from_guest(it->second)) == to_guest(vtbl)) return it->second;
        if (d3d8_trace_level() >= 2) fprintf(stderr, "d3d8: stale wrapper 0x%x for %p replaced\n", it->second, native);
        wrappers.erase(it);
    }
    uint8_t *w = (uint8_t *)x86_calloc(1, 24);
    wr32(w, to_guest(vtbl));
    wr32(w + 4, D3D8_MAGIC);
    uint64_t p = (uint64_t)(uintptr_t)native;
    memcpy(w + 8, &p, 8);
    d3d8_state *st = new d3d8_state();
    st->guest = to_guest(w);
    st->native = native;
    uint64_t sp = (uint64_t)(uintptr_t)st;
    memcpy(w + 16, &sp, 8);
    wrappers[native] = st->guest;
    if (d3d8_trace_level() >= 3) fprintf(stderr, "d3d8: wrap %p -> 0x%x\n", native, st->guest);
    return st->guest;
}

/* the wrapper was released for the last time: the native object is gone.
   The guest memory stays (the x86 code may still hold the pointer). */
static void forget_wrapper(void *native)
{
    std::lock_guard<std::recursive_mutex> g(wrap_lock);
    auto it = wrappers.find(native);
    if (it == wrappers.end()) return;
    uint8_t *w = (uint8_t *)from_guest(it->second);
    d3d8_state *st = d3d8_state_of(w);
    for (void *c : st->children) wrappers.erase(c);
    for (auto &l : st->locks) if (l.second.guest) x86_free(l.second.guest);
    wrappers.erase(it);
    uint64_t zero = 0;
    memcpy(w + 8, &zero, 8);
    memcpy(w + 16, &zero, 8);
    delete st;
}

uint32_t d3d8_wrap(IDirect3D8 *o) { return make_wrapper(o, IDirect3D8Vtbl_asm2c); }
uint32_t d3d8_wrap(IDirect3DDevice8 *o) { return make_wrapper(o, IDirect3DDevice8Vtbl_asm2c); }
uint32_t d3d8_wrap(IDirect3DSwapChain8 *o) { return make_wrapper(o, IDirect3DSwapChain8Vtbl_asm2c); }
uint32_t d3d8_wrap(IDirect3DSurface8 *o) { return make_wrapper(o, IDirect3DSurface8Vtbl_asm2c); }
uint32_t d3d8_wrap(IDirect3DVolume8 *o) { return make_wrapper(o, IDirect3DVolume8Vtbl_asm2c); }
uint32_t d3d8_wrap(IDirect3DVertexBuffer8 *o) { return make_wrapper(o, IDirect3DVertexBuffer8Vtbl_asm2c); }
uint32_t d3d8_wrap(IDirect3DIndexBuffer8 *o) { return make_wrapper(o, IDirect3DIndexBuffer8Vtbl_asm2c); }
uint32_t d3d8_wrap(IDirect3DTexture8 *o) { return make_wrapper(o, IDirect3DTexture8Vtbl_asm2c); }
uint32_t d3d8_wrap(IDirect3DCubeTexture8 *o) { return make_wrapper(o, IDirect3DCubeTexture8Vtbl_asm2c); }
uint32_t d3d8_wrap(IDirect3DVolumeTexture8 *o) { return make_wrapper(o, IDirect3DVolumeTexture8Vtbl_asm2c); }

uint32_t d3d8_wrap(IDirect3DResource8 *o)
{
    if (o == nullptr) return 0;
    {
        std::lock_guard<std::recursive_mutex> g(wrap_lock);
        auto it = wrappers.find(o);
        if (it != wrappers.end()) return it->second;
    }
    switch (o->GetType())
    {
        case D3DRTYPE_SURFACE: return d3d8_wrap((IDirect3DSurface8 *)o);
        case D3DRTYPE_VOLUME: return d3d8_wrap((IDirect3DVolume8 *)o);
        case D3DRTYPE_TEXTURE: return d3d8_wrap((IDirect3DTexture8 *)o);
        case D3DRTYPE_VOLUMETEXTURE: return d3d8_wrap((IDirect3DVolumeTexture8 *)o);
        case D3DRTYPE_CUBETEXTURE: return d3d8_wrap((IDirect3DCubeTexture8 *)o);
        case D3DRTYPE_VERTEXBUFFER: return d3d8_wrap((IDirect3DVertexBuffer8 *)o);
        case D3DRTYPE_INDEXBUFFER: return d3d8_wrap((IDirect3DIndexBuffer8 *)o);
        default: return 0;
    }
}

uint32_t d3d8_wrap(IDirect3DBaseTexture8 *o) { return d3d8_wrap((IDirect3DResource8 *)o); }

/* wrapper of an object that QueryInterface or GetContainer returns */
static uint32_t wrap_by_iid(REFIID riid, void *o)
{
    if (o == nullptr) return 0;
    if (riid == IID_IDirect3D8) return d3d8_wrap((IDirect3D8 *)o);
    if (riid == IID_IDirect3DDevice8) return d3d8_wrap((IDirect3DDevice8 *)o);
    if (riid == IID_IDirect3DSwapChain8) return d3d8_wrap((IDirect3DSwapChain8 *)o);
    if (riid == IID_IDirect3DSurface8) return d3d8_wrap((IDirect3DSurface8 *)o);
    if (riid == IID_IDirect3DVolume8) return d3d8_wrap((IDirect3DVolume8 *)o);
    if (riid == IID_IDirect3DVertexBuffer8) return d3d8_wrap((IDirect3DVertexBuffer8 *)o);
    if (riid == IID_IDirect3DIndexBuffer8) return d3d8_wrap((IDirect3DIndexBuffer8 *)o);
    if (riid == IID_IDirect3DTexture8) return d3d8_wrap((IDirect3DTexture8 *)o);
    if (riid == IID_IDirect3DCubeTexture8) return d3d8_wrap((IDirect3DCubeTexture8 *)o);
    if (riid == IID_IDirect3DVolumeTexture8) return d3d8_wrap((IDirect3DVolumeTexture8 *)o);
    if (riid == IID_IDirect3DBaseTexture8) return d3d8_wrap((IDirect3DBaseTexture8 *)o);
    if (riid == IID_IDirect3DResource8) return d3d8_wrap((IDirect3DResource8 *)o);
    /* IUnknown: keep the wrapper of the object if it has one */
    std::lock_guard<std::recursive_mutex> g(wrap_lock);
    auto it = wrappers.find(o);
    return (it != wrappers.end()) ? it->second : 0;
}

/* QueryInterface and Release of every interface */
#define D3D8_IUNKNOWN(IFACE) \
EXTERN_C uint32_t IFACE##_QueryInterface_c(void *self, void *riid, void *ppvObject) \
{ \
    IFACE *obj = d3d8_native<IFACE>(self); \
    void *out = nullptr; \
    HRESULT r = obj->QueryInterface(*(const IID *)riid, &out); \
    if (ppvObject) wr32(ppvObject, SUCCEEDED(r) ? wrap_by_iid(*(const IID *)riid, out) : 0); \
    D3D8_TRACE(#IFACE "::QueryInterface", r); \
    return (uint32_t)r; \
} \
EXTERN_C uint32_t IFACE##_Release_c(void *self) \
{ \
    IFACE *obj = d3d8_native<IFACE>(self); \
    if (obj == nullptr) return 0; \
    ULONG r = obj->Release(); \
    if (r == 0) forget_wrapper(obj); \
    return r; \
}

D3D8_IUNKNOWN(IDirect3D8)
D3D8_IUNKNOWN(IDirect3DDevice8)
D3D8_IUNKNOWN(IDirect3DSwapChain8)
D3D8_IUNKNOWN(IDirect3DSurface8)
D3D8_IUNKNOWN(IDirect3DVolume8)
D3D8_IUNKNOWN(IDirect3DVertexBuffer8)
D3D8_IUNKNOWN(IDirect3DIndexBuffer8)
D3D8_IUNKNOWN(IDirect3DTexture8)
D3D8_IUNKNOWN(IDirect3DCubeTexture8)
D3D8_IUNKNOWN(IDirect3DVolumeTexture8)
D3D8_IUNKNOWN(IDirect3DBaseTexture8)
D3D8_IUNKNOWN(IDirect3DResource8)

EXTERN_C uint32_t IDirect3DSurface8_GetContainer_c(void *self, void *riid, void *ppContainer)
{
    void *out = nullptr;
    HRESULT r = d3d8_native<IDirect3DSurface8>(self)->GetContainer(*(const IID *)riid, &out);
    if (ppContainer) wr32(ppContainer, SUCCEEDED(r) ? wrap_by_iid(*(const IID *)riid, out) : 0);
    return (uint32_t)r;
}

EXTERN_C uint32_t IDirect3DVolume8_GetContainer_c(void *self, void *riid, void *ppContainer)
{
    void *out = nullptr;
    HRESULT r = d3d8_native<IDirect3DVolume8>(self)->GetContainer(*(const IID *)riid, &out);
    if (ppContainer) wr32(ppContainer, SUCCEEDED(r) ? wrap_by_iid(*(const IID *)riid, out) : 0);
    return (uint32_t)r;
}

/* ================================================================ DXVK */

static void *dxvk_d3d8;
static IDirect3D8 *(*dxvk_Direct3DCreate8)(UINT);

static std::string exe_dir(void)
{
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return ".";
    char real[PATH_MAX];
    if (realpath(buf, real) == NULL) return ".";
    std::string s(real);
    return s.substr(0, s.rfind('/'));
}

static int file_exists(const std::string &p)
{
    return access(p.c_str(), R_OK) == 0;
}

static int load_dxvk(void)
{
    if (dxvk_d3d8 != NULL) return 0;
    const char *env = game_getenv("DXVK_DIR");
    std::string dir = env ? env : exe_dir() + "/../Frameworks";

    /* The Vulkan loader finds MoltenVK through its driver file. */
    std::string icd = dir + "/MoltenVK_icd.json";
    if (!file_exists(icd)) icd = dir + "/../MoltenVK_icd.json";
    if (!file_exists(icd)) icd = dir + "/../Resources/MoltenVK_icd.json";   /* app bundle */
    if (getenv("VK_DRIVER_FILES") == NULL && file_exists(icd))
    {
        setenv("VK_DRIVER_FILES", icd.c_str(), 1);
        setenv("VK_ICD_FILENAMES", icd.c_str(), 1);
    }
    setenv("DXVK_WSI_DRIVER", "SDL3", 1);
    /* DXVK writes its shader cache and log into the current folder (the game
       data). Put the cache with the saves, and write no log files. */
    if (getenv("DXVK_STATE_CACHE_PATH") == NULL)
    {
        char docs[4096];
        Winapi_DocumentsHostPath(docs, sizeof(docs));
        if (docs[0]) setenv("DXVK_STATE_CACHE_PATH", docs, 1);
    }
    if (getenv("DXVK_LOG_PATH") == NULL) setenv("DXVK_LOG_PATH", "none", 1);
    if (getenv("DXVK_LOG_LEVEL") == NULL && d3d8_trace_level() == 0) setenv("DXVK_LOG_LEVEL", "none", 1);
    if (getenv("MVK_CONFIG_LOG_LEVEL") == NULL && d3d8_trace_level() == 0) setenv("MVK_CONFIG_LOG_LEVEL", "0", 1);
    if (getenv("DXVK_HUD") == NULL) setenv("DXVK_HUD", "0", 1);
    /* The terrain shaders of Generals on MoltenVK (as in the community port) */
    if (getenv("DXVK_CONFIG") == NULL) setenv("DXVK_CONFIG", "d3d9.forceSamplerTypeSpecConstants = False", 1);

    /* Load by full path first: DXVK then finds them by name. libSDL3 is the
       one that the SDL2 library of the runtime (sdl2-compat) uses. */
    const char *libs[] = { "libvulkan.1.dylib", "libMoltenVK.dylib", "libdxvk_d3d9.0.dylib", "libdxvk_d3d8.0.dylib" };
    for (const char *l : libs)
    {
        std::string p = dir + "/" + l;
        void *h = dlopen(p.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (h == NULL)
        {
            fprintf(stderr, "d3d8: cannot load %s: %s\n", p.c_str(), dlerror());
            return -1;
        }
        if (strcmp(l, "libdxvk_d3d8.0.dylib") == 0) dxvk_d3d8 = h;
    }
    dxvk_Direct3DCreate8 = (IDirect3D8 *(*)(UINT))dlsym(dxvk_d3d8, "Direct3DCreate8");
    if (dxvk_Direct3DCreate8 == NULL)
    {
        fprintf(stderr, "d3d8: Direct3DCreate8 not found in DXVK\n");
        return -1;
    }
    if (d3d8_trace_level()) fprintf(stderr, "d3d8: DXVK loaded from %s\n", dir.c_str());
    return 0;
}

EXTERN_C void *Direct3DCreate8_c(uint32_t SDKVersion)
{
    if (load_dxvk() != 0) return NULL;
    IDirect3D8 *d3d = dxvk_Direct3DCreate8(SDKVersion);
    if (d3d8_trace_level()) fprintf(stderr, "d3d8: Direct3DCreate8(%u) -> %p\n", SDKVersion, (void *)d3d);
    uint32_t w = d3d8_wrap(d3d);
    return w ? from_guest(w) : NULL;
}

/* ================================================================ window */

static SDL_Window *d3d_window;
static void *game_hwnd;          /* the guest HWND of the game window (host pointer) */
static int bb_width, bb_height;

static SDL_Window *get_window(int width, int height)
{
    if (d3d_window == NULL)
    {
        int w = (Display_Width != 0) ? Display_Width : width;
        int h = (Display_Height != 0) ? Display_Height : height;
        Uint32 flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
        if (Display_Mode != 0) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        d3d_window = SDL_CreateWindow(GAME_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
        if (d3d_window == NULL)
        {
            fprintf(stderr, "d3d8: SDL_CreateWindow: %s\n", SDL_GetError());
            return NULL;
        }
        /* the window procedure waits for focus, as on Windows */
        SDL_Event event;
        memset(&event, 0, sizeof(event));
        event.type = SDL_WINDOWEVENT;
        event.window.event = SDL_WINDOWEVENT_SHOWN;
        event.window.windowID = SDL_GetWindowID(d3d_window);
        SDL_PushEvent(&event);
    }
    bb_width = width;
    bb_height = height;
    User32_SetClientWindow(d3d_window, width, height);
    return d3d_window;
}

/* Cmd+Return / Alt+Return (WinApi-user32.c calls the DirectDraw function;
   it does the same for this window) */
EXTERN_C void D3D8_ToggleFullscreen(void)
{
    if (d3d_window == NULL) return;
    int full = (SDL_GetWindowFlags(d3d_window) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(d3d_window, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    Display_Mode = full ? 0 : 1;
}

EXTERN_C int D3D8_HasWindow(void)
{
    return d3d_window != NULL;
}

HWND d3d8_hwnd(void *guest_hwnd)
{
    return (HWND)d3d_window;
}

/* D3DPRESENT_PARAMETERS: 13 dwords in the guest; hDeviceWindow is a pointer */
static void present_params_from_guest(D3DPRESENT_PARAMETERS *pp, const uint8_t *g)
{
    memset(pp, 0, sizeof(*pp));
    pp->BackBufferWidth = rd32(g + 0);
    pp->BackBufferHeight = rd32(g + 4);
    pp->BackBufferFormat = (D3DFORMAT)rd32(g + 8);
    pp->BackBufferCount = rd32(g + 12);
    pp->MultiSampleType = (D3DMULTISAMPLE_TYPE)rd32(g + 16);
    pp->SwapEffect = (D3DSWAPEFFECT)rd32(g + 20);
    pp->hDeviceWindow = NULL;
    pp->Windowed = rd32(g + 28);
    pp->EnableAutoDepthStencil = rd32(g + 32);
    pp->AutoDepthStencilFormat = (D3DFORMAT)rd32(g + 36);
    pp->Flags = rd32(g + 40);
    pp->FullScreen_RefreshRateInHz = rd32(g + 44);
    pp->FullScreen_PresentationInterval = rd32(g + 48);

    if (d3d8_trace_level())
    {
        fprintf(stderr, "d3d8: present parameters %ux%u format %d count %u swap %d windowed %d depth %d/%d flags 0x%x interval 0x%x\n",
                pp->BackBufferWidth, pp->BackBufferHeight, pp->BackBufferFormat, pp->BackBufferCount, pp->SwapEffect,
                pp->Windowed, pp->EnableAutoDepthStencil, pp->AutoDepthStencilFormat, pp->Flags, pp->FullScreen_PresentationInterval);
    }

    /* The game window size: with 0 (windowed) the client size of the window */
    if (pp->BackBufferWidth == 0) pp->BackBufferWidth = 800;
    if (pp->BackBufferHeight == 0) pp->BackBufferHeight = 600;
    /* Always windowed for DXVK: the SDL window is the full screen */
    pp->Windowed = TRUE;
    pp->FullScreen_RefreshRateInHz = 0;
    /* windowed: Direct3D 8 allows only the default interval (DXVK then
       presents at once). VSync=on: the swap effect that waits for it. */
    pp->FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    if (Display_VSync)
    {
        pp->SwapEffect = D3DSWAPEFFECT_COPY_VSYNC;
        pp->BackBufferCount = 1;
    }
    if (pp->BackBufferFormat == D3DFMT_UNKNOWN) pp->BackBufferFormat = D3DFMT_X8R8G8B8;
}

static void present_params_to_guest(uint8_t *g, const D3DPRESENT_PARAMETERS *pp)
{
    /* only the values that Direct3D changes: the back buffer count and format */
    wr32(g + 8, pp->BackBufferFormat);
    wr32(g + 12, pp->BackBufferCount);
}

EXTERN_C uint32_t IDirect3D8_CreateDevice_c(void *self, uint32_t Adapter, uint32_t DeviceType, void *hFocusWindow,
                                            uint32_t BehaviorFlags, void *pPresentationParameters, void *ppReturnedDeviceInterface)
{
    IDirect3D8 *d3d = d3d8_native<IDirect3D8>(self);
    D3DPRESENT_PARAMETERS pp;
    present_params_from_guest(&pp, (const uint8_t *)pPresentationParameters);
    game_hwnd = hFocusWindow;
    SDL_Window *win = get_window(pp.BackBufferWidth, pp.BackBufferHeight);
    if (win == NULL) return (uint32_t)D3DERR_NOTAVAILABLE;
    pp.hDeviceWindow = (HWND)win;
    IDirect3DDevice8 *dev = nullptr;
    /* the translated code is not thread safe with DXVK's own threads;
       multithreaded keeps DXVK's calls serialized */
    HRESULT r = d3d->CreateDevice(Adapter, (D3DDEVTYPE)DeviceType, (HWND)win, BehaviorFlags | D3DCREATE_MULTITHREADED, &pp, &dev);
    if (SUCCEEDED(r)) present_params_to_guest((uint8_t *)pPresentationParameters, &pp);
    if (ppReturnedDeviceInterface) wr32(ppReturnedDeviceInterface, SUCCEEDED(r) ? d3d8_wrap(dev) : 0);
    if (d3d8_trace_level()) fprintf(stderr, "d3d8: CreateDevice(adapter %u, type %u, flags 0x%x) -> 0x%x\n", Adapter, DeviceType, BehaviorFlags, (unsigned)r);
    return (uint32_t)r;
}

EXTERN_C uint32_t IDirect3DDevice8_Reset_c(void *self, void *pPresentationParameters)
{
    IDirect3DDevice8 *dev = d3d8_native<IDirect3DDevice8>(self);
    D3DPRESENT_PARAMETERS pp;
    present_params_from_guest(&pp, (const uint8_t *)pPresentationParameters);
    SDL_Window *win = get_window(pp.BackBufferWidth, pp.BackBufferHeight);
    pp.hDeviceWindow = (HWND)win;
    HRESULT r = dev->Reset(&pp);
    if (SUCCEEDED(r)) present_params_to_guest((uint8_t *)pPresentationParameters, &pp);
    if (d3d8_trace_level()) fprintf(stderr, "d3d8: Reset(%ux%u) -> 0x%x\n", pp.BackBufferWidth, pp.BackBufferHeight, (unsigned)r);
    return (uint32_t)r;
}

EXTERN_C uint32_t IDirect3DDevice8_CreateAdditionalSwapChain_c(void *self, void *pPresentationParameters, void *pSwapChain)
{
    IDirect3DDevice8 *dev = d3d8_native<IDirect3DDevice8>(self);
    D3DPRESENT_PARAMETERS pp;
    present_params_from_guest(&pp, (const uint8_t *)pPresentationParameters);
    pp.hDeviceWindow = (HWND)d3d_window;
    IDirect3DSwapChain8 *sc = nullptr;
    HRESULT r = dev->CreateAdditionalSwapChain(&pp, &sc);
    if (pSwapChain) wr32(pSwapChain, SUCCEEDED(r) ? d3d8_wrap(sc) : 0);
    return (uint32_t)r;
}

EXTERN_C uint32_t IDirect3DDevice8_GetCreationParameters_c(void *self, void *pParameters)
{
    D3DDEVICE_CREATION_PARAMETERS cp;
    HRESULT r = d3d8_native<IDirect3DDevice8>(self)->GetCreationParameters(&cp);
    if (SUCCEEDED(r) && pParameters)
    {
        uint8_t *g = (uint8_t *)pParameters;
        wr32(g + 0, cp.AdapterOrdinal);
        wr32(g + 4, cp.DeviceType);
        wr32(g + 8, to_guest(game_hwnd));
        wr32(g + 12, cp.BehaviorFlags & ~(DWORD)D3DCREATE_MULTITHREADED);
    }
    return (uint32_t)r;
}

void d3d8_fix_display_mode(void *mode)
{
    if (mode == NULL) return;
    D3DDISPLAYMODE *m = (D3DDISPLAYMODE *)mode;
    if (m->Format == D3DFMT_UNKNOWN) m->Format = D3DFMT_X8R8G8B8;
}

/* SDL's offscreen video driver has no display modes. Then the game finds no
   mode of 32 bits and takes another device set-up than with a window.
   Report the usual desktop modes in that case. */
static const uint32_t fallback_modes[][2] = {
    {640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1280, 800}, {1280, 1024},
    {1440, 900}, {1600, 900}, {1680, 1050}, {1920, 1080}, {1920, 1200},
};
static const uint32_t fallback_mode_count = sizeof(fallback_modes) / sizeof(fallback_modes[0]);

EXTERN_C uint32_t IDirect3D8_GetAdapterModeCount_c(void *self, uint32_t Adapter)
{
    uint32_t r = d3d8_native<IDirect3D8>(self)->GetAdapterModeCount((UINT)Adapter);
    if (r == 0 && Adapter == 0) r = fallback_mode_count;
    D3D8_TRACE("IDirect3D8::GetAdapterModeCount", r);
    return r;
}

EXTERN_C uint32_t IDirect3D8_EnumAdapterModes_c(void *self, uint32_t Adapter, uint32_t Mode, void *pMode)
{
    IDirect3D8 *d3d = d3d8_native<IDirect3D8>(self);
    HRESULT r;
    if (Adapter == 0 && d3d->GetAdapterModeCount(0) == 0)
    {
        if (Mode >= fallback_mode_count || pMode == NULL) r = D3DERR_INVALIDCALL;
        else
        {
            D3DDISPLAYMODE *m = (D3DDISPLAYMODE *)pMode;
            m->Width = fallback_modes[Mode][0];
            m->Height = fallback_modes[Mode][1];
            m->RefreshRate = 60;
            m->Format = D3DFMT_X8R8G8B8;
            r = D3D_OK;
        }
    }
    else
    {
        r = d3d->EnumAdapterModes((UINT)Adapter, (UINT)Mode, (D3DDISPLAYMODE *)pMode);
        d3d8_fix_display_mode(pMode);
    }
    D3D8_TRACE("IDirect3D8::EnumAdapterModes", r);
    return (uint32_t)r;
}

/* ================================================================ present, screenshots */

static void save_frame(IDirect3DDevice8 *dev, const char *path)
{
    IDirect3DSurface8 *bb = nullptr;
    if (FAILED(dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb)) || bb == nullptr) return;
    D3DSURFACE_DESC desc;
    bb->GetDesc(&desc);
    IDirect3DSurface8 *img = nullptr;
    /* CopyRects needs the same format; 32-bit formats only */
    if (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8)
    {
        if (d3d8_trace_level()) fprintf(stderr, "d3d8: shot: back buffer format %u not supported\n", (unsigned)desc.Format);
        bb->Release();
        return;
    }
    if (SUCCEEDED(dev->CreateImageSurface(desc.Width, desc.Height, desc.Format, &img)))
    {
        if (SUCCEEDED(dev->CopyRects(bb, NULL, 0, img, NULL)))
        {
            D3DLOCKED_RECT lr;
            if (SUCCEEDED(img->LockRect(&lr, NULL, D3DLOCK_READONLY)))
            {
                /* without alpha: the back buffer alpha is not a part of the
                   picture (the game writes alpha 0 in places) */
                SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(lr.pBits, desc.Width, desc.Height, 32, lr.Pitch, SDL_PIXELFORMAT_RGB888);
                if (s)
                {
                    SDL_SaveBMP(s, path);
                    SDL_FreeSurface(s);
                }
                img->UnlockRect();
            }
        }
        else if (d3d8_trace_level()) fprintf(stderr, "d3d8: shot: CopyRects failed\n");
        img->Release();
    }
    bb->Release();
}

static void frame_hooks(IDirect3DDevice8 *dev)
{
    static int count;
    static const char *dump = game_getenv("DUMP");
    char name[128], path[1024];
    if (Display_TakeShotRequest(name, sizeof(name)))
    {
        snprintf(path, sizeof(path), "%s/%s.bmp", dump ? dump : "/tmp", name);
        save_frame(dev, path);
        fprintf(stderr, "shot: %s\n", path);
    }
    if (dump != NULL && (count++ % 30) == 0)
    {
        snprintf(path, sizeof(path), "%s/frame-%05d.bmp", dump, count / 30);
        save_frame(dev, path);
        if (count / 30 >= 20)
        {
            snprintf(path, sizeof(path), "%s/frame-%05d.bmp", dump, count / 30 - 20);
            remove(path);
        }
    }
}

EXTERN_C void LagTrace_Present(Uint64 t_start, Uint64 t_upload, Uint64 t_end);

EXTERN_C uint32_t IDirect3DDevice8_Present_c(void *self, void *pSourceRect, void *pDestRect, void *hDestWindowOverride, void *pDirtyRegion)
{
    IDirect3DDevice8 *dev = d3d8_native<IDirect3DDevice8>(self);
    frame_hooks(dev);
    Uint64 t0 = SDL_GetPerformanceCounter();
    HRESULT r = dev->Present((const RECT *)pSourceRect, (const RECT *)pDestRect, NULL, NULL);
    Uint64 t1 = SDL_GetPerformanceCounter();
    LagTrace_Present(t0, t0, t1);
    D3D8_TRACE("IDirect3DDevice8::Present", r);
    return (uint32_t)r;
}

EXTERN_C uint32_t IDirect3DSwapChain8_Present_c(void *self, void *pSourceRect, void *pDestRect, void *hDestWindowOverride, void *pDirtyRegion)
{
    return (uint32_t)d3d8_native<IDirect3DSwapChain8>(self)->Present((const RECT *)pSourceRect, (const RECT *)pDestRect, NULL, NULL);
}

/* ================================================================ locks */

static d3d8_state *state(void *self) { return d3d8_state_of(self); }

static uint8_t *guest_buffer(lock_info &l, uint32_t size)
{
    if (l.guest == nullptr || l.size < size)
    {
        if (l.guest) x86_free(l.guest);
        l.guest = (uint8_t *)x86_malloc(size ? size : 4);
        l.size = size;
    }
    return l.guest;
}

/* Debug aid: <GAME>_DUMP_TEXTURES=<folder> saves each whole-surface lock
   that the game wrote (16- and 32-bit formats) as a BMP file. */
static void dump_texture(const lock_info &l)
{
    static const char *dir = game_getenv("DUMP_TEXTURES");
    static int count;
    if (dir == NULL || !l.whole || (l.flags & D3DLOCK_READONLY) || l.width < 16 || l.rows < 16) return;
    std::vector<uint32_t> px((size_t)l.width * l.rows);
    for (uint32_t y = 0; y < l.rows; y++)
        for (uint32_t x = 0; x < l.width; x++)
        {
            const uint8_t *p = l.guest + (size_t)y * l.pitch;
            uint32_t v, c;
            switch (l.format)
            {
            case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: c = ((const uint32_t *)p)[x] | 0xff000000u; break;
            case D3DFMT_A4R4G4B4: v = ((const uint16_t *)p)[x];
                c = 0xff000000u | (((v >> 8) & 15) * 17) << 16 | (((v >> 4) & 15) * 17) << 8 | ((v & 15) * 17); break;
            case D3DFMT_A1R5G5B5: case D3DFMT_X1R5G5B5: v = ((const uint16_t *)p)[x];
                c = 0xff000000u | (((v >> 10) & 31) * 255 / 31) << 16 | (((v >> 5) & 31) * 255 / 31) << 8 | ((v & 31) * 255 / 31); break;
            case D3DFMT_R5G6B5: v = ((const uint16_t *)p)[x];
                c = 0xff000000u | (((v >> 11) & 31) * 255 / 31) << 16 | (((v >> 5) & 63) * 255 / 63) << 8 | ((v & 31) * 255 / 31); break;
            default: return;
            }
            px[(size_t)y * l.width + x] = c;
        }
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(px.data(), l.width, l.rows, 32, l.width * 4, SDL_PIXELFORMAT_ARGB8888);
    if (s == NULL) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/tex%05d-%ux%u-f%u.bmp", dir, count++, l.width, l.rows, l.format);
    SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
}

/* D3DLOCKED_RECT: {INT Pitch; void *pBits} = 8 bytes in the guest */
static void unlock_copy(void *self, uint32_t key)
{
    auto &locks = state(self)->locks;
    auto it = locks.find(key);
    if (it == locks.end() || it->second.native == nullptr) return;
    lock_info &l = it->second;
    if (l.format) dump_texture(l);
    if (!(l.flags & D3DLOCK_READONLY))
    {
        if (l.native_pitch == 0 || l.native_pitch == l.pitch) memcpy(l.native, l.guest, (size_t)l.pitch * l.rows);
        else for (uint32_t y = 0; y < l.rows; y++) memcpy((uint8_t *)l.native + (size_t)y * l.native_pitch, l.guest + (size_t)y * l.pitch, l.row_bytes);
    }
    l.native = nullptr;
}

/*
 * A game can keep the pointer of a lock on a system-memory surface after
 * UnlockRect and write to it later (the shroud of Generals does: it locks
 * once and then copies the surface to a texture with CopyRects in each
 * frame). Direct3D allows it, because that memory stays in place. Here the
 * game has a guest copy, so before the surface is read on the GPU side the
 * copy is written to the native surface again.
 */
static void sync_kept(void *self, uint32_t key, D3DPOOL pool, HRESULT (*lock)(void *, uint32_t, D3DLOCKED_RECT *), void (*unlock)(void *, uint32_t))
{
    if (self == NULL || pool != D3DPOOL_SYSTEMMEM) return;
    auto &locks = state(self)->locks;
    auto it = locks.find(key);
    if (it == locks.end()) return;
    lock_info &l = it->second;
    if (l.guest == nullptr || l.native != nullptr || !l.whole || (l.flags & D3DLOCK_READONLY)) return;
    D3DLOCKED_RECT lr;
    if (FAILED(lock(self, key, &lr))) return;
    if (lr.Pitch == l.pitch) memcpy(lr.pBits, l.guest, (size_t)l.pitch * l.rows);
    else if (l.row_bytes) for (uint32_t y = 0; y < l.rows; y++) memcpy((uint8_t *)lr.pBits + (size_t)y * lr.Pitch, l.guest + (size_t)y * l.pitch, l.row_bytes);
    unlock(self, key);
}

static HRESULT lock_surface(void *self, uint32_t, D3DLOCKED_RECT *lr) { return d3d8_native<IDirect3DSurface8>(self)->LockRect(lr, NULL, D3DLOCK_NOSYSLOCK); }
static void unlock_surface(void *self, uint32_t) { d3d8_native<IDirect3DSurface8>(self)->UnlockRect(); }
static HRESULT lock_level(void *self, uint32_t level, D3DLOCKED_RECT *lr) { return d3d8_native<IDirect3DTexture8>(self)->LockRect(level, lr, NULL, D3DLOCK_NOSYSLOCK); }
static void unlock_level(void *self, uint32_t level) { d3d8_native<IDirect3DTexture8>(self)->UnlockRect(level); }

static void sync_kept_surface(void *self)
{
    if (self == NULL) return;
    D3DSURFACE_DESC d;
    if (FAILED(d3d8_native<IDirect3DSurface8>(self)->GetDesc(&d))) return;
    sync_kept(self, 0, d.Pool, lock_surface, unlock_surface);
}

static void sync_kept_texture(void *self)
{
    if (self == NULL) return;
    IDirect3DBaseTexture8 *b = d3d8_native<IDirect3DBaseTexture8>(self);
    if (b->GetType() != D3DRTYPE_TEXTURE) return;
    IDirect3DTexture8 *t = (IDirect3DTexture8 *)b;
    for (uint32_t level = 0; level < t->GetLevelCount(); level++)
    {
        D3DSURFACE_DESC d;
        if (FAILED(t->GetLevelDesc(level, &d))) return;
        sync_kept(self, level, d.Pool, lock_level, unlock_level);
    }
}

EXTERN_C uint32_t IDirect3DDevice8_CopyRects_c(void *self, void *src_surface, void *src_rects, uint32_t rect_count, void *dst_surface, void *dst_points)
{
    sync_kept_surface(src_surface);
    HRESULT r = d3d8_native<IDirect3DDevice8>(self)->CopyRects(d3d8_unwrap<IDirect3DSurface8>(src_surface), (const RECT *)src_rects, (UINT)rect_count,
                                                                d3d8_unwrap<IDirect3DSurface8>(dst_surface), (const POINT *)dst_points);
    D3D8_TRACE("IDirect3DDevice8::CopyRects", r);
    return (uint32_t)r;
}

EXTERN_C uint32_t IDirect3DDevice8_UpdateTexture_c(void *self, void *pSourceTexture, void *pDestinationTexture)
{
    sync_kept_texture(pSourceTexture);
    HRESULT r = d3d8_native<IDirect3DDevice8>(self)->UpdateTexture(d3d8_unwrap<IDirect3DBaseTexture8>(pSourceTexture), d3d8_unwrap<IDirect3DBaseTexture8>(pDestinationTexture));
    D3D8_TRACE("IDirect3DDevice8::UpdateTexture", r);
    return (uint32_t)r;
}

/* rows of a block-compressed format are 4 pixels high */
static uint32_t lock_height(D3DFORMAT f, uint32_t h)
{
    if (f == D3DFMT_DXT1 || f == D3DFMT_DXT2 || f == D3DFMT_DXT3 || f == D3DFMT_DXT4 || f == D3DFMT_DXT5) return (h + 3) / 4;
    return h;
}

static RECT block_rect(D3DFORMAT f, const RECT *r)
{
    RECT b = *r;
    if (lock_height(f, 4) == 1)
    {
        b.top /= 4;
        b.bottom = (b.bottom + 3) / 4;
    }
    return b;
}

/* bytes per pixel, or per 4x4 block for DXT (negative); 0: not known */
static int format_bytes(D3DFORMAT f)
{
    switch ((uint32_t)f)
    {
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_Q8W8V8U8: case D3DFMT_X8L8V8U8:
    case D3DFMT_A2B10G10R10: case D3DFMT_G16R16: case D3DFMT_W11V11U10: case D3DFMT_A2W10V10U10: case D3DFMT_V16U16:
        return 4;
    case D3DFMT_R8G8B8: return 3;
    case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4:
    case D3DFMT_A8R3G3B2: case D3DFMT_A8L8: case D3DFMT_V8U8: case D3DFMT_L6V5U5: case D3DFMT_A8P8:
        return 2;
    case D3DFMT_A8: case D3DFMT_L8: case D3DFMT_P8: case D3DFMT_A4L4: case D3DFMT_R3G3B2: return 1;
    case D3DFMT_DXT1: return -8;
    case D3DFMT_DXT2: case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5: return -16;
    default: return 0;
    }
}

/*
 * The guest side of a surface lock. The guest gets a tight pitch (the width
 * times the pixel size), as Windows drivers give for these surfaces: some
 * game code ignores the pitch and uses width x pixel size (the tree texture
 * of Generals). DXVK can give a larger pitch; then the rows are copied one
 * by one at lock and unlock.
 */
static void lock_rect_guest(lock_info &l, const D3DSURFACE_DESC &d, const RECT *rect, const D3DLOCKED_RECT &lr,
                            uint32_t Flags, void *pLockedRect)
{
    RECT br;
    if (rect) br = block_rect(d.Format, rect);
    uint32_t rows = rect ? (uint32_t)(br.bottom - br.top) : lock_height(d.Format, d.Height);
    uint32_t width = rect ? (uint32_t)(rect->right - rect->left) : d.Width;
    int fb = format_bytes(d.Format);
    uint32_t row_bytes = fb > 0 ? width * (uint32_t)fb : fb < 0 ? ((width + 3) / 4) * (uint32_t)(-fb) : 0;
    if (row_bytes == 0 || row_bytes > (uint32_t)lr.Pitch) row_bytes = (uint32_t)lr.Pitch;
    /* a rectangle lock keeps the native pitch: the game adds rows of the
       whole surface to the pointer of the first row */
    int32_t gpitch = rect ? lr.Pitch : (int32_t)row_bytes;
    uint8_t *g = guest_buffer(l, (uint32_t)gpitch * rows);
    l.native = lr.pBits;
    l.pitch = gpitch;
    l.native_pitch = lr.Pitch;
    l.row_bytes = row_bytes;
    l.rows = rows;
    l.flags = Flags;
    l.whole = (rect == NULL);
    l.format = (uint32_t)d.Format;
    l.width = width;
    if (!(Flags & D3DLOCK_DISCARD))
    {
        if (gpitch == lr.Pitch) memcpy(g, lr.pBits, (size_t)lr.Pitch * rows);
        else for (uint32_t y = 0; y < rows; y++) memcpy(g + (size_t)y * gpitch, (uint8_t *)lr.pBits + (size_t)y * lr.Pitch, row_bytes);
    }
    wr32(pLockedRect, (uint32_t)gpitch);
    wr32((uint8_t *)pLockedRect + 4, to_guest(g));
}

EXTERN_C uint32_t IDirect3DSurface8_LockRect_c(void *self, void *pLockedRect, void *pRect, uint32_t Flags)
{
    IDirect3DSurface8 *s = d3d8_native<IDirect3DSurface8>(self);
    D3DSURFACE_DESC d;
    s->GetDesc(&d);
    const RECT *rect = (const RECT *)pRect;
    D3DLOCKED_RECT lr;
    HRESULT r = s->LockRect(&lr, rect, Flags);
    if (d3d8_trace_level() >= 3) fprintf(stderr, "d3d8:   Surface LockRect(%p, %s, 0x%x) from %s\n", self, rect ? "rect" : "all", Flags, guest_caller());
    if (FAILED(r)) return (uint32_t)r;
    lock_rect_guest(state(self)->locks[0], d, rect, lr, Flags, pLockedRect);
    return (uint32_t)r;
}

EXTERN_C uint32_t IDirect3DSurface8_UnlockRect_c(void *self)
{
    unlock_copy(self, 0);
    return (uint32_t)d3d8_native<IDirect3DSurface8>(self)->UnlockRect();
}

EXTERN_C uint32_t IDirect3DTexture8_LockRect_c(void *self, uint32_t Level, void *pLockedRect, void *pRect, uint32_t Flags)
{
    IDirect3DTexture8 *t = d3d8_native<IDirect3DTexture8>(self);
    D3DSURFACE_DESC d;
    HRESULT r = t->GetLevelDesc(Level, &d);
    if (FAILED(r)) return (uint32_t)r;
    const RECT *rect = (const RECT *)pRect;
    D3DLOCKED_RECT lr;
    r = t->LockRect(Level, &lr, rect, Flags);
    if (FAILED(r)) return (uint32_t)r;
    lock_rect_guest(state(self)->locks[Level], d, rect, lr, Flags, pLockedRect);
    return (uint32_t)r;
}

EXTERN_C uint32_t IDirect3DTexture8_UnlockRect_c(void *self, uint32_t Level)
{
    unlock_copy(self, Level);
    return (uint32_t)d3d8_native<IDirect3DTexture8>(self)->UnlockRect(Level);
}

EXTERN_C uint32_t IDirect3DCubeTexture8_LockRect_c(void *self, uint32_t FaceType, uint32_t Level, void *pLockedRect, void *pRect, uint32_t Flags)
{
    IDirect3DCubeTexture8 *t = d3d8_native<IDirect3DCubeTexture8>(self);
    D3DSURFACE_DESC d;
    HRESULT r = t->GetLevelDesc(Level, &d);
    if (FAILED(r)) return (uint32_t)r;
    const RECT *rect = (const RECT *)pRect;
    D3DLOCKED_RECT lr;
    r = t->LockRect((D3DCUBEMAP_FACES)FaceType, Level, &lr, rect, Flags);
    if (FAILED(r)) return (uint32_t)r;
    lock_rect_guest(state(self)->locks[Level | (FaceType << 16)], d, rect, lr, Flags, pLockedRect);
    return (uint32_t)r;
}

EXTERN_C uint32_t IDirect3DCubeTexture8_UnlockRect_c(void *self, uint32_t FaceType, uint32_t Level)
{
    unlock_copy(self, Level | (FaceType << 16));
    return (uint32_t)d3d8_native<IDirect3DCubeTexture8>(self)->UnlockRect((D3DCUBEMAP_FACES)FaceType, Level);
}

/* D3DLOCKED_BOX: {INT RowPitch; INT SlicePitch; void *pBits} = 12 bytes */
static uint32_t lock_box(void *self, uint32_t key, void *pLockedVolume, D3DLOCKED_BOX &lb, uint32_t depth, uint32_t flags)
{
    lock_info &l = state(self)->locks[key];
    uint32_t size = (uint32_t)lb.SlicePitch * depth;
    uint8_t *g = guest_buffer(l, size);
    l.native = lb.pBits;
    l.pitch = lb.SlicePitch;
    l.native_pitch = 0;
    l.rows = depth;
    l.flags = flags;
    if (!(flags & D3DLOCK_DISCARD)) memcpy(g, lb.pBits, size);
    wr32(pLockedVolume, (uint32_t)lb.RowPitch);
    wr32((uint8_t *)pLockedVolume + 4, (uint32_t)lb.SlicePitch);
    wr32((uint8_t *)pLockedVolume + 8, to_guest(g));
    return 0;
}

EXTERN_C uint32_t IDirect3DVolume8_LockBox_c(void *self, void *pLockedVolume, void *pBox, uint32_t Flags)
{
    IDirect3DVolume8 *v = d3d8_native<IDirect3DVolume8>(self);
    D3DVOLUME_DESC d;
    v->GetDesc(&d);
    D3DLOCKED_BOX lb;
    HRESULT r = v->LockBox(&lb, (const D3DBOX *)pBox, Flags);
    if (FAILED(r)) return (uint32_t)r;
    const D3DBOX *b = (const D3DBOX *)pBox;
    lock_box(self, 0, pLockedVolume, lb, b ? b->Back - b->Front : d.Depth, Flags);
    return (uint32_t)r;
}

EXTERN_C uint32_t IDirect3DVolume8_UnlockBox_c(void *self)
{
    unlock_copy(self, 0);
    return (uint32_t)d3d8_native<IDirect3DVolume8>(self)->UnlockBox();
}

EXTERN_C uint32_t IDirect3DVolumeTexture8_LockBox_c(void *self, uint32_t Level, void *pLockedVolume, void *pBox, uint32_t Flags)
{
    IDirect3DVolumeTexture8 *t = d3d8_native<IDirect3DVolumeTexture8>(self);
    D3DVOLUME_DESC d;
    t->GetLevelDesc(Level, &d);
    D3DLOCKED_BOX lb;
    HRESULT r = t->LockBox(Level, &lb, (const D3DBOX *)pBox, Flags);
    if (FAILED(r)) return (uint32_t)r;
    const D3DBOX *b = (const D3DBOX *)pBox;
    lock_box(self, Level, pLockedVolume, lb, b ? b->Back - b->Front : d.Depth, Flags);
    return (uint32_t)r;
}

EXTERN_C uint32_t IDirect3DVolumeTexture8_UnlockBox_c(void *self, uint32_t Level)
{
    unlock_copy(self, Level);
    return (uint32_t)d3d8_native<IDirect3DVolumeTexture8>(self)->UnlockBox(Level);
}

/* Vertex and index buffers: the lock range; size 0 is the whole buffer */
template <class T, class DESC>
static uint32_t lock_buffer(void *self, uint32_t offset, uint32_t size, void *ppbData, uint32_t flags)
{
    T *b = d3d8_native<T>(self);
    DESC d;
    b->GetDesc(&d);
    if (size == 0) size = d.Size - offset;
    BYTE *p = nullptr;
    HRESULT r = b->Lock(offset, size, &p, flags);
    if (FAILED(r)) return (uint32_t)r;
    lock_info &l = state(self)->locks[0];
    uint8_t *g = guest_buffer(l, size);
    l.native = p;
    l.pitch = (int32_t)size;
    l.native_pitch = 0;
    l.rows = 1;
    l.flags = flags;
    /* The whole range is copied back at Unlock, so it must start with the
       current data, also for write-only buffers and no-overwrite locks:
       the game can change only some vertices in the range (the terrain
       does). Only a discard lock gives undefined data. */
    if (!(flags & D3DLOCK_DISCARD)) memcpy(g, p, size);
    wr32(ppbData, to_guest(g));
    return (uint32_t)r;
}

EXTERN_C uint32_t IDirect3DVertexBuffer8_Lock_c(void *self, uint32_t OffsetToLock, uint32_t SizeToLock, void *ppbData, uint32_t Flags)
{
    return lock_buffer<IDirect3DVertexBuffer8, D3DVERTEXBUFFER_DESC>(self, OffsetToLock, SizeToLock, ppbData, Flags);
}

EXTERN_C uint32_t IDirect3DVertexBuffer8_Unlock_c(void *self)
{
    unlock_copy(self, 0);
    return (uint32_t)d3d8_native<IDirect3DVertexBuffer8>(self)->Unlock();
}

EXTERN_C uint32_t IDirect3DIndexBuffer8_Lock_c(void *self, uint32_t OffsetToLock, uint32_t SizeToLock, void *ppbData, uint32_t Flags)
{
    return lock_buffer<IDirect3DIndexBuffer8, D3DINDEXBUFFER_DESC>(self, OffsetToLock, SizeToLock, ppbData, Flags);
}

EXTERN_C uint32_t IDirect3DIndexBuffer8_Unlock_c(void *self)
{
    unlock_copy(self, 0);
    return (uint32_t)d3d8_native<IDirect3DIndexBuffer8>(self)->Unlock();
}

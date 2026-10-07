/*
 *  Port change: Direct3D 8 for the translated x86 code, on DXVK (d3d8 to
 *  Vulkan) and MoltenVK (Vulkan to Metal).
 *
 *  The x86 code gets a guest wrapper object for each native DXVK object:
 *      +0  guest address of the vtable (glue procedures, com/d3d8.com)
 *      +4  "D3D8"
 *      +8  the native object (64-bit pointer)
 *      +16 host state of the wrapper (d3d8_state *: lock buffers)
 *  The glue calls <Iface>_<Method>_c with a host pointer to the wrapper.
 *
 *  Built only for games with DXVK_INCLUDE in game.conf (the DXVK source
 *  folder: its include/native headers give the native interfaces).
 *
 *  MIT license, see README.md.
 */

#ifndef NATIVE_WINAPI_D3D8_H
#define NATIVE_WINAPI_D3D8_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <d3d8.h>
#include "guest.h"

#ifndef EXTERN_C
#define EXTERN_C extern "C"
#endif

#define D3D8_MAGIC 0x38443344u   /* "D3D8" */

struct d3d8_state;

/* the native object of a wrapper */
template <class T> static inline T *d3d8_native(const void *self)
{
    uint64_t p;
    memcpy(&p, (const uint8_t *)self + 8, 8);
    return (T *)(uintptr_t)p;
}

template <class T> static inline T *d3d8_unwrap(const void *self)
{
    return (self == NULL) ? nullptr : d3d8_native<T>(self);
}

static inline d3d8_state *d3d8_state_of(const void *self)
{
    uint64_t p;
    memcpy(&p, (const uint8_t *)self + 16, 8);
    return (d3d8_state *)(uintptr_t)p;
}

/* guest wrapper (guest address) of a native object; 0 for NULL */
uint32_t d3d8_wrap(IDirect3D8 *o);
uint32_t d3d8_wrap(IDirect3DDevice8 *o);
uint32_t d3d8_wrap(IDirect3DSwapChain8 *o);
uint32_t d3d8_wrap(IDirect3DSurface8 *o);
uint32_t d3d8_wrap(IDirect3DVolume8 *o);
uint32_t d3d8_wrap(IDirect3DVertexBuffer8 *o);
uint32_t d3d8_wrap(IDirect3DIndexBuffer8 *o);
uint32_t d3d8_wrap(IDirect3DTexture8 *o);
uint32_t d3d8_wrap(IDirect3DCubeTexture8 *o);
uint32_t d3d8_wrap(IDirect3DVolumeTexture8 *o);
uint32_t d3d8_wrap(IDirect3DBaseTexture8 *o);
uint32_t d3d8_wrap(IDirect3DResource8 *o);

static inline float d3d8_float(uint32_t bits)
{
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

/* the native window (SDL_Window *) for a guest HWND */
HWND d3d8_hwnd(void *guest_hwnd);

int d3d8_trace_level(void);

/* A display without a known format (the SDL offscreen driver) reports
   X8R8G8B8, as a real display does; the game picks its paths from it. */
void d3d8_fix_display_mode(void *mode);

#define D3D8_TRACE(name, result) \
    do { if (d3d8_trace_level() >= 2 || (d3d8_trace_level() >= 1 && (int32_t)(result) < 0 && (uint32_t)(result) > 0x80000000u)) \
        fprintf(stderr, "d3d8: %s -> 0x%x\n", name, (unsigned)(result)); } while (0)

#endif

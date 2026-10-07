/*
 *  Port change: DirectInput 8 (keyboard and mouse). The objects and the
 *  first methods are those of DirectInput 5 (WinApi-dinput.c); only the
 *  vtables differ. The DirectInput 8 methods that DirectInput 5 does not
 *  have return DIERR_UNSUPPORTED (the weak stubs of com/dinput8.com).
 *
 *  MIT license, see README.md.
 */

#include "game-info.h"
#include <stdint.h>
#include <stdio.h>
#include "Game-Memory.h"
#include "guest.h"

#ifndef EXTERN_C
#define EXTERN_C extern "C"
#endif

EXTERN_C uint8_t IDirectInput8AVtbl_asm2c[];
EXTERN_C uint8_t IDirectInputDevice8AVtbl_asm2c[];

/* WinApi-dinput.c (the first argument is the object) */
#define DI5(name, ...) EXTERN_C uint32_t IDirectInputA_##name##_c(void *o, ##__VA_ARGS__)
#define DEV5(name, ...) EXTERN_C uint32_t IDirectInputDevice2A_##name##_c(void *d, ##__VA_ARGS__)
DI5(QueryInterface, void *riid, uint32_t *ppvObj);
DI5(AddRef);
DI5(Release);
DI5(CreateDevice, void *rguid, uint32_t *lplpDirectInputDevice, void *pUnkOuter);
DI5(EnumDevices, uint32_t dwDevType, uint32_t lpCallback, uint32_t pvRef, uint32_t dwFlags);
DI5(GetDeviceStatus, void *rguidInstance);
DEV5(QueryInterface, void *riid, uint32_t *ppvObj);
DEV5(AddRef);
DEV5(Release);
DEV5(GetCapabilities, uint8_t *caps);
DEV5(EnumObjects, uint32_t lpCallback, void *pvRef, uint32_t dwFlags);
DEV5(GetProperty, uint32_t rguidProp, uint8_t *pdiph);
DEV5(SetProperty, uint32_t rguidProp, uint8_t *pdiph);
DEV5(Acquire);
DEV5(Unacquire);
DEV5(GetDeviceState, uint32_t cbData, uint8_t *data);
DEV5(GetDeviceData, uint32_t cbObjectData, uint8_t *rgdod, uint32_t *pdwInOut, uint32_t dwFlags);
DEV5(SetDataFormat, uint8_t *lpdf);
DEV5(SetEventNotification, void *hEvent);
DEV5(SetCooperativeLevel, void *hwnd, uint32_t dwFlags);
DEV5(GetDeviceInfo, uint8_t *pdidi);
DEV5(Poll);

#define DI_OK 0
#define DIERR_INVALIDPARAM 0x80070057u

EXTERN_C uint32_t DirectInput8Create_c(void *hinst, uint32_t dwVersion, void *riidltf, uint32_t *ppvOut, void *punkOuter)
{
    if (ppvOut == NULL) return DIERR_INVALIDPARAM;
    uint32_t *o = (uint32_t *)x86_calloc(1, 8);   /* {lpVtbl, RefCount}: as WinApi-dinput.c */
    o[0] = to_guest(IDirectInput8AVtbl_asm2c);
    o[1] = 1;
    wr32(ppvOut, to_guest(o));
    if (game_getenv("TRACE_DINPUT")) fprintf(stderr, "DInput: DirectInput8Create version 0x%x\n", dwVersion);
    return DI_OK;
}

EXTERN_C uint32_t IDirectInput8A_QueryInterface_c(void *o, void *riid, uint32_t *p) { return IDirectInputA_QueryInterface_c(o, riid, p); }
EXTERN_C uint32_t IDirectInput8A_AddRef_c(void *o) { return IDirectInputA_AddRef_c(o); }
EXTERN_C uint32_t IDirectInput8A_Release_c(void *o) { return IDirectInputA_Release_c(o); }
EXTERN_C uint32_t IDirectInput8A_EnumDevices_c(void *o, uint32_t t, uint32_t cb, uint32_t ref, uint32_t f) { return IDirectInputA_EnumDevices_c(o, t, cb, ref, f); }
EXTERN_C uint32_t IDirectInput8A_GetDeviceStatus_c(void *o, void *g) { return IDirectInputA_GetDeviceStatus_c(o, g); }

EXTERN_C uint32_t IDirectInput8A_CreateDevice_c(void *o, void *rguid, uint32_t *lplpDevice, void *pUnkOuter)
{
    uint32_t r = IDirectInputA_CreateDevice_c(o, rguid, lplpDevice, pUnkOuter);
    if (r == DI_OK && lplpDevice != NULL && rd32(lplpDevice) != 0)
    {
        wr32(from_guest(rd32(lplpDevice)), to_guest(IDirectInputDevice8AVtbl_asm2c));
    }
    return r;
}

EXTERN_C uint32_t IDirectInputDevice8A_QueryInterface_c(void *d, void *riid, uint32_t *p) { return IDirectInputDevice2A_QueryInterface_c(d, riid, p); }
EXTERN_C uint32_t IDirectInputDevice8A_AddRef_c(void *d) { return IDirectInputDevice2A_AddRef_c(d); }
EXTERN_C uint32_t IDirectInputDevice8A_Release_c(void *d) { return IDirectInputDevice2A_Release_c(d); }
EXTERN_C uint32_t IDirectInputDevice8A_GetCapabilities_c(void *d, uint8_t *c) { return IDirectInputDevice2A_GetCapabilities_c(d, c); }
EXTERN_C uint32_t IDirectInputDevice8A_EnumObjects_c(void *d, uint32_t cb, void *ref, uint32_t f) { return IDirectInputDevice2A_EnumObjects_c(d, cb, ref, f); }
EXTERN_C uint32_t IDirectInputDevice8A_GetProperty_c(void *d, uint32_t g, uint8_t *p) { return IDirectInputDevice2A_GetProperty_c(d, g, p); }
EXTERN_C uint32_t IDirectInputDevice8A_SetProperty_c(void *d, uint32_t g, uint8_t *p) { return IDirectInputDevice2A_SetProperty_c(d, g, p); }
EXTERN_C uint32_t IDirectInputDevice8A_Acquire_c(void *d) { return IDirectInputDevice2A_Acquire_c(d); }
EXTERN_C uint32_t IDirectInputDevice8A_Unacquire_c(void *d) { return IDirectInputDevice2A_Unacquire_c(d); }
EXTERN_C uint32_t IDirectInputDevice8A_GetDeviceState_c(void *d, uint32_t n, uint8_t *p) { return IDirectInputDevice2A_GetDeviceState_c(d, n, p); }
EXTERN_C uint32_t IDirectInputDevice8A_GetDeviceData_c(void *d, uint32_t n, uint8_t *r, uint32_t *io, uint32_t f) { return IDirectInputDevice2A_GetDeviceData_c(d, n, r, io, f); }
EXTERN_C uint32_t IDirectInputDevice8A_SetDataFormat_c(void *d, uint8_t *f) { return IDirectInputDevice2A_SetDataFormat_c(d, f); }
EXTERN_C uint32_t IDirectInputDevice8A_SetEventNotification_c(void *d, void *e) { return IDirectInputDevice2A_SetEventNotification_c(d, e); }
EXTERN_C uint32_t IDirectInputDevice8A_SetCooperativeLevel_c(void *d, void *h, uint32_t f) { return IDirectInputDevice2A_SetCooperativeLevel_c(d, h, f); }
EXTERN_C uint32_t IDirectInputDevice8A_GetDeviceInfo_c(void *d, uint8_t *p) { return IDirectInputDevice2A_GetDeviceInfo_c(d, p); }
EXTERN_C uint32_t IDirectInputDevice8A_Poll_c(void *d) { return IDirectInputDevice2A_Poll_c(d); }

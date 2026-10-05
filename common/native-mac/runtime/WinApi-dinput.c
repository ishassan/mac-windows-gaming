/*
 *  Native port: DirectInput 5 (IDirectInputA, IDirectInputDevice2A) for the
 *  system keyboard and mouse, on SDL input events. Joysticks are not
 *  available (no devices are enumerated).
 *
 *  An SDL event watch sees every input event (also the events of the
 *  scripted-input thread, input-script.c) and updates the device state and
 *  the device buffers. Mouse axes are relative: the motion is in game
 *  pixels, so the game cursor follows the system cursor in a window.
 *  The vtables come from com/dinput.com (common/native-mac/tools/gen_com.py).
 *  Trace with <GAME>_TRACE_DINPUT=1.
 *  MIT license, see the README.md of the repository.
 */

#include "game-info.h"
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Game-Memory.h"
#include "guest.h"
#include "platform.h"

#define DI_OK               0x00000000u
#define DI_BUFFEROVERFLOW   0x00000001u
#define DIERR_INVALIDPARAM  0x80070057u
#define DIERR_NOINTERFACE   0x80004002u
#define DIERR_DEVICENOTREG  0x80040154u
#define DIERR_NOTACQUIRED   0x8007000Cu
#define DIERR_OUTOFMEMORY   0x8007000Eu
#define DIERR_UNSUPPORTED   0x80004001u

#define DIDEVTYPE_MOUSE     2
#define DIDEVTYPE_KEYBOARD  3
#define DIGDD_PEEK          1
#define DIPROP_BUFFERSIZE   1
#define DIPROP_AXISMODE     2

#define DIDFT_AXIS          0x00000003u
#define DIDFT_BUTTON        0x0000000Cu

#define KIND_KEYBOARD 1
#define KIND_MOUSE    2
#define MAX_BUFFER    1024

EXTERN_C uint8_t IDirectInputAVtbl_asm2c[];
EXTERN_C uint8_t IDirectInputDevice2AVtbl_asm2c[];
EXTERN_C uint32_t CCALL CallX86Function(uint32_t address, int nargs, const uint32_t *args);
extern "C" uint32_t SetEvent_c(void *hEvent);

typedef struct {
    uint32_t dwOfs, dwData, dwTimeStamp, dwSequence;
} di_object_data;

typedef struct {
    uint32_t lpVtbl;      /* must be first (guest COM object) */
    uint32_t RefCount;
} di_object;

typedef struct {
    uint32_t lpVtbl;      /* must be first (guest COM object) */
    uint32_t RefCount;
    int kind;
    int acquired;
    int relative;
    void *event;          /* SetEventNotification handle (host pointer) */
    uint32_t data_size;   /* from SetDataFormat */
    uint32_t axis_ofs[3]; /* mouse: offsets of x, y, z in the data format */
    uint32_t button_ofs[8];
    int axis_count, button_count;
    int32_t axis[3];      /* mouse: motion since the last GetDeviceState (or position) */
    uint8_t keys[256];    /* keyboard: DIK code -> 0x80 if down */
    uint8_t buttons[8];
    di_object_data *ring; /* buffered data (host memory) */
    uint32_t ring_size, ring_head, ring_count;
    int overflow;
} di_device;

static SDL_mutex *lock;
static di_device *devices[8];
static int device_count;
static uint32_t sequence;
static int watch_added;

static int trace(void)
{
    static int value = -1;
    if (value < 0)
    {
        const char *v = game_getenv("TRACE_DINPUT");
        value = v ? atoi(v) : 0;
    }
    return value;
}

static int same_guid(const void *g, uint32_t d1)
{
    static const uint8_t tail[8] = { 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };
    return g != NULL && rd32(g) == d1 && memcmp((const uint8_t *)g + 8, tail, 8) == 0;
}

/* SDL scancode (USB usage) -> DirectInput key code (PC set 1 scan code) */
static int dik_of(SDL_Scancode s)
{
    static const uint8_t letters[26] = {
        0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
        0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c };
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return letters[s - SDL_SCANCODE_A];
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return 0x02 + (s - SDL_SCANCODE_1);
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10) return 0x3b + (s - SDL_SCANCODE_F1);
    switch (s)
    {
    case SDL_SCANCODE_0: return 0x0b;
    case SDL_SCANCODE_RETURN: return 0x1c;
    case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_BACKSPACE: return 0x0e;
    case SDL_SCANCODE_TAB: return 0x0f;
    case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_MINUS: return 0x0c;
    case SDL_SCANCODE_EQUALS: return 0x0d;
    case SDL_SCANCODE_LEFTBRACKET: return 0x1a;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1b;
    case SDL_SCANCODE_BACKSLASH: return 0x2b;
    case SDL_SCANCODE_SEMICOLON: return 0x27;
    case SDL_SCANCODE_APOSTROPHE: return 0x28;
    case SDL_SCANCODE_GRAVE: return 0x29;
    case SDL_SCANCODE_COMMA: return 0x33;
    case SDL_SCANCODE_PERIOD: return 0x34;
    case SDL_SCANCODE_SLASH: return 0x35;
    case SDL_SCANCODE_CAPSLOCK: return 0x3a;
    case SDL_SCANCODE_F11: return 0x57;
    case SDL_SCANCODE_F12: return 0x58;
    case SDL_SCANCODE_PRINTSCREEN: return 0xb7;
    case SDL_SCANCODE_SCROLLLOCK: return 0x46;
    case SDL_SCANCODE_PAUSE: return 0xc5;
    case SDL_SCANCODE_INSERT: return 0xd2;
    case SDL_SCANCODE_HOME: return 0xc7;
    case SDL_SCANCODE_PAGEUP: return 0xc9;
    case SDL_SCANCODE_DELETE: return 0xd3;
    case SDL_SCANCODE_END: return 0xcf;
    case SDL_SCANCODE_PAGEDOWN: return 0xd1;
    case SDL_SCANCODE_RIGHT: return 0xcd;
    case SDL_SCANCODE_LEFT: return 0xcb;
    case SDL_SCANCODE_DOWN: return 0xd0;
    case SDL_SCANCODE_UP: return 0xc8;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45;
    case SDL_SCANCODE_KP_DIVIDE: return 0xb5;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x37;
    case SDL_SCANCODE_KP_MINUS: return 0x4a;
    case SDL_SCANCODE_KP_PLUS: return 0x4e;
    case SDL_SCANCODE_KP_ENTER: return 0x9c;
    case SDL_SCANCODE_KP_1: return 0x4f;
    case SDL_SCANCODE_KP_2: return 0x50;
    case SDL_SCANCODE_KP_3: return 0x51;
    case SDL_SCANCODE_KP_4: return 0x4b;
    case SDL_SCANCODE_KP_5: return 0x4c;
    case SDL_SCANCODE_KP_6: return 0x4d;
    case SDL_SCANCODE_KP_7: return 0x47;
    case SDL_SCANCODE_KP_8: return 0x48;
    case SDL_SCANCODE_KP_9: return 0x49;
    case SDL_SCANCODE_KP_0: return 0x52;
    case SDL_SCANCODE_KP_PERIOD: return 0x53;
    case SDL_SCANCODE_LCTRL: return 0x1d;
    case SDL_SCANCODE_LSHIFT: return 0x2a;
    case SDL_SCANCODE_LALT: return 0x38;
    case SDL_SCANCODE_LGUI: return 0xdb;
    case SDL_SCANCODE_RCTRL: return 0x9d;
    case SDL_SCANCODE_RSHIFT: return 0x36;
    case SDL_SCANCODE_RALT: return 0xb8;
    case SDL_SCANCODE_RGUI: return 0xdc;
    default: return 0;
    }
}

/* Call with the lock held */
static void add_data(di_device *d, uint32_t ofs, uint32_t data)
{
    if (d->ring == NULL || !d->acquired) return;
    if (d->ring_count == d->ring_size)
    {
        d->overflow = 1;
        d->ring_head = (d->ring_head + 1) % d->ring_size;
        d->ring_count--;
    }
    di_object_data *e = &d->ring[(d->ring_head + d->ring_count) % d->ring_size];
    e->dwOfs = ofs;
    e->dwData = data;
    e->dwTimeStamp = SDL_GetTicks();
    e->dwSequence = ++sequence;
    d->ring_count++;
}

static int SDLCALL event_watch(void *userdata, SDL_Event *ev)
{
    int i, changed = 0;
    SDL_LockMutex(lock);
    for (i = 0; i < device_count; i++)
    {
        di_device *d = devices[i];
        if (d == NULL) continue;
        if (d->kind == KIND_KEYBOARD && (ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP))
        {
            int dik = dik_of(ev->key.keysym.scancode);
            uint8_t v = (ev->type == SDL_KEYDOWN) ? 0x80 : 0;
            if (dik == 0 || ev->key.repeat) continue;
            d->keys[dik] = v;
            add_data(d, dik, v);
            changed = 1;
        }
        else if (d->kind == KIND_MOUSE && ev->type == SDL_MOUSEMOTION)
        {
            int dx = ev->motion.xrel, dy = ev->motion.yrel;
            if (dx == 0 && dy == 0) continue;
            d->axis[0] += dx;
            d->axis[1] += dy;
            if (dx && d->axis_count > 0) add_data(d, d->axis_ofs[0], (uint32_t)dx);
            if (dy && d->axis_count > 1) add_data(d, d->axis_ofs[1], (uint32_t)dy);
            changed = 1;
        }
        else if (d->kind == KIND_MOUSE && ev->type == SDL_MOUSEWHEEL)
        {
            int dz = ev->wheel.y * 120;   /* WHEEL_DELTA per step */
            d->axis[2] += dz;
            if (d->axis_count > 2) add_data(d, d->axis_ofs[2], (uint32_t)dz);
            changed = 1;
        }
        else if (d->kind == KIND_MOUSE && (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP))
        {
            int b;
            uint8_t v = (ev->type == SDL_MOUSEBUTTONDOWN) ? 0x80 : 0;
            switch (ev->button.button)
            {
            case SDL_BUTTON_LEFT: b = 0; break;
            case SDL_BUTTON_RIGHT: b = 1; break;
            case SDL_BUTTON_MIDDLE: b = 2; break;
            case SDL_BUTTON_X1: b = 3; break;
            default: b = -1; break;
            }
            if (b < 0) continue;
            d->buttons[b] = v;
            if (b < d->button_count) add_data(d, d->button_ofs[b], v);
            changed = 1;
        }
        if (changed && d->event != NULL && d->acquired) SetEvent_c(d->event);
        changed = 0;
    }
    SDL_UnlockMutex(lock);
    return 1;
}

static void init(void)
{
    if (lock == NULL) lock = SDL_CreateMutex();
    if (!watch_added)
    {
        watch_added = 1;
        /* added after the renderer exists, so motion is in game pixels */
        SDL_AddEventWatch(event_watch, NULL);
    }
}

/* ------------------------------------------------------------ IDirectInputA */

EXTERN_C uint32_t DirectInputCreateA_c(void *hinst, uint32_t dwVersion, uint32_t *lplpDirectInput, void *punkOuter)
{
    if (lplpDirectInput == NULL) return DIERR_INVALIDPARAM;
    di_object *o = (di_object *)x86_calloc(1, sizeof(di_object));
    if (o == NULL) return DIERR_OUTOFMEMORY;
    o->lpVtbl = to_guest(IDirectInputAVtbl_asm2c);
    o->RefCount = 1;
    wr32(lplpDirectInput, to_guest(o));
    init();
    if (trace()) fprintf(stderr, "DInput: DirectInputCreateA version 0x%x\n", dwVersion);
    return DI_OK;
}

EXTERN_C uint32_t IDirectInputA_QueryInterface_c(di_object *o, void *riid, uint32_t *ppvObj)
{
    if (ppvObj == NULL) return DIERR_INVALIDPARAM;
    o->RefCount++;
    wr32(ppvObj, to_guest(o));
    return DI_OK;
}

EXTERN_C uint32_t IDirectInputA_AddRef_c(di_object *o) { return ++o->RefCount; }

EXTERN_C uint32_t IDirectInputA_Release_c(di_object *o)
{
    uint32_t n = --o->RefCount;
    if (n == 0) x86_free(o);
    return n;
}

EXTERN_C uint32_t IDirectInputA_CreateDevice_c(di_object *o, void *rguid, uint32_t *lplpDirectInputDevice, void *pUnkOuter)
{
    int kind;
    if (lplpDirectInputDevice == NULL) return DIERR_INVALIDPARAM;
    *lplpDirectInputDevice = 0;
    if (same_guid(rguid, 0x6f1d2b61)) kind = KIND_KEYBOARD;       /* GUID_SysKeyboard */
    else if (same_guid(rguid, 0x6f1d2b60)) kind = KIND_MOUSE;     /* GUID_SysMouse */
    else
    {
        if (trace()) fprintf(stderr, "DInput: CreateDevice: unknown device %08x\n", rguid ? rd32(rguid) : 0);
        return DIERR_DEVICENOTREG;
    }
    di_device *d = (di_device *)x86_calloc(1, sizeof(di_device));
    if (d == NULL) return DIERR_OUTOFMEMORY;
    d->lpVtbl = to_guest(IDirectInputDevice2AVtbl_asm2c);
    d->RefCount = 1;
    d->kind = kind;
    d->relative = 1;
    d->axis_count = 3;
    d->button_count = 4;
    for (int i = 0; i < 3; i++) d->axis_ofs[i] = 4 * i;
    for (int i = 0; i < 8; i++) d->button_ofs[i] = 12 + i;
    init();
    SDL_LockMutex(lock);
    for (int i = 0; i < 8; i++)
    {
        if (devices[i] == NULL)
        {
            devices[i] = d;
            if (i >= device_count) device_count = i + 1;
            break;
        }
    }
    SDL_UnlockMutex(lock);
    wr32(lplpDirectInputDevice, to_guest(d));
    if (trace()) fprintf(stderr, "DInput: CreateDevice %s\n", kind == KIND_KEYBOARD ? "keyboard" : "mouse");
    return DI_OK;
}

/* DIDEVICEINSTANCEA for the keyboard or the mouse */
static void fill_instance(uint8_t *di, int kind)
{
    uint32_t size = rd32(di);
    static const uint8_t tail[8] = { 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };
    if (size < 576 || size > 580) size = 580;
    memset(di, 0, size);
    wr32(di, size);
    wr32(di + 4, kind == KIND_KEYBOARD ? 0x6f1d2b61 : 0x6f1d2b60);   /* guidInstance */
    wr16(di + 8, 0xd5a0);
    wr16(di + 10, 0x11cf);
    memcpy(di + 12, tail, 8);
    memcpy(di + 20, di + 4, 16);                                       /* guidProduct */
    wr32(di + 36, kind == KIND_KEYBOARD ? DIDEVTYPE_KEYBOARD : DIDEVTYPE_MOUSE);
    strcpy((char *)di + 40, kind == KIND_KEYBOARD ? "Keyboard" : "Mouse");
    strcpy((char *)di + 300, kind == KIND_KEYBOARD ? "Keyboard" : "Mouse");
}

EXTERN_C uint32_t IDirectInputA_EnumDevices_c(di_object *o, uint32_t dwDevType, uint32_t lpCallback, uint32_t pvRef, uint32_t dwFlags)
{
    int kinds[2], n = 0;
    if (trace()) fprintf(stderr, "DInput: EnumDevices type %u flags 0x%x\n", dwDevType, dwFlags);
    if (dwDevType == 0 || dwDevType == DIDEVTYPE_KEYBOARD) kinds[n++] = KIND_KEYBOARD;
    if (dwDevType == 0 || dwDevType == DIDEVTYPE_MOUSE) kinds[n++] = KIND_MOUSE;
    for (int i = 0; i < n; i++)
    {
        uint8_t *di = (uint8_t *)x86_calloc(1, 580);
        wr32(di, 580);
        fill_instance(di, kinds[i]);
        uint32_t args[2] = { to_guest(di), pvRef };
        uint32_t ret = CallX86Function(lpCallback, 2, args);
        x86_free(di);
        if (ret == 0) break;   /* DIENUM_STOP */
    }
    return DI_OK;
}

EXTERN_C uint32_t IDirectInputA_GetDeviceStatus_c(di_object *o, void *rguidInstance)
{
    if (same_guid(rguidInstance, 0x6f1d2b61) || same_guid(rguidInstance, 0x6f1d2b60)) return DI_OK;
    return DIERR_DEVICENOTREG;
}

/* ----------------------------------------------------- IDirectInputDevice2A */

EXTERN_C uint32_t IDirectInputDevice2A_QueryInterface_c(di_device *d, void *riid, uint32_t *ppvObj)
{
    if (ppvObj == NULL) return DIERR_INVALIDPARAM;
    /* IDirectInputDevice2A and older: the vtable is a superset */
    d->RefCount++;
    wr32(ppvObj, to_guest(d));
    return DI_OK;
}

EXTERN_C uint32_t IDirectInputDevice2A_AddRef_c(di_device *d) { return ++d->RefCount; }

EXTERN_C uint32_t IDirectInputDevice2A_Release_c(di_device *d)
{
    uint32_t n = --d->RefCount;
    if (n != 0) return n;
    SDL_LockMutex(lock);
    for (int i = 0; i < device_count; i++)
    {
        if (devices[i] == d) devices[i] = NULL;
    }
    SDL_UnlockMutex(lock);
    free(d->ring);
    x86_free(d);
    return 0;
}

EXTERN_C uint32_t IDirectInputDevice2A_GetCapabilities_c(di_device *d, uint8_t *caps)
{
    if (caps == NULL) return DIERR_INVALIDPARAM;
    uint32_t size = rd32(caps);
    if (size < 24 || size > 44) return DIERR_INVALIDPARAM;
    memset(caps + 4, 0, size - 4);
    wr32(caps + 4, 0x00000001);   /* DIDC_ATTACHED */
    if (d->kind == KIND_KEYBOARD)
    {
        wr32(caps + 8, DIDEVTYPE_KEYBOARD | (4 << 8));   /* DIDEVTYPEKEYBOARD_PCENH */
        wr32(caps + 16, 128);
    }
    else
    {
        wr32(caps + 8, DIDEVTYPE_MOUSE);
        wr32(caps + 12, 3);
        wr32(caps + 16, 3);
    }
    return DI_OK;
}

EXTERN_C uint32_t IDirectInputDevice2A_GetProperty_c(di_device *d, uint32_t rguidProp, uint8_t *pdiph)
{
    if (pdiph == NULL) return DIERR_INVALIDPARAM;
    if (rguidProp == DIPROP_BUFFERSIZE) { wr32(pdiph + 16, d->ring_size); return DI_OK; }
    if (rguidProp == DIPROP_AXISMODE) { wr32(pdiph + 16, d->relative ? 1 : 0); return DI_OK; }
    return DIERR_UNSUPPORTED;
}

EXTERN_C uint32_t IDirectInputDevice2A_SetProperty_c(di_device *d, uint32_t rguidProp, uint8_t *pdiph)
{
    if (pdiph == NULL) return DIERR_INVALIDPARAM;
    uint32_t value = rd32(pdiph + 16);
    if (trace()) fprintf(stderr, "DInput: SetProperty %u = %u\n", rguidProp, value);
    if (rguidProp == DIPROP_BUFFERSIZE)
    {
        if (value > MAX_BUFFER) value = MAX_BUFFER;
        SDL_LockMutex(lock);
        free(d->ring);
        d->ring = value ? (di_object_data *)calloc(value, sizeof(di_object_data)) : NULL;
        d->ring_size = value;
        d->ring_head = d->ring_count = 0;
        d->overflow = 0;
        SDL_UnlockMutex(lock);
        return DI_OK;
    }
    if (rguidProp == DIPROP_AXISMODE)
    {
        d->relative = (value == 1);
        return DI_OK;
    }
    return DI_OK;   /* ranges, dead zones: no effect on a keyboard or mouse */
}

EXTERN_C uint32_t IDirectInputDevice2A_Acquire_c(di_device *d)
{
    SDL_LockMutex(lock);
    if (!d->acquired)
    {
        d->acquired = 1;
        d->axis[0] = d->axis[1] = d->axis[2] = 0;
    }
    SDL_UnlockMutex(lock);
    return DI_OK;
}

EXTERN_C uint32_t IDirectInputDevice2A_Unacquire_c(di_device *d)
{
    SDL_LockMutex(lock);
    d->acquired = 0;
    d->ring_head = d->ring_count = 0;
    SDL_UnlockMutex(lock);
    return DI_OK;
}

EXTERN_C uint32_t IDirectInputDevice2A_GetDeviceState_c(di_device *d, uint32_t cbData, uint8_t *data)
{
    if (data == NULL) return DIERR_INVALIDPARAM;
    if (!d->acquired) return DIERR_NOTACQUIRED;
    memset(data, 0, cbData);
    SDL_LockMutex(lock);
    if (d->kind == KIND_KEYBOARD)
    {
        memcpy(data, d->keys, cbData < 256 ? cbData : 256);
    }
    else
    {
        for (int i = 0; i < d->axis_count && i < 3; i++)
        {
            if (d->axis_ofs[i] + 4 <= cbData) wr32(data + d->axis_ofs[i], (uint32_t)d->axis[i]);
        }
        for (int i = 0; i < d->button_count && i < 8; i++)
        {
            if (d->button_ofs[i] < cbData) data[d->button_ofs[i]] = d->buttons[i];
        }
        if (d->relative) d->axis[0] = d->axis[1] = d->axis[2] = 0;
    }
    SDL_UnlockMutex(lock);
    return DI_OK;
}

EXTERN_C uint32_t IDirectInputDevice2A_GetDeviceData_c(di_device *d, uint32_t cbObjectData, uint8_t *rgdod, uint32_t *pdwInOut, uint32_t dwFlags)
{
    uint32_t max, n = 0, ret;
    if (pdwInOut == NULL || cbObjectData < 16) return DIERR_INVALIDPARAM;
    if (!d->acquired) return DIERR_NOTACQUIRED;
    max = rd32(pdwInOut);
    SDL_LockMutex(lock);
    while (n < max && n < d->ring_count)
    {
        di_object_data *e = &d->ring[(d->ring_head + n) % d->ring_size];
        if (rgdod != NULL)
        {
            memset(rgdod + n * cbObjectData, 0, cbObjectData);
            memcpy(rgdod + n * cbObjectData, e, 16);
        }
        n++;
    }
    if (!(dwFlags & DIGDD_PEEK))
    {
        d->ring_head = d->ring_size ? (d->ring_head + n) % d->ring_size : 0;
        d->ring_count -= n;
    }
    ret = d->overflow ? DI_BUFFEROVERFLOW : DI_OK;
    if (!(dwFlags & DIGDD_PEEK)) d->overflow = 0;
    SDL_UnlockMutex(lock);
    wr32(pdwInOut, n);
    return ret;
}

/* DIDATAFORMAT: dwSize, dwObjSize, dwFlags, dwDataSize, dwNumObjs, rgodf.
 * DIOBJECTDATAFORMAT: pguid, dwOfs, dwType, dwFlags. For the mouse, the
 * axes and buttons are taken in their order in the format. */
EXTERN_C uint32_t IDirectInputDevice2A_SetDataFormat_c(di_device *d, uint8_t *lpdf)
{
    if (lpdf == NULL) return DIERR_INVALIDPARAM;
    uint32_t obj_size = rd32(lpdf + 4), flags = rd32(lpdf + 8);
    uint32_t count = rd32(lpdf + 16);
    uint8_t *objs = (uint8_t *)from_guest(rd32(lpdf + 20));
    d->data_size = rd32(lpdf + 12);
    if (trace()) fprintf(stderr, "DInput: SetDataFormat %s: size %u, %u objects, flags 0x%x\n",
                         d->kind == KIND_KEYBOARD ? "keyboard" : "mouse", d->data_size, count, flags);
    if (d->kind == KIND_MOUSE && objs != NULL && obj_size >= 16)
    {
        d->axis_count = d->button_count = 0;
        d->relative = !(flags & 1);   /* DIDF_ABSAXIS */
        for (uint32_t i = 0; i < count; i++)
        {
            uint8_t *od = objs + i * obj_size;
            uint32_t ofs = rd32(od + 4), type = rd32(od + 8);
            if ((type & DIDFT_AXIS) && d->axis_count < 3) d->axis_ofs[d->axis_count++] = ofs;
            else if ((type & DIDFT_BUTTON) && d->button_count < 8) d->button_ofs[d->button_count++] = ofs;
        }
    }
    return DI_OK;
}

EXTERN_C uint32_t IDirectInputDevice2A_SetEventNotification_c(di_device *d, void *hEvent)
{
    d->event = hEvent;
    return DI_OK;
}

EXTERN_C uint32_t IDirectInputDevice2A_SetCooperativeLevel_c(di_device *d, void *hwnd, uint32_t dwFlags)
{
    if (trace()) fprintf(stderr, "DInput: SetCooperativeLevel %s 0x%x\n", d->kind == KIND_KEYBOARD ? "keyboard" : "mouse", dwFlags);
    return DI_OK;
}

EXTERN_C uint32_t IDirectInputDevice2A_GetDeviceInfo_c(di_device *d, uint8_t *pdidi)
{
    if (pdidi == NULL) return DIERR_INVALIDPARAM;
    fill_instance(pdidi, d->kind);
    return DI_OK;
}

EXTERN_C uint32_t IDirectInputDevice2A_EnumObjects_c(di_device *d, uint32_t lpCallback, void *pvRef, uint32_t dwFlags)
{
    return DI_OK;   /* no objects are listed */
}

EXTERN_C uint32_t IDirectInputDevice2A_Poll_c(di_device *d)
{
    return DI_OK;
}

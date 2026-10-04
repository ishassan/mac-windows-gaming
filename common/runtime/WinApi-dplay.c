/*
 *  Native port: DirectPlay objects (IDirectPlay4A, IDirectPlayLobby3A) for
 *  games that create them at start-up even for single-player (Revenant).
 *  There is no network: the objects exist, the lobby says "not lobbied",
 *  EnumConnections finds no service providers, and the other methods fail
 *  with DPERR_UNSUPPORTED (weak stubs from com/dplay.com).
 *  MIT license, see the README.md of the repository.
 */

#include "game-info.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "WinApi.h"
#include "Game-Memory.h"
#include "guest.h"

#define S_OK          0x00000000u
#define E_NOINTERFACE 0x80004002u
#define E_POINTER     0x80004003u

EXTERN_C uint8_t IDirectPlay4AVtbl_asm2c[];
EXTERN_C uint8_t IDirectPlayLobby3AVtbl_asm2c[];

typedef struct {
    uint32_t lpVtbl;
    uint32_t RefCount;
} com_object;

static uint32_t create(uint8_t *vtbl, void *ppv)
{
    com_object *o = (com_object *)x86_malloc(sizeof(com_object));
    if (o == NULL) return 0x8007000eu;   /* E_OUTOFMEMORY */
    o->lpVtbl = to_guest(vtbl);
    o->RefCount = 1;
    wr32(ppv, to_guest(o));
    return S_OK;
}

/* CoCreateInstance (WinApi-misc.c) calls this for CLSID_DirectPlay
 * {D1EB6D20-8923-11D0-9D97-00A0C90A43CB} and CLSID_DirectPlayLobby
 * {2FE8F810-B2A5-11D0-A787-0000F803ABFC}. Returns 1 if it handled the class. */
EXTERN_C int DPlay_CoCreateInstance(const uint8_t *rclsid, uint32_t *result, void *ppv)
{
    uint32_t d1 = rd32(rclsid);
    if (d1 == 0xd1eb6d20u) { *result = create(IDirectPlay4AVtbl_asm2c, ppv); return 1; }
    if (d1 == 0x2fe8f810u) { *result = create(IDirectPlayLobby3AVtbl_asm2c, ppv); return 1; }
    return 0;
}

static uint32_t query_interface(com_object *o, void *ppv)
{
    if (ppv == NULL) return E_POINTER;
    /* any DirectPlay interface version: the vtable is the newest one,
     * and the older interfaces are prefixes of it */
    o->RefCount++;
    wr32(ppv, to_guest(o));
    return S_OK;
}

static uint32_t release(com_object *o)
{
    uint32_t n = --o->RefCount;
    if (n == 0) x86_free(o);
    return n;
}

EXTERN_C uint32_t IDirectPlay4A_QueryInterface_c(com_object *o, void *riid, void *ppv) { return query_interface(o, ppv); }
EXTERN_C uint32_t IDirectPlay4A_AddRef_c(com_object *o) { return ++o->RefCount; }
EXTERN_C uint32_t IDirectPlay4A_Release_c(com_object *o) { return release(o); }
EXTERN_C uint32_t IDirectPlay4A_Close_c(com_object *o) { return S_OK; }

EXTERN_C uint32_t IDirectPlayLobby3A_QueryInterface_c(com_object *o, void *riid, void *ppv) { return query_interface(o, ppv); }
EXTERN_C uint32_t IDirectPlayLobby3A_AddRef_c(com_object *o) { return ++o->RefCount; }
EXTERN_C uint32_t IDirectPlayLobby3A_Release_c(com_object *o) { return release(o); }

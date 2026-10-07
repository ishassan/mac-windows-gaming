/*
 *  Port change: smaller KERNEL32, USER32, GDI32, ole32, OLEAUT32, IMM32,
 *  DBGHELP, AVIFIL32 and WSOCK32 functions (first needed by Generals Zero
 *  Hour). Parts that the port does not support (online play, input method
 *  editors, crash reports with symbols, movie capture) report failure, and
 *  the games then skip them.
 *
 *  MIT license, see README.md.
 */

#define _FILE_OFFSET_BITS 64
#include "game-info.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include "WinApi.h"
#include "Game-Memory.h"
#include "guest.h"

#ifndef EXTERN_C
#define EXTERN_C extern "C"
#endif

#define E_FAIL      0x80004005u
#define E_NOTIMPL   0x80004001u
#define S_FALSE     1u

/* ================================================================ KERNEL32 */

EXTERN_C uint32_t GetFileSize_c(void *hFile, void *lpFileSizeHigh)
{
    handle h = (handle)hFile;
    struct stat st;
    if (h == NULL || h->handle_type != HT_FILE || h->fh.f == NULL) return 0xffffffffu;
    if (fstat(fileno((FILE *)h->fh.f), &st) != 0) return 0xffffffffu;
    if (lpFileSizeHigh) wr32(lpFileSizeHigh, (uint32_t)((uint64_t)st.st_size >> 32));
    return (uint32_t)st.st_size;
}

EXTERN_C uint32_t SetFileTime_c(void *hFile, void *c, void *a, void *w) { return 1; }

/* FILETIME from a FAT date and time (local time) */
EXTERN_C uint32_t DosDateTimeToFileTime_c(uint32_t wFatDate, uint32_t wFatTime, void *lpFileTime)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = ((wFatDate >> 9) & 0x7f) + 80;
    t.tm_mon = ((wFatDate >> 5) & 0xf) - 1;
    t.tm_mday = wFatDate & 0x1f;
    t.tm_hour = (wFatTime >> 11) & 0x1f;
    t.tm_min = (wFatTime >> 5) & 0x3f;
    t.tm_sec = (wFatTime & 0x1f) * 2;
    t.tm_isdst = -1;
    uint64_t ft = ((uint64_t)timegm(&t) + 11644473600ull) * 10000000ull;
    wr32(lpFileTime, (uint32_t)ft);
    wr32((uint8_t *)lpFileTime + 4, (uint32_t)(ft >> 32));
    return 1;
}

EXTERN_C uint32_t FormatMessageA_c(uint32_t f, void *src, uint32_t id, uint32_t lang, void *buf, uint32_t size, void *args) { return 0; }
EXTERN_C uint32_t FormatMessageW_c(uint32_t f, void *src, uint32_t id, uint32_t lang, void *buf, uint32_t size, void *args) { return 0; }

EXTERN_C uint32_t GetComputerNameA_c(char *lpBuffer, void *nSize)
{
    const char *name = "MAC";
    uint32_t n = rd32(nSize);
    if (n < strlen(name) + 1) { wr32(nSize, (uint32_t)strlen(name) + 1); return 0; }
    strcpy(lpBuffer, name);
    wr32(nSize, (uint32_t)strlen(name));
    return 1;
}

/* the game folder is drive C: (see GetCurrentDirectoryA) */
EXTERN_C uint32_t GetTempPathA_c(uint32_t n, char *buf)
{
    const char *p = "C:\\";
    if (n < 4) return 4;
    strcpy(buf, p);
    return 3;
}

EXTERN_C uint32_t GetPriorityClass_c(void *h) { return 0x20; }       /* NORMAL_PRIORITY_CLASS */
EXTERN_C uint32_t GetThreadPriority_c(void *h) { return 0; }         /* THREAD_PRIORITY_NORMAL */
EXTERN_C uint32_t TerminateThread_c(void *h, uint32_t code)
{
    LOG_ONCE("TerminateThread: not supported\n");
    return 0;
}

/* SYSTEMTIME (16 bytes) or the current time, as text in 16-bit characters */
static void wide_time_text(void *out, uint32_t cch, const char *text, uint32_t *result)
{
    uint32_t n = (uint32_t)strlen(text) + 1;
    *result = n;
    if (out == NULL || cch == 0) return;
    if (cch < n) { *result = 0; return; }
    for (uint32_t i = 0; i < n; i++) wr16((uint8_t *)out + 2 * i, (uint16_t)(uint8_t)text[i]);
}

static struct tm system_time(const void *st)
{
    struct tm t;
    if (st == NULL)
    {
        time_t now = time(NULL);
        localtime_r(&now, &t);
        return t;
    }
    const uint8_t *s = (const uint8_t *)st;
    memset(&t, 0, sizeof(t));
    t.tm_year = (s[0] | (s[1] << 8)) - 1900;
    t.tm_mon = (s[2] | (s[3] << 8)) - 1;
    t.tm_wday = s[4] | (s[5] << 8);
    t.tm_mday = s[6] | (s[7] << 8);
    t.tm_hour = s[8] | (s[9] << 8);
    t.tm_min = s[10] | (s[11] << 8);
    t.tm_sec = s[12] | (s[13] << 8);
    return t;
}

EXTERN_C uint32_t GetDateFormatW_c(uint32_t locale, uint32_t flags, void *lpDate, void *lpFormat, void *lpDateStr, uint32_t cchDate)
{
    struct tm t = system_time(lpDate);
    char buf[64];
    strftime(buf, sizeof(buf), "%m/%d/%Y", &t);
    uint32_t r;
    wide_time_text(lpDateStr, cchDate, buf, &r);
    return r;
}

EXTERN_C uint32_t GetTimeFormatW_c(uint32_t locale, uint32_t flags, void *lpTime, void *lpFormat, void *lpTimeStr, uint32_t cchTime)
{
    struct tm t = system_time(lpTime);
    char buf[64];
    strftime(buf, sizeof(buf), "%I:%M:%S %p", &t);
    uint32_t r;
    wide_time_text(lpTimeStr, cchTime, buf, &r);
    return r;
}

/* Fixed memory: the handle is the pointer */
EXTERN_C void *GlobalHandle_c(void *pMem) { return pMem; }
EXTERN_C void *GlobalLock_c(void *hMem) { return hMem; }
EXTERN_C uint32_t GlobalUnlock_c(void *hMem) { return 0; }

EXTERN_C void *LocalAlloc_c(uint32_t uFlags, uint32_t uBytes)
{
    void *p = x86_malloc(uBytes ? uBytes : 1);
    if (p && (uFlags & 0x40)) memset(p, 0, uBytes);   /* LMEM_ZEROINIT */
    return p;
}

EXTERN_C void *LocalFree_c(void *hMem)
{
    if (hMem) x86_free(hMem);
    return NULL;
}

/* PF_FLOATING_POINT_PRECISION_ERRATA 0, PF_FLOATING_POINT_EMULATED 1:
   no. The SIMD features (MMX 3, XMMI 6, 3DNOW 7, XMMI64 10): no, as with
   CPUID (the translated code has no SIMD instructions). */
EXTERN_C uint32_t IsProcessorFeaturePresent_c(uint32_t feature) { return 0; }

EXTERN_C uint32_t MulDiv_c(int32_t a, int32_t b, int32_t c)
{
    if (c == 0) return (uint32_t)-1;
    int64_t r = (int64_t)a * b;
    r = (r >= 0) ? (r + (c > 0 ? c : -c) / 2) / c : (r - (c > 0 ? c : -c) / 2) / c;
    return (uint32_t)(int32_t)r;
}

EXTERN_C void *MapViewOfFileEx_c(void *h, uint32_t access, uint32_t hi, uint32_t lo, uint32_t n, void *base)
{
    LOG_ONCE("MapViewOfFileEx: not supported\n");
    return NULL;
}

EXTERN_C uint32_t UnmapViewOfFile_c(void *p) { return 1; }

/* No other process makes named events here */
EXTERN_C void *OpenEventA_c(uint32_t access, uint32_t inherit, const char *name)
{
    Winapi_SetLastError(2);   /* ERROR_FILE_NOT_FOUND */
    return NULL;
}

/* ================================================================ USER32 */

/* RECT of the window from the client RECT: no frame (the SDL window) */
EXTERN_C uint32_t AdjustWindowRect_c(void *lpRect, uint32_t style, uint32_t menu) { return 1; }
EXTERN_C void *FindWindowA_c(const char *cls, const char *name) { return NULL; }
EXTERN_C uint32_t GetDoubleClickTime_c(void) { return 500; }
EXTERN_C uint32_t GetKeyboardLayout_c(uint32_t thread) { return 0x04090409; }   /* US English */

/* Cursor files (.ani, .cur): the system cursor is used. The handle only
   has to be different from NULL. */
EXTERN_C void *LoadCursorFromFileA_c(const char *name)
{
    static uint32_t next = 0xfa00;
    next += 4;
    if (next >= 0xfb00) next = 0xfa04;
    return guest_value(next);
}

EXTERN_C uint32_t MessageBoxW_c(void *hWnd, const uint16_t *text, const uint16_t *caption, uint32_t type)
{
    fprintf(stderr, "MessageBoxW: ");
    for (const uint16_t *p = caption; p && *p; p++) fputc(*p < 0x80 ? *p : '?', stderr);
    fprintf(stderr, ": ");
    for (const uint16_t *p = text; p && *p; p++) fputc(*p < 0x80 ? *p : '?', stderr);
    fprintf(stderr, "\n");
    return 1;   /* IDOK */
}

/* The client area starts at 0, 0 of the screen (as for ClientToScreen) */
EXTERN_C uint32_t ScreenToClient_c(void *hWnd, void *lpPoint) { return 1; }
EXTERN_C uint32_t SetForegroundWindow_c(void *hWnd) { return 1; }
EXTERN_C uint32_t SetWindowTextW_c(void *hWnd, const uint16_t *text) { return 1; }

/* ================================================================ GDI32 */

/* Font files of the game: macOS has the fonts that the games use */
EXTERN_C uint32_t AddFontResourceA_c(const char *name) { return 1; }
EXTERN_C uint32_t RemoveFontResourceA_c(const char *name) { return 1; }
EXTERN_C uint32_t SaveDC_c(void *hdc) { return 1; }
EXTERN_C uint32_t RestoreDC_c(void *hdc, int32_t n) { return 1; }
/* Gamma: not changed (the screen is shared with other programs) */
EXTERN_C uint32_t SetDeviceGammaRamp_c(void *hdc, void *ramp) { return 1; }

/* ================================================================ ole32, OLEAUT32 */

EXTERN_C uint32_t OleInitialize_c(void *r) { return 0; }
EXTERN_C void OleUninitialize_c(void) {}
EXTERN_C uint32_t OleRun_c(void *p) { return E_FAIL; }
EXTERN_C uint32_t GetErrorInfo_c(uint32_t r, void *pp) { if (pp) wr32(pp, 0); return S_FALSE; }
EXTERN_C uint32_t CreateStdDispatch_c(void *a, void *b, void *c, void *pp) { if (pp) wr32(pp, 0); return E_NOTIMPL; }
EXTERN_C uint32_t LoadTypeLib_c(void *file, void *pp) { if (pp) wr32(pp, 0); return E_FAIL; }

/* BSTR: the 4-byte length (in bytes) is before the string */
EXTERN_C void *SysAllocString_c(const uint16_t *s)
{
    if (s == NULL) return NULL;
    uint32_t n = 0;
    while (s[n]) n++;
    uint8_t *b = (uint8_t *)x86_malloc(4 + 2 * n + 2);
    wr32(b, 2 * n);
    memcpy(b + 4, s, 2 * n + 2);
    return b + 4;
}

EXTERN_C void SysFreeString_c(void *s) { if (s) x86_free((uint8_t *)s - 4); }
EXTERN_C uint32_t VariantClear_c(void *v) { if (v) wr16(v, 0); return 0; }

/* ================================================================ IMM32 */

EXTERN_C uint32_t ImmGetContext_c(void *hWnd) { return 0; }
EXTERN_C uint32_t ImmReleaseContext_c(void *hWnd, uint32_t himc) { return 1; }
EXTERN_C uint32_t ImmAssociateContext_c(void *hWnd, uint32_t himc) { return 0; }
EXTERN_C uint32_t ImmCreateContext_c(void) { return 0; }
EXTERN_C uint32_t ImmDestroyContext_c(uint32_t himc) { return 1; }
EXTERN_C uint32_t ImmGetCompositionStringA_c(uint32_t h, uint32_t i, void *b, uint32_t n) { return 0; }
EXTERN_C uint32_t ImmGetCompositionStringW_c(uint32_t h, uint32_t i, void *b, uint32_t n) { return 0; }
EXTERN_C uint32_t ImmGetCandidateListA_c(uint32_t h, uint32_t i, void *b, uint32_t n) { return 0; }
EXTERN_C uint32_t ImmGetCandidateListW_c(uint32_t h, uint32_t i, void *b, uint32_t n) { return 0; }
EXTERN_C uint32_t ImmGetCandidateListCountA_c(uint32_t h, void *c) { if (c) wr32(c, 0); return 0; }
EXTERN_C uint32_t ImmGetCandidateListCountW_c(uint32_t h, void *c) { if (c) wr32(c, 0); return 0; }
EXTERN_C uint32_t ImmGetProperty_c(uint32_t hkl, uint32_t index) { return 0; }

/* ================================================================ DBGHELP */

EXTERN_C uint32_t SymInitialize_c(void *p, void *path, uint32_t invade) { return 0; }
EXTERN_C uint32_t SymCleanup_c(void *p) { return 1; }
EXTERN_C uint32_t SymSetOptions_c(uint32_t o) { return o; }
EXTERN_C uint32_t SymLoadModule_c(void *p, void *f, void *img, void *mod, uint32_t base, uint32_t size) { return 0; }
EXTERN_C uint32_t SymGetSymFromAddr_c(void *p, uint32_t addr, void *disp, void *sym) { return 0; }
EXTERN_C uint32_t SymGetModuleBase_c(void *p, uint32_t addr) { return 0; }
EXTERN_C uint32_t SymFunctionTableAccess_c(void *p, uint32_t addr) { return 0; }
EXTERN_C uint32_t StackWalk_c(uint32_t m, void *p, void *t, void *frame, void *ctx, uint32_t r, uint32_t f, uint32_t b, uint32_t tr) { return 0; }

/* ================================================================ AVIFIL32 */

EXTERN_C void AVIFileInit_c(void) {}
EXTERN_C void AVIFileExit_c(void) {}
EXTERN_C uint32_t AVIFileOpenA_c(void *pp, void *f, uint32_t m, void *h) { if (pp) wr32(pp, 0); return E_FAIL; }
EXTERN_C uint32_t AVIFileCreateStreamA_c(void *f, void *pp, void *si) { if (pp) wr32(pp, 0); return E_FAIL; }
EXTERN_C uint32_t AVIStreamSetFormat_c(void *s, uint32_t pos, void *fmt, uint32_t n) { return E_FAIL; }
EXTERN_C uint32_t AVIStreamWrite_c(void *s, uint32_t a, uint32_t b, void *buf, uint32_t n, uint32_t fl, void *w, void *wb) { return E_FAIL; }
EXTERN_C uint32_t AVIFileRelease_c(void *f) { return 0; }
EXTERN_C uint32_t AVIStreamRelease_c(void *s) { return 0; }

/* ================================================================ WSOCK32 */

#define WSAENETDOWN 10050

EXTERN_C uint32_t ws_WSAGetLastError_c(void) { return WSAENETDOWN; }
EXTERN_C uint32_t ws___WSAFDIsSet_c(uint32_t s, void *set)
{
    uint32_t n = rd32(set);
    for (uint32_t i = 0; i < n && i < 64; i++) if (rd32((uint8_t *)set + 4 + 4 * i) == s) return 1;
    return 0;
}
EXTERN_C uint32_t ws_getsockname_c(uint32_t s, void *name, void *len) { return (uint32_t)-1; }
EXTERN_C uint32_t ws_getsockopt_c(uint32_t s, uint32_t l, uint32_t o, void *v, void *len) { return (uint32_t)-1; }
EXTERN_C uint32_t ws_htonl_c(uint32_t x) { return htonl(x); }
EXTERN_C uint32_t ws_ntohl_c(uint32_t x) { return ntohl(x); }
EXTERN_C uint32_t ws_ntohs_c(uint32_t x) { return ntohs((uint16_t)x); }
EXTERN_C uint32_t ws_recvfrom_c(uint32_t s, void *b, uint32_t n, uint32_t f, void *from, void *len) { return (uint32_t)-1; }
EXTERN_C uint32_t ws_sendto_c(uint32_t s, void *b, uint32_t n, uint32_t f, void *to, uint32_t len) { return (uint32_t)-1; }
EXTERN_C uint32_t ws_shutdown_c(uint32_t s, uint32_t how) { return (uint32_t)-1; }

/*
 *  Commandos port: KERNEL32 functions that the statically linked MSVC 2005
 *  C runtime of comandos.exe needs (heap, TLS, locale, environment, console,
 *  time, SEH exceptions), and a few the game uses.
 *
 *  The SR Septerra port (WinApi-kernel32.c) has the file and search
 *  functions; this file adds the rest. MIT license, see README.md.
 */

#define _FILE_OFFSET_BITS 64
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <map>
#include "WinApi.h"
#include "Game-Memory.h"
#include "CLIB.h"
#include "guest.h"
#include "llasm/llasm_cpu.h"

#define EXTERN_C extern "C"

#define ERROR_CALL_NOT_IMPLEMENTED 120
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_INVALID_HANDLE_VALUE 6

/* ---------------------------------------------------------------- errors */

EXTERN_C void SetLastError_c(uint32_t dwErrCode)
{
    Winapi_SetLastError(dwErrCode);
}

/* ------------------------------------------------------------------ heap */

static uint32_t default_heap_object[4];

EXTERN_C void *GetProcessHeap_c(void)
{
    return default_heap_object;
}

EXTERN_C void *HeapCreate_c(uint32_t flOptions, uint32_t dwInitialSize, uint32_t dwMaximumSize)
{
    /* one allocator for all heaps; the handle only has to be unique */
    void *h = x86_malloc(16);
    return h;
}

EXTERN_C uint32_t HeapDestroy_c(void *hHeap)
{
    if (hHeap != default_heap_object) x86_free(hHeap);
    return 1;
}

#define HEAP_ZERO_MEMORY 0x08
#define HEAP_REALLOC_IN_PLACE_ONLY 0x10

EXTERN_C void *HeapAlloc_c(void *hHeap, uint32_t dwFlags, uint32_t dwBytes)
{
    void *p;
    if (dwBytes == 0) dwBytes = 1;
    p = (dwFlags & HEAP_ZERO_MEMORY) ? x86_calloc(1, dwBytes) : x86_malloc(dwBytes);
    if (p == NULL) Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return p;
}

EXTERN_C uint32_t HeapFree_c(void *hHeap, uint32_t dwFlags, void *lpMem)
{
    x86_free(lpMem);
    return 1;
}

EXTERN_C void *HeapReAlloc_c(void *hHeap, uint32_t dwFlags, void *lpMem, uint32_t dwBytes)
{
    unsigned int old_size = x86_msize(lpMem);
    void *p;

    if (dwBytes == 0) dwBytes = 1;
    if (dwFlags & HEAP_REALLOC_IN_PLACE_ONLY)
    {
        return (dwBytes <= old_size) ? lpMem : NULL;
    }
    p = x86_realloc(lpMem, dwBytes);
    if (p == NULL)
    {
        Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if ((dwFlags & HEAP_ZERO_MEMORY) && dwBytes > old_size)
    {
        memset((uint8_t *)p + old_size, 0, dwBytes - old_size);
    }
    return p;
}

EXTERN_C uint32_t HeapSize_c(void *hHeap, uint32_t dwFlags, void *lpMem)
{
    return x86_msize(lpMem);
}

/* --------------------------------------------------------- virtual memory */

#define MEM_COMMIT  0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000

static std::map<uintptr_t, uint32_t> virtual_regions;

EXTERN_C void *VirtualAlloc_c(void *lpAddress, uint32_t dwSize, uint32_t flAllocationType, uint32_t flProtect)
{
    void *mem;
    uint32_t size;

    if (lpAddress != NULL)
    {
        /* commit inside a region reserved before: the memory is already usable */
        auto it = virtual_regions.upper_bound((uintptr_t)lpAddress);
        if (it != virtual_regions.begin())
        {
            --it;
            if ((uintptr_t)lpAddress + dwSize <= it->first + it->second)
            {
                return lpAddress;
            }
        }
        LOG_ONCE("VirtualAlloc: fixed address not supported\n");
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    size = (dwSize + 0xffff) & ~0xffffu;
    mem = map_memory_32bit(size, 0);
    if (mem == NULL)
    {
        Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    virtual_regions[(uintptr_t)mem] = size;
    return mem;
}

EXTERN_C uint32_t VirtualFree_c(void *lpAddress, uint32_t dwSize, uint32_t dwFreeType)
{
    if (dwFreeType & MEM_RELEASE)
    {
        auto it = virtual_regions.find((uintptr_t)lpAddress);
        if (it == virtual_regions.end()) return 0;
        unmap_memory_32bit(lpAddress, it->second);
        virtual_regions.erase(it);
    }
    return 1;   /* decommit: keep the memory */
}

/* ------------------------------------------------------------------- TLS */

#define TLS_SLOTS 64
static int tls_used[TLS_SLOTS];
static thread_local uint32_t tls_values[TLS_SLOTS];

EXTERN_C uint32_t TlsAlloc_c(void)
{
    for (int i = 0; i < TLS_SLOTS; i++)
    {
        if (!tls_used[i])
        {
            tls_used[i] = 1;
            return i;
        }
    }
    return 0xffffffff;
}

EXTERN_C uint32_t TlsFree_c(uint32_t dwTlsIndex)
{
    if (dwTlsIndex >= TLS_SLOTS) return 0;
    tls_used[dwTlsIndex] = 0;
    return 1;
}

EXTERN_C uint32_t TlsGetValue_c(uint32_t dwTlsIndex)
{
    if (dwTlsIndex >= TLS_SLOTS) return 0;
    Winapi_SetLastError(0);
    return tls_values[dwTlsIndex];
}

EXTERN_C uint32_t TlsSetValue_c(uint32_t dwTlsIndex, uint32_t lpTlsValue)
{
    if (dwTlsIndex >= TLS_SLOTS) return 0;
    tls_values[dwTlsIndex] = lpTlsValue;
    return 1;
}

/* ----------------------------------------------------------- interlocked */

EXTERN_C uint32_t InterlockedIncrement_c(void *lpAddend)
{
    return __atomic_add_fetch((uint32_t *)lpAddend, 1, __ATOMIC_SEQ_CST);
}

EXTERN_C uint32_t InterlockedDecrement_c(void *lpAddend)
{
    return __atomic_sub_fetch((uint32_t *)lpAddend, 1, __ATOMIC_SEQ_CST);
}

EXTERN_C uint32_t InterlockedExchange_c(void *Target, uint32_t Value)
{
    return __atomic_exchange_n((uint32_t *)Target, Value, __ATOMIC_SEQ_CST);
}

/* --------------------------------------------------------- process/module */

#define GUEST_HINSTANCE 0x00400000u
#define GUEST_HKERNEL32 0x7c800000u
#define MODULE_PATH "C:\\comandos.exe"

static char *guest_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *)x86_malloc((unsigned int)n);
    if (d) memcpy(d, s, n);
    return d;
}

EXTERN_C void *GetModuleHandleA_c(const char *lpModuleName)
{
    if (lpModuleName == NULL) return guest_value(GUEST_HINSTANCE);
    if (strncasecmp(lpModuleName, "kernel32", 8) == 0) return guest_value(GUEST_HKERNEL32);
    return NULL;
}

EXTERN_C uint32_t GetModuleFileNameA_c(void *hModule, char *lpFilename, uint32_t nSize)
{
    size_t n = strlen(MODULE_PATH);
    if (nSize == 0) return 0;
    if (n >= nSize)
    {
        memcpy(lpFilename, MODULE_PATH, nSize - 1);
        lpFilename[nSize - 1] = 0;
        Winapi_SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return nSize;
    }
    memcpy(lpFilename, MODULE_PATH, n + 1);
    return (uint32_t)n;
}

EXTERN_C void *LoadLibraryA_c(const char *lpLibFileName)
{
    LOG_ONCE("LoadLibraryA(%s): not available\n", lpLibFileName ? lpLibFileName : "");
    Winapi_SetLastError(ERROR_FILE_NOT_FOUND);
    return NULL;
}

EXTERN_C uint32_t FreeLibrary_c(void *hLibModule)
{
    return 1;
}

EXTERN_C uint32_t GetProcAddress_c(void *hModule, const char *lpProcName)
{
    /* The C runtime asks for optional functions (EncodePointer,
       IsProcessorFeaturePresent, ...) and works without them. */
    Winapi_SetLastError(127); /* ERROR_PROC_NOT_FOUND */
    return 0;
}

EXTERN_C void *GetCommandLineA_c(void)
{
    static char *cmdline;
    if (cmdline == NULL) cmdline = guest_strdup("\"" MODULE_PATH "\"");
    return cmdline;
}

EXTERN_C void GetStartupInfoA_c(void *lpStartupInfo)
{
    memset(lpStartupInfo, 0, 68);
    wr32(lpStartupInfo, 68);   /* cb */
}

EXTERN_C uint32_t GetCurrentProcessId_c(void)
{
    return (uint32_t)getpid();
}

EXTERN_C uint32_t GetCurrentThreadId_c(void)
{
    static thread_local uint32_t id;
    static uint32_t next_id = 1;
    if (id == 0) id = __atomic_fetch_add(&next_id, 1, __ATOMIC_SEQ_CST);
    return id;
}

EXTERN_C uint32_t TerminateProcess_c(void *hProcess, uint32_t uExitCode)
{
    exit((int)uExitCode);
}

EXTERN_C void FatalAppExitA_c(uint32_t uAction, const char *lpMessageText)
{
    fprintf(stderr, "FatalAppExit: %s\n", lpMessageText ? lpMessageText : "");
    exit(3);
}

EXTERN_C uint32_t IsDebuggerPresent_c(void)
{
    return 0;
}

EXTERN_C void DebugBreak_c(void)
{
    fprintf(stderr, "DebugBreak called\n");
}

EXTERN_C void OutputDebugStringA_c(const char *lpOutputString)
{
    if (lpOutputString) fprintf(stderr, "[debug] %s", lpOutputString);
}

EXTERN_C uint32_t GetVersionExA_c(void *lpVersionInformation)
{
    uint8_t *v = (uint8_t *)lpVersionInformation;
    uint32_t size = rd32(v);
    if (size != 148 && size != 156) return 0;
    memset(v + 4, 0, size - 4);
    wr32(v + 4, 5);       /* Windows XP 5.1.2600 */
    wr32(v + 8, 1);
    wr32(v + 12, 2600);
    wr32(v + 16, 2);      /* VER_PLATFORM_WIN32_NT */
    strcpy((char *)v + 20, "Service Pack 3");
    if (size == 156)
    {
        wr16(v + 148, 3); /* wServicePackMajor */
        v[154] = 1;       /* wProductType = VER_NT_WORKSTATION */
    }
    return 1;
}

/* --------------------------------------------------------------- console */

EXTERN_C void *GetStdHandle_c(uint32_t nStdHandle)
{
    static handle std_handles[3];
    int index;

    switch (nStdHandle)
    {
        case 0xfffffff6: index = 0; break;   /* STD_INPUT_HANDLE */
        case 0xfffffff5: index = 1; break;   /* STD_OUTPUT_HANDLE */
        case 0xfffffff4: index = 2; break;   /* STD_ERROR_HANDLE */
        default: return INVALID_HANDLE32_VALUE;
    }
    if (std_handles[index] == NULL)
    {
        std_handles[index] = Winapi_AllocHandle();
        std_handles[index]->fh.handle_type = HT_FILE;
        std_handles[index]->fh.f = (index == 0) ? stdin : ((index == 1) ? stdout : stderr);
    }
    return std_handles[index];
}

EXTERN_C uint32_t SetStdHandle_c(uint32_t nStdHandle, void *hHandle)
{
    return 1;
}

EXTERN_C uint32_t GetFileType_c(void *hFile)
{
    handle h = (handle)hFile;
    if (h == NULL || h == INVALID_HANDLE32_VALUE) return 0;
    if (h->handle_type == HT_FILE)
    {
        FILE *f = (FILE *)h->fh.f;
        return (f == stdin || f == stdout || f == stderr) ? 2 : 1;   /* CHAR : DISK */
    }
    return 0;
}

EXTERN_C uint32_t SetHandleCount_c(uint32_t uNumber)
{
    return uNumber;
}

EXTERN_C uint32_t GetConsoleCP_c(void) { return 1252; }
EXTERN_C uint32_t GetConsoleOutputCP_c(void) { return 1252; }

EXTERN_C uint32_t GetConsoleMode_c(void *hConsoleHandle, void *lpMode)
{
    Winapi_SetLastError(ERROR_INVALID_HANDLE);
    return 0;
}

EXTERN_C uint32_t WriteConsoleA_c(void *hConsoleOutput, const void *lpBuffer, uint32_t n, void *lpWritten, void *lpReserved)
{
    fwrite(lpBuffer, 1, n, stderr);
    if (lpWritten) wr32(lpWritten, n);
    return 1;
}

EXTERN_C uint32_t WriteConsoleW_c(void *hConsoleOutput, const void *lpBuffer, uint32_t n, void *lpWritten, void *lpReserved)
{
    Winapi_SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}

EXTERN_C uint32_t SetConsoleCtrlHandler_c(uint32_t HandlerRoutine, uint32_t Add)
{
    return 1;
}

/* ----------------------------------------------------------- environment */

EXTERN_C void *GetEnvironmentStrings_c(void)
{
    /* empty environment block: two zero bytes */
    static char *block;
    if (block == NULL) block = (char *)x86_calloc(1, 4);
    return block;
}

EXTERN_C void *GetEnvironmentStringsW_c(void)
{
    Winapi_SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

EXTERN_C uint32_t FreeEnvironmentStringsA_c(void *p) { return 1; }
EXTERN_C uint32_t FreeEnvironmentStringsW_c(void *p) { return 1; }

EXTERN_C uint32_t SetEnvironmentVariableA_c(const char *lpName, const char *lpValue)
{
    return 1;
}

/* --------------------------------------------------- files and folders */

EXTERN_C uint32_t GetCurrentDirectoryA_c(uint32_t nBufferLength, char *lpBuffer)
{
    const char *cur = "C:\\";
    if (nBufferLength < 4) return 4;
    strcpy(lpBuffer, cur);
    return 3;
}

EXTERN_C uint32_t SetCurrentDirectoryA_c(const char *lpPathName)
{
    LOG_ONCE("SetCurrentDirectoryA(%s): ignored\n", lpPathName ? lpPathName : "");
    return 1;
}

EXTERN_C uint32_t GetDriveTypeA_c(const char *lpRootPathName)
{
    if (lpRootPathName == NULL) return 3;
    if ((lpRootPathName[0] == 'C' || lpRootPathName[0] == 'c') && lpRootPathName[1] == ':') return 3; /* DRIVE_FIXED */
    return 1;   /* DRIVE_NO_ROOT_DIR */
}

EXTERN_C uint32_t SetFileAttributesA_c(const char *lpFileName, uint32_t dwFileAttributes)
{
    return 1;
}

EXTERN_C uint32_t FlushFileBuffers_c(void *hFile)
{
    handle h = (handle)hFile;
    if (h && h != INVALID_HANDLE32_VALUE && h->handle_type == HT_FILE) fflush((FILE *)h->fh.f);
    return 1;
}

EXTERN_C uint32_t SetEndOfFile_c(void *hFile)
{
    handle h = (handle)hFile;
    FILE *f;
    if (h == NULL || h == INVALID_HANDLE32_VALUE || h->handle_type != HT_FILE) return 0;
    f = (FILE *)h->fh.f;
    fflush(f);
    return (0 == ftruncate(fileno(f), ftello(f))) ? 1 : 0;
}

EXTERN_C uint32_t CopyFileA_c(const char *lpExistingFileName, const char *lpNewFileName, uint32_t bFailIfExists)
{
    char src[4096], dst[4096], buf[65536];
    FILE *in, *out;
    size_t n;

    if (!CLIB_FindFile(lpExistingFileName, src))
    {
        Winapi_SetLastError(ERROR_FILE_NOT_FOUND);
        return 0;
    }
    if (CLIB_FindFile(lpNewFileName, dst) && bFailIfExists)
    {
        Winapi_SetLastError(ERROR_FILE_EXISTS);
        return 0;
    }
    in = fopen(src, "rb");
    if (in == NULL) return 0;
    out = fopen(dst, "wb");
    if (out == NULL) { fclose(in); return 0; }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return 1;
}

EXTERN_C void *CreateFileW_c(const void *lpFileName, uint32_t a, uint32_t b, void *c, uint32_t d, uint32_t e, void *f)
{
    Winapi_SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return INVALID_HANDLE32_VALUE;
}

/* ------------------------------------------------------------------ time */

static void unix_to_filetime(struct timeval *tv, void *ft)
{
    uint64_t t = ((uint64_t)tv->tv_sec + 11644473600ull) * 10000000ull + (uint64_t)tv->tv_usec * 10;
    wr32(ft, (uint32_t)t);
    wr32((uint8_t *)ft + 4, (uint32_t)(t >> 32));
}

EXTERN_C void GetSystemTimeAsFileTime_c(void *lpSystemTimeAsFileTime)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    unix_to_filetime(&tv, lpSystemTimeAsFileTime);
}

EXTERN_C uint32_t GetTickCount_c(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

EXTERN_C uint32_t GetTimeZoneInformation_c(void *tzi)
{
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    memset(tzi, 0, 172);
    wr32(tzi, (uint32_t)(int32_t)(-lt.tm_gmtoff / 60));   /* Bias in minutes */
    return 0;   /* TIME_ZONE_ID_UNKNOWN: no daylight rules */
}

static void get_local_systemtime(const void *lpDate, struct tm *out)
{
    if (lpDate)
    {
        const uint16_t *st = (const uint16_t *)lpDate;   /* SYSTEMTIME */
        memset(out, 0, sizeof(*out));
        out->tm_year = st[0] - 1900; out->tm_mon = st[1] - 1; out->tm_wday = st[2];
        out->tm_mday = st[3]; out->tm_hour = st[4]; out->tm_min = st[5]; out->tm_sec = st[6];
    }
    else
    {
        time_t now = time(NULL);
        localtime_r(&now, out);
    }
}

EXTERN_C uint32_t GetDateFormatA_c(uint32_t Locale, uint32_t dwFlags, const void *lpDate, const char *lpFormat, char *lpDateStr, uint32_t cchDate)
{
    struct tm t;
    char buf[64];
    get_local_systemtime(lpDate, &t);
    strftime(buf, sizeof(buf), (dwFlags & 2) ? "%A, %B %d, %Y" : "%m/%d/%Y", &t);   /* DATE_LONGDATE */
    if (cchDate == 0) return (uint32_t)strlen(buf) + 1;
    snprintf(lpDateStr, cchDate, "%s", buf);
    return (uint32_t)strlen(lpDateStr) + 1;
}

EXTERN_C uint32_t GetTimeFormatA_c(uint32_t Locale, uint32_t dwFlags, const void *lpTime, const char *lpFormat, char *lpTimeStr, uint32_t cchTime)
{
    struct tm t;
    char buf[64];
    get_local_systemtime(lpTime, &t);
    strftime(buf, sizeof(buf), "%H:%M:%S", &t);
    if (cchTime == 0) return (uint32_t)strlen(buf) + 1;
    snprintf(lpTimeStr, cchTime, "%s", buf);
    return (uint32_t)strlen(lpTimeStr) + 1;
}

/* ------------------------------------------------------------ locale/NLS */
/* Code page 1252 is treated as Latin-1. The C runtime calls the "W"
   functions first; when they fail with ERROR_CALL_NOT_IMPLEMENTED it uses
   the "A" functions, as on Windows 95. */

EXTERN_C uint32_t GetACP_c(void) { return 1252; }
EXTERN_C uint32_t GetOEMCP_c(void) { return 437; }
EXTERN_C uint32_t IsValidCodePage_c(uint32_t CodePage) { return 1; }
EXTERN_C uint32_t IsValidLocale_c(uint32_t Locale, uint32_t dwFlags) { return 1; }
EXTERN_C uint32_t EnumSystemLocalesA_c(uint32_t proc, uint32_t dwFlags) { return 1; }
EXTERN_C uint32_t GetSystemDefaultLangID_c(void) { return 0x0409; }
EXTERN_C uint32_t GetUserDefaultLCID_c(void) { return 0x0409; }

EXTERN_C uint32_t GetCPInfo_c(uint32_t CodePage, void *lpCPInfo)
{
    uint8_t *c = (uint8_t *)lpCPInfo;
    memset(c, 0, 20);
    wr32(c, 1);       /* MaxCharSize */
    c[4] = '?';       /* DefaultChar */
    return 1;
}

EXTERN_C uint32_t MultiByteToWideChar_c(uint32_t CodePage, uint32_t dwFlags, const char *src, uint32_t cb, void *dst, uint32_t cch)
{
    int32_t n = (int32_t)cb;
    if (n == -1) n = (int32_t)strlen(src) + 1;
    if (cch == 0) return (uint32_t)n;
    if ((uint32_t)n > cch)
    {
        Winapi_SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (int32_t i = 0; i < n; i++) wr16((uint8_t *)dst + 2 * i, (uint8_t)src[i]);
    return (uint32_t)n;
}

EXTERN_C uint32_t WideCharToMultiByte_c(uint32_t CodePage, uint32_t dwFlags, const void *src, uint32_t cch, char *dst, uint32_t cb, void *lpDefaultChar, void *lpUsedDefaultChar)
{
    const uint8_t *s = (const uint8_t *)src;
    int32_t n = (int32_t)cch;
    if (n == -1)
    {
        n = 0;
        while (s[2 * n] | s[2 * n + 1]) n++;
        n++;
    }
    if (lpUsedDefaultChar) wr32(lpUsedDefaultChar, 0);
    if (cb == 0) return (uint32_t)n;
    if ((uint32_t)n > cb)
    {
        Winapi_SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (int32_t i = 0; i < n; i++)
    {
        uint16_t w = s[2 * i] | (s[2 * i + 1] << 8);
        dst[i] = (w < 256) ? (char)w : '?';
    }
    return (uint32_t)n;
}

#define LCMAP_LOWERCASE 0x100
#define LCMAP_UPPERCASE 0x200

EXTERN_C uint32_t LCMapStringA_c(uint32_t Locale, uint32_t dwMapFlags, const char *src, uint32_t cchSrc, char *dst, uint32_t cchDest)
{
    int32_t n = (int32_t)cchSrc;
    if (n == -1) n = (int32_t)strlen(src) + 1;
    if (cchDest == 0) return (uint32_t)n;
    if ((uint32_t)n > cchDest)
    {
        Winapi_SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    for (int32_t i = 0; i < n; i++)
    {
        unsigned char c = (unsigned char)src[i];
        if (dwMapFlags & LCMAP_UPPERCASE) c = (unsigned char)toupper(c);
        else if (dwMapFlags & LCMAP_LOWERCASE) c = (unsigned char)tolower(c);
        dst[i] = (char)c;
    }
    return (uint32_t)n;
}

EXTERN_C uint32_t LCMapStringW_c(uint32_t a, uint32_t b, const void *c, uint32_t d, void *e, uint32_t f)
{
    Winapi_SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}

EXTERN_C uint32_t GetStringTypeA_c(uint32_t Locale, uint32_t dwInfoType, const char *src, uint32_t cchSrc, void *lpCharType)
{
    int32_t n = (int32_t)cchSrc;
    if (n == -1) n = (int32_t)strlen(src) + 1;
    for (int32_t i = 0; i < n; i++)
    {
        unsigned char c = (unsigned char)src[i];
        uint16_t t = 0;
        if (dwInfoType == 1)   /* CT_CTYPE1 */
        {
            if (isupper(c)) t |= 0x001;
            if (islower(c)) t |= 0x002;
            if (isdigit(c)) t |= 0x004;
            if (isspace(c)) t |= 0x008;
            if (ispunct(c)) t |= 0x010;
            if (iscntrl(c)) t |= 0x020;
            if (c == ' ' || c == '\t') t |= 0x040;
            if (isxdigit(c)) t |= 0x080;
            if (isalpha(c)) t |= 0x100;
        }
        wr16((uint8_t *)lpCharType + 2 * i, t);
    }
    return 1;
}

EXTERN_C uint32_t GetStringTypeW_c(uint32_t a, const void *b, uint32_t c, void *d)
{
    Winapi_SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}

EXTERN_C uint32_t CompareStringA_c(uint32_t Locale, uint32_t dwCmpFlags, const char *s1, uint32_t c1, const char *s2, uint32_t c2)
{
    int32_t n1 = (int32_t)c1, n2 = (int32_t)c2, r;
    if (n1 == -1) n1 = (int32_t)strlen(s1);
    if (n2 == -1) n2 = (int32_t)strlen(s2);
    r = (dwCmpFlags & 1) ? strncasecmp(s1, s2, (n1 < n2) ? n1 : n2) : strncmp(s1, s2, (n1 < n2) ? n1 : n2);
    if (r == 0) r = n1 - n2;
    return (r < 0) ? 1 : ((r == 0) ? 2 : 3);
}

EXTERN_C uint32_t CompareStringW_c(uint32_t a, uint32_t b, const void *c, uint32_t d, const void *e, uint32_t f)
{
    Winapi_SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}

EXTERN_C uint32_t GetLocaleInfoA_c(uint32_t Locale, uint32_t LCType, char *lpLCData, uint32_t cchData)
{
    const char *value;
    switch (LCType & 0xffff)
    {
        case 0x1004: value = "1252"; break;  /* LOCALE_IDEFAULTANSICODEPAGE */
        case 0x000b: value = "437"; break;   /* LOCALE_IDEFAULTCODEPAGE */
        case 0x000e: value = "."; break;     /* LOCALE_SDECIMAL */
        case 0x000f: value = ","; break;     /* LOCALE_STHOUSAND */
        case 0x0010: value = "3;0"; break;   /* LOCALE_SGROUPING */
        case 0x0001: value = "0409"; break;  /* LOCALE_ILANGUAGE */
        case 0x0003: value = "ENU"; break;   /* LOCALE_SABBREVLANGNAME */
        case 0x0007: value = "USA"; break;   /* LOCALE_SABBREVCTRYNAME */
        case 0x1001: value = "English"; break;
        case 0x1002: value = "United States"; break;
        default:
            LOG_ONCE("GetLocaleInfoA: LCType 0x%x not known\n", LCType);
            value = "";
            break;
    }
    if (cchData == 0) return (uint32_t)strlen(value) + 1;
    snprintf(lpLCData, cchData, "%s", value);
    return (uint32_t)strlen(lpLCData) + 1;
}

EXTERN_C uint32_t GetLocaleInfoW_c(uint32_t a, uint32_t b, void *c, uint32_t d)
{
    Winapi_SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}

/* ------------------------------------------------------------- lstr* */

EXTERN_C void *lstrcatA_c(char *s1, const char *s2) { return strcat(s1, s2); }
EXTERN_C void *lstrcpyA_c(char *s1, const char *s2) { return strcpy(s1, s2); }
EXTERN_C uint32_t lstrcmpA_c(const char *s1, const char *s2) { return (uint32_t)strcmp(s1 ? s1 : "", s2 ? s2 : ""); }
EXTERN_C uint32_t lstrcmpiA_c(const char *s1, const char *s2) { return (uint32_t)strcasecmp(s1 ? s1 : "", s2 ? s2 : ""); }
EXTERN_C uint32_t lstrlenA_c(const char *s) { return s ? (uint32_t)strlen(s) : 0; }

EXTERN_C void *lstrcpynA_c(char *s1, const char *s2, uint32_t iMaxLength)
{
    if (iMaxLength == 0) return s1;
    strncpy(s1, s2, iMaxLength - 1);
    s1[iMaxLength - 1] = 0;
    return s1;
}

/* ---------------------------------------------------------------- SEH */
/*
 * Structured exception handling for the translated code. The SEH chain is
 * the list at fs:[0] (current_SEH_frame); each record is {next, handler}.
 * C++ "throw" calls RaiseException; the MSVC frame handler finds the catch
 * block, calls RtlUnwind, runs the catch block and jumps to the code after
 * it. That jump never comes back here, so the native frames of this call
 * stay on the host stack (a small, bounded leak per exception).
 */

extern uint32_t current_SEH_frame;
EXTERN_C uint32_t CCALL CallX86Function(uint32_t address, int nargs, const uint32_t *args);

#define EXCEPTION_NONCONTINUABLE 0x01
#define EXCEPTION_UNWINDING      0x02
#define EXCEPTION_EXIT_UNWIND    0x04

static uint32_t unhandled_filter;

static uint8_t *alloc_exception_record(uint32_t code, uint32_t flags, uint32_t nargs, const void *args)
{
    uint8_t *rec = (uint8_t *)x86_calloc(1, 80 + 716 + 64);
    wr32(rec + 0, code);
    wr32(rec + 4, flags);
    if (nargs > 15) nargs = 15;
    wr32(rec + 16, nargs);
    if (args) memcpy(rec + 20, args, 4 * nargs);
    return rec;
}

EXTERN_C void RaiseException_c(uint32_t dwExceptionCode, uint32_t dwExceptionFlags, uint32_t nNumberOfArguments, void *lpArguments)
{
    uint8_t *rec = alloc_exception_record(dwExceptionCode, dwExceptionFlags & EXCEPTION_NONCONTINUABLE, nNumberOfArguments, lpArguments);
    uint8_t *context = rec + 80;
    uint8_t *dispatcher = rec + 80 + 716;
    uint32_t frame = current_SEH_frame;

    while (frame != 0xffffffff && frame != 0)
    {
        uint32_t *record = (uint32_t *)from_guest(frame);
        uint32_t args[4] = { to_guest(rec), frame, to_guest(context), to_guest(dispatcher) };
        uint32_t result = CallX86Function(record[1], 4, args);
        if (result == 0)   /* ExceptionContinueExecution */
        {
            return;
        }
        frame = record[0];   /* ExceptionContinueSearch */
    }

    if (unhandled_filter)
    {
        uint32_t ptrs[2] = { to_guest(rec), to_guest(context) };
        uint32_t *ep = (uint32_t *)x86_malloc(8);
        memcpy(ep, ptrs, 8);
        uint32_t arg = to_guest(ep);
        CallX86Function(unhandled_filter, 1, &arg);
    }
    fprintf(stderr, "Fatal: unhandled exception 0x%08x\n", dwExceptionCode);
    exit(4);
}

EXTERN_C void RtlUnwind_c(uint32_t TargetFrame, uint32_t TargetIp, void *ExceptionRecord, uint32_t ReturnValue)
{
    uint8_t *rec;
    uint32_t frame;

    if (ExceptionRecord == NULL)
    {
        rec = alloc_exception_record(0xc0000027, 0, 0, NULL);   /* STATUS_UNWIND */
    }
    else
    {
        rec = (uint8_t *)ExceptionRecord;
    }
    wr32(rec + 4, rd32(rec + 4) | EXCEPTION_UNWINDING | (TargetFrame == 0 ? EXCEPTION_EXIT_UNWIND : 0));

    frame = current_SEH_frame;
    while (frame != 0xffffffff && frame != 0 && frame != TargetFrame)
    {
        uint32_t *record = (uint32_t *)from_guest(frame);
        uint32_t next = record[0];
        uint32_t args[4] = { to_guest(rec), frame, 0, 0 };
        CallX86Function(record[1], 4, args);
        frame = next;
        current_SEH_frame = next;
    }
}

EXTERN_C uint32_t SetUnhandledExceptionFilter_c(uint32_t lpTopLevelExceptionFilter)
{
    uint32_t old = unhandled_filter;
    unhandled_filter = lpTopLevelExceptionFilter;
    return old;
}

EXTERN_C uint32_t UnhandledExceptionFilter_c(void *ExceptionInfo)
{
    fprintf(stderr, "UnhandledExceptionFilter called\n");
    return 1;   /* EXCEPTION_EXECUTE_HANDLER */
}

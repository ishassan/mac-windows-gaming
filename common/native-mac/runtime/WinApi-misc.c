/*
 *  Port change: GDI32 extras, ADVAPI32 (registry), SHELL32, SHLWAPI,
 *  ole32 and WSOCK32 functions.
 *  MIT license, see README.md.
 */

#define _FILE_OFFSET_BITS 64
#include "game-info.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include "WinApi.h"
#include "Game-Memory.h"
#include "CLIB.h"
#include "guest.h"

#ifndef EXTERN_C
#define EXTERN_C extern "C"
#endif

/* ------------------------------------------------------------------ GDI32 */

EXTERN_C uint32_t GetDeviceCaps_c(void *hdc, int32_t index)
{
    switch (index)
    {
        case 8:   return 1024;   /* HORZRES */
        case 10:  return 768;    /* VERTRES */
        case 12:  return 32;     /* BITSPIXEL */
        case 14:  return 1;      /* PLANES */
        case 24:  return 0xffffffff; /* NUMCOLORS: more than 256 */
        case 88:  return 96;     /* LOGPIXELSX */
        case 90:  return 96;     /* LOGPIXELSY */
        case 116: return 60;     /* VREFRESH */
        default:
            LOG_ONCE("GetDeviceCaps: index %d not known\n", index);
            return 0;
    }
}




EXTERN_C uint32_t GetPixel_c(void *hdc, int32_t x, int32_t y) { return 0; }
EXTERN_C uint32_t SetPixel_c(void *hdc, int32_t x, int32_t y, uint32_t color) { return color; }

/* ADVAPI32 (registry): see WinApi-registry.c */

/* --------------------------------------------------------------- SHELL32 */
/* "My Documents" is a folder inside the game folder (drive C:):
 * GAME_DOCUMENTS_PATH in the game's game.h (default "C:\\User"). The
 * environment variable <P>DOCUMENTS=<host folder> gives another folder (for
 * tests on a copy of the saves); it becomes a path relative to drive C:. */

#ifndef GAME_DOCUMENTS_PATH
#define GAME_DOCUMENTS_PATH "C:\\User"
#endif

static void documents_path(char *out)
{
    const char *env = game_getenv("DOCUMENTS");
    char cwd[4096];
    if (env != NULL && env[0] == '/' && getcwd(cwd, sizeof(cwd)) != NULL)
    {
        /* relative path from the game folder to env */
        std::string a(cwd), b(env);
        while (b.size() > 1 && b.back() == '/') b.pop_back();
        std::vector<std::string> pa, pb;
        auto split = [](const std::string &s, std::vector<std::string> &v) {
            size_t i = 0;
            while (i < s.size())
            {
                size_t j = s.find('/', i);
                if (j == std::string::npos) j = s.size();
                if (j > i) v.push_back(s.substr(i, j - i));
                i = j + 1;
            }
        };
        split(a, pa);
        split(b, pb);
        size_t k = 0;
        while (k < pa.size() && k < pb.size() && pa[k] == pb[k]) k++;
        std::string r = "C:";
        for (size_t i = k; i < pa.size(); i++) r += "\\..";
        for (size_t i = k; i < pb.size(); i++) r += "\\" + pb[i];
        if (r == "C:") r = "C:\\";
        strcpy(out, r.c_str());
        return;
    }
    strcpy(out, GAME_DOCUMENTS_PATH);
}

/* The host folder of the documents (saves and settings), for runtime parts
   that write files there (the DXVK shader cache). Empty if not found. */
EXTERN_C void Winapi_DocumentsHostPath(char *out, size_t size)
{
    char guest[4096], host[4096];
    documents_path(guest);
    out[0] = 0;
    CLIB_FindFile(guest, host);
    if (realpath(host, guest) != NULL) snprintf(out, size, "%s", guest);
}

static int make_dirs(const char *winpath);

EXTERN_C uint32_t SHGetFolderPathA_c(void *hwnd, uint32_t csidl, void *hToken, uint32_t dwFlags, char *pszPath)
{
    documents_path(pszPath);
    if (csidl & 0x8000)   /* CSIDL_FLAG_CREATE */
    {
        make_dirs(pszPath);
    }
    return 0;   /* S_OK */
}

/* CSIDL_PERSONAL (5) and the other folders: all are "My Documents" */
EXTERN_C uint32_t SHGetSpecialFolderPathA_c(void *hwnd, char *pszPath, uint32_t csidl, uint32_t fCreate)
{
    documents_path(pszPath);
    if (fCreate) make_dirs(pszPath);
    return 1;
}

/* An item ID list: here a guest block that holds the folder id */
EXTERN_C uint32_t SHGetSpecialFolderLocation_c(void *hwnd, uint32_t nFolder, void *ppidl)
{
    uint8_t *pidl = (uint8_t *)x86_calloc(1, 8);
    wr32(pidl, 0x4c444950);   /* "PIDL" */
    wr32(pidl + 4, nFolder);
    wr32(ppidl, to_guest(pidl));
    return 0;
}

EXTERN_C uint32_t SHGetPathFromIDListA_c(void *pidl, char *pszPath)
{
    if (pidl == NULL || rd32(pidl) != 0x4c444950) return 0;
    documents_path(pszPath);
    return 1;
}

static int make_dirs(const char *winpath)
{
    char path[4096];
    char *p;

    CLIB_FindFile(winpath, path);
    for (p = path + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = 0;
            mkdir(path, 0755);
            *p = '/';
        }
    }
    return mkdir(path, 0755);
}

EXTERN_C uint32_t SHCreateDirectoryExA_c(void *hwnd, const char *pszPath, void *psa)
{
    char path[4096];
    if (CLIB_FindFile(pszPath, path)) return 183;   /* ERROR_ALREADY_EXISTS */
    return (make_dirs(pszPath) == 0) ? 0 : 3;       /* ERROR_PATH_NOT_FOUND */
}

EXTERN_C void DragAcceptFiles_c(void *hWnd, uint32_t fAccept) { }
EXTERN_C void DragFinish_c(uint32_t hDrop) { }
EXTERN_C uint32_t DragQueryFileA_c(uint32_t hDrop, uint32_t iFile, char *lpszFile, uint32_t cch) { return 0; }

/* --------------------------------------------------------------- SHLWAPI */

EXTERN_C uint32_t PathFileExistsA_c(const char *pszPath)
{
    char path[4096];
    return pszPath ? (uint32_t)CLIB_FindFile(pszPath, path) : 0;
}

EXTERN_C uint32_t PathAppendA_c(char *pszPath, const char *pszMore)
{
    size_t n = strlen(pszPath);
    while (*pszMore == '\\') pszMore++;
    if (n > 0 && pszPath[n - 1] != '\\') strcat(pszPath, "\\");
    strcat(pszPath, pszMore);
    return 1;
}

/* ----------------------------------------------------------------- ole32 */

EXTERN_C uint32_t CoInitialize_c(void *pvReserved) { return 0; }
EXTERN_C void CoUninitialize_c(void) { }

EXTERN_C uint32_t AMStream_Create(uint32_t *ppv);

EXTERN_C int DPlay_CoCreateInstance(const uint8_t *rclsid, uint32_t *result, void *ppv);

EXTERN_C uint32_t CoCreateInstance_c(void *rclsid, void *pUnkOuter, uint32_t dwClsContext, void *riid, void *ppv)
{
    /* CLSID_AMMultiMediaStream {49C47CE5-9BA4-11D0-8212-00C04FC32C45}:
     * the video player (WinApi-amstream.c) */
    if (rclsid && ppv && rd32(rclsid) == 0x49c47ce5u && rd32((uint8_t *)rclsid + 4) == 0x11d09ba4u)
    {
        return AMStream_Create((uint32_t *)ppv);
    }
    {
        uint32_t result;
        if (rclsid && ppv && DPlay_CoCreateInstance((const uint8_t *)rclsid, &result, ppv)) return result;
    }
    if (ppv) wr32(ppv, 0);
    LOG_ONCE("CoCreateInstance: class %08x is not available\n", rclsid ? rd32(rclsid) : 0);
    return 0x80040154;   /* REGDB_E_CLASSNOTREG */
}

/* --------------------------------------------------------------- WSOCK32 */
/* Multiplayer is not supported in this version: network calls fail. */

#define WSAENETDOWN 10050

/* Port change: the network is off, so WSAStartup fails. A game whose
   game.h defines GAME_WSA_STARTUP_OK gets success (Generals Zero Hour asks
   for the host name through it, for the player name in skirmish); its
   sockets still fail. */
EXTERN_C uint32_t ws_WSAStartup_c(uint32_t wVersionRequested, void *lpWSAData)
{
#ifdef GAME_WSA_STARTUP_OK
    if (lpWSAData)
    {
        memset(lpWSAData, 0, 400);                  /* WSADATA (32-bit) */
        wr16(lpWSAData, 0x0202);                    /* wVersion */
        wr16((uint8_t *)lpWSAData + 2, 0x0202);     /* wHighVersion */
    }
    return 0;
#else
    return WSAENETDOWN;
#endif
}
EXTERN_C uint32_t ws_WSACleanup_c(void) { return 0; }
EXTERN_C uint32_t ws_socket_c(uint32_t af, uint32_t type, uint32_t protocol) { return 0xffffffff; }
EXTERN_C uint32_t ws_closesocket_c(uint32_t s) { return 0; }
EXTERN_C uint32_t ws_accept_c(uint32_t s, void *addr, void *addrlen) { return 0xffffffff; }
EXTERN_C uint32_t ws_bind_c(uint32_t s, void *name, uint32_t namelen) { return 0xffffffff; }
EXTERN_C uint32_t ws_connect_c(uint32_t s, void *name, uint32_t namelen) { return 0xffffffff; }
EXTERN_C uint32_t ws_listen_c(uint32_t s, uint32_t backlog) { return 0xffffffff; }
EXTERN_C uint32_t ws_recv_c(uint32_t s, void *buf, uint32_t len, uint32_t flags) { return 0xffffffff; }
EXTERN_C uint32_t ws_send_c(uint32_t s, void *buf, uint32_t len, uint32_t flags) { return 0xffffffff; }
EXTERN_C uint32_t ws_select_c(uint32_t nfds, void *r, void *w, void *e, void *t) { return 0xffffffff; }
EXTERN_C uint32_t ws_setsockopt_c(uint32_t s, uint32_t level, uint32_t optname, void *optval, uint32_t optlen) { return 0xffffffff; }
EXTERN_C uint32_t ws_ioctlsocket_c(uint32_t s, uint32_t cmd, void *argp) { return 0xffffffff; }
EXTERN_C void *ws_gethostbyname_c(const char *name) { return NULL; }
EXTERN_C uint32_t ws_gethostname_c(char *name, uint32_t namelen)
{
    /* the game shows it as the player name (Windows: the computer name) */
    if (name && namelen > 0) snprintf(name, namelen, "Player");
    return 0;
}
EXTERN_C uint32_t ws_htons_c(uint32_t v) { return ((v & 0xff) << 8) | ((v >> 8) & 0xff); }
EXTERN_C uint32_t ws_inet_addr_c(const char *cp) { return 0xffffffff; }
EXTERN_C void *ws_inet_ntoa_c(uint32_t in)
{
    static char *buf;
    if (buf == NULL) buf = (char *)x86_malloc(16);
    snprintf(buf, 16, "%u.%u.%u.%u", in & 0xff, (in >> 8) & 0xff, (in >> 16) & 0xff, in >> 24);
    return buf;
}

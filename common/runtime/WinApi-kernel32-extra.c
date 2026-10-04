/*
 *  Native port: more KERNEL32 functions (files, time, INI files, system
 *  information). First needed by Revenant.
 *  MIT license, see the README.md of the repository.
 */

#define _FILE_OFFSET_BITS 64
#include "game-info.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include "WinApi.h"
#include "Game-Memory.h"
#include "CLIB.h"
#include "guest.h"

#define FILE_ATTRIBUTE_READONLY  0x01
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_ARCHIVE   0x20
#define FILE_ATTRIBUTE_NORMAL    0x80
#define INVALID_FILE_ATTRIBUTES  0xffffffffu
#define ERROR_FILE_NOT_FOUND_    2
#define ERROR_INVALID_HANDLE_    6

/* 100 ns ticks between 1601-01-01 and 1970-01-01 */
#define FILETIME_UNIX_EPOCH 116444736000000000ULL

static void write_filetime(void *p, uint64_t ft)
{
    wr32(p, (uint32_t)ft);
    wr32((uint8_t *)p + 4, (uint32_t)(ft >> 32));
}

static uint64_t read_filetime(const void *p)
{
    return rd32(p) | ((uint64_t)rd32((const uint8_t *)p + 4) << 32);
}

static uint64_t timespec_to_filetime(const struct timespec *ts)
{
    return FILETIME_UNIX_EPOCH + (uint64_t)ts->tv_sec * 10000000ULL + (uint64_t)ts->tv_nsec / 100;
}

/* SYSTEMTIME: 8 WORDs (year, month, day of week, day, hour, minute, second, ms) */
static void write_systemtime(void *p, const struct tm *t, int ms)
{
    uint8_t *s = (uint8_t *)p;
    wr16(s + 0, (uint16_t)(t->tm_year + 1900));
    wr16(s + 2, (uint16_t)(t->tm_mon + 1));
    wr16(s + 4, (uint16_t)t->tm_wday);
    wr16(s + 6, (uint16_t)t->tm_mday);
    wr16(s + 8, (uint16_t)t->tm_hour);
    wr16(s + 10, (uint16_t)t->tm_min);
    wr16(s + 12, (uint16_t)t->tm_sec);
    wr16(s + 14, (uint16_t)ms);
}

static uint16_t rd16(const void *p) { uint16_t v; memcpy(&v, p, 2); return v; }

EXTERN_C void GetLocalTime_c(void *lpSystemTime)
{
    struct timeval tv;
    struct tm t;
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &t);
    write_systemtime(lpSystemTime, &t, (int)(tv.tv_usec / 1000));
}

EXTERN_C void GetSystemTime_c(void *lpSystemTime)
{
    struct timeval tv;
    struct tm t;
    gettimeofday(&tv, NULL);
    gmtime_r(&tv.tv_sec, &t);
    write_systemtime(lpSystemTime, &t, (int)(tv.tv_usec / 1000));
}

EXTERN_C uint32_t SystemTimeToFileTime_c(const void *lpSystemTime, void *lpFileTime)
{
    const uint8_t *s = (const uint8_t *)lpSystemTime;
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = rd16(s + 0) - 1900;
    t.tm_mon = rd16(s + 2) - 1;
    t.tm_mday = rd16(s + 6);
    t.tm_hour = rd16(s + 8);
    t.tm_min = rd16(s + 10);
    t.tm_sec = rd16(s + 12);
    time_t secs = timegm(&t);
    write_filetime(lpFileTime, FILETIME_UNIX_EPOCH + (uint64_t)secs * 10000000ULL + (uint64_t)rd16(s + 14) * 10000ULL);
    return 1;
}

EXTERN_C uint32_t FileTimeToDosDateTime_c(const void *lpFileTime, void *lpFatDate, void *lpFatTime)
{
    uint64_t ft = read_filetime(lpFileTime);
    time_t secs = (time_t)((ft - FILETIME_UNIX_EPOCH) / 10000000ULL);
    struct tm t;
    gmtime_r(&secs, &t);
    if (t.tm_year < 80) return 0;
    if (lpFatDate) wr16(lpFatDate, (uint16_t)(((t.tm_year - 80) << 9) | ((t.tm_mon + 1) << 5) | t.tm_mday));
    if (lpFatTime) wr16(lpFatTime, (uint16_t)((t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec / 2)));
    return 1;
}

static uint32_t attributes_of(const struct stat *st)
{
    uint32_t a = S_ISDIR(st->st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    if (!(st->st_mode & S_IWUSR)) a |= FILE_ATTRIBUTE_READONLY;
    return a;
}

EXTERN_C uint32_t GetFileAttributesA_c(const char *lpFileName)
{
    char path[4096];
    struct stat st;
    if (lpFileName == NULL || !CLIB_FindFile(lpFileName, path) || stat(path, &st) != 0)
    {
        Winapi_SetLastError(ERROR_FILE_NOT_FOUND_);
        return INVALID_FILE_ATTRIBUTES;
    }
    return attributes_of(&st);
}

EXTERN_C uint32_t GetFileInformationByHandle_c(void *hFile, void *lpFileInformation)
{
    handle h = (handle)hFile;
    struct stat st;
    uint8_t *p = (uint8_t *)lpFileInformation;
    if (h == NULL || h->handle_type != HT_FILE || h->fh.f == NULL || fstat(fileno((FILE *)h->fh.f), &st) != 0)
    {
        Winapi_SetLastError(ERROR_INVALID_HANDLE_);
        return 0;
    }
    memset(p, 0, 52);
    wr32(p + 0, attributes_of(&st));
    write_filetime(p + 4, timespec_to_filetime(&st.st_birthtimespec));
    write_filetime(p + 12, timespec_to_filetime(&st.st_atimespec));
    write_filetime(p + 20, timespec_to_filetime(&st.st_mtimespec));
    wr32(p + 28, 0x12345678);                          /* volume serial number */
    wr32(p + 32, (uint32_t)((uint64_t)st.st_size >> 32));
    wr32(p + 36, (uint32_t)st.st_size);
    wr32(p + 40, (uint32_t)st.st_nlink);
    wr32(p + 44, (uint32_t)((uint64_t)st.st_ino >> 32));
    wr32(p + 48, (uint32_t)st.st_ino);
    return 1;
}

EXTERN_C uint32_t MoveFileA_c(const char *lpExistingFileName, const char *lpNewFileName)
{
    char from[4096], to[4096];
    if (!CLIB_FindFile(lpExistingFileName, from))
    {
        Winapi_SetLastError(ERROR_FILE_NOT_FOUND_);
        return 0;
    }
    if (CLIB_FindFile(lpNewFileName, to))
    {
        Winapi_SetLastError(ERROR_ALREADY_EXISTS);   /* MoveFile does not replace */
        return 0;
    }
    return rename(from, to) == 0 ? 1 : 0;
}

/* Only drive C: exists: it is the game folder. */
EXTERN_C uint32_t GetLogicalDrives_c(void)
{
    return 1u << 2;
}

EXTERN_C uint32_t GetVolumeInformationA_c(const char *lpRootPathName, char *lpVolumeNameBuffer, uint32_t nVolumeNameSize,
                                          void *lpVolumeSerialNumber, void *lpMaximumComponentLength, void *lpFileSystemFlags,
                                          char *lpFileSystemNameBuffer, uint32_t nFileSystemNameSize)
{
    if (lpRootPathName != NULL && toupper((unsigned char)lpRootPathName[0]) != 'C')
    {
        Winapi_SetLastError(21);   /* ERROR_NOT_READY */
        return 0;
    }
    if (lpVolumeNameBuffer && nVolumeNameSize) snprintf(lpVolumeNameBuffer, nVolumeNameSize, "%s", "GAME");
    if (lpVolumeSerialNumber) wr32(lpVolumeSerialNumber, 0x12345678);
    if (lpMaximumComponentLength) wr32(lpMaximumComponentLength, 255);
    if (lpFileSystemFlags) wr32(lpFileSystemFlags, 0x3);
    if (lpFileSystemNameBuffer && nFileSystemNameSize) snprintf(lpFileSystemNameBuffer, nFileSystemNameSize, "%s", "NTFS");
    return 1;
}

/* Windows XP 5.1.2600, the same as GetVersionExA (WinApi-kernel32-crt.c):
 * low byte = major, next byte = minor, high word = build (bit 31 clear = NT). */
EXTERN_C uint32_t GetVersion_c(void)
{
    return (2600u << 16) | (1u << 8) | 5u;
}

/* GlobalAlloc: GMEM_FIXED memory is a pointer; GMEM_MOVEABLE is used the same
 * way (GlobalLock is not imported by the games that need this). */
EXTERN_C void *GlobalAlloc_c(uint32_t uFlags, uint32_t dwBytes)
{
    void *p = x86_malloc(dwBytes ? dwBytes : 1);
    if (p && (uFlags & 0x40)) memset(p, 0, dwBytes);   /* GMEM_ZEROINIT */
    return p;
}

EXTERN_C void *GlobalFree_c(void *hMem)
{
    if (hMem) x86_free(hMem);
    return NULL;
}

/* MEMORYSTATUS (32 bytes): report 256 MB of RAM, all free */
EXTERN_C void GlobalMemoryStatus_c(void *lpBuffer)
{
    uint8_t *p = (uint8_t *)lpBuffer;
    wr32(p + 0, 32);
    wr32(p + 4, 10);                   /* memory load (%) */
    wr32(p + 8, 256u << 20);           /* total physical */
    wr32(p + 12, 230u << 20);          /* available physical */
    wr32(p + 16, 512u << 20);          /* total page file */
    wr32(p + 20, 480u << 20);          /* available page file */
    wr32(p + 24, 2047u << 20);         /* total virtual */
    wr32(p + 28, 1900u << 20);         /* available virtual */
}

EXTERN_C uint32_t IsBadCodePtr_c(uint32_t lpfn) { return lpfn == 0; }
EXTERN_C uint32_t IsBadReadPtr_c(void *lp, uint32_t ucb) { return lp == NULL && ucb != 0; }
EXTERN_C uint32_t IsBadWritePtr_c(void *lp, uint32_t ucb) { return lp == NULL && ucb != 0; }
EXTERN_C uint32_t VirtualLock_c(void *lpAddress, uint32_t dwSize) { return 1; }
EXTERN_C uint32_t VirtualUnlock_c(void *lpAddress, uint32_t dwSize) { return 1; }

/* No pipes to child processes exist. */
EXTERN_C uint32_t PeekNamedPipe_c(void *hNamedPipe, void *lpBuffer, uint32_t nBufferSize, void *lpBytesRead,
                                  void *lpTotalBytesAvail, void *lpBytesLeftThisMessage)
{
    if (lpBytesRead) wr32(lpBytesRead, 0);
    if (lpTotalBytesAvail) wr32(lpTotalBytesAvail, 0);
    if (lpBytesLeftThisMessage) wr32(lpBytesLeftThisMessage, 0);
    Winapi_SetLastError(ERROR_INVALID_HANDLE_);
    return 0;
}

/* ------------------------------------------------------------- INI files */

/* GetPrivateProfileIntA: the value is read with GetPrivateProfileStringA
 * (WinApi-kernel32.c), then converted like Windows does (leading digits). */
extern "C" uint32_t CCALL GetPrivateProfileStringA_c(const char *lpAppName, const char *lpKeyName, const char *lpDefault,
                                                 char *lpReturnedString, uint32_t nSize, const char *lpFileName);

EXTERN_C uint32_t GetPrivateProfileIntA_c(const char *lpAppName, const char *lpKeyName, int32_t nDefault, const char *lpFileName)
{
    char buf[64];
    if (GetPrivateProfileStringA_c(lpAppName, lpKeyName, "", buf, sizeof(buf), lpFileName) == 0)
    {
        return (uint32_t)nDefault;
    }
    return (uint32_t)strtol(buf, NULL, 0);
}

static std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

/* WritePrivateProfileStringA: rewrite the INI file with the key changed,
 * added (lpString != NULL) or removed (lpString == NULL). lpKeyName == NULL
 * removes the whole section. Other lines stay as they are. */
EXTERN_C uint32_t WritePrivateProfileStringA_c(const char *lpAppName, const char *lpKeyName, const char *lpString, const char *lpFileName)
{
    char path[4096];
    std::vector<std::string> lines;
    std::string line;
    FILE *f;
    int c;

    if (lpAppName == NULL || lpFileName == NULL) return 0;
    CLIB_FindFile(lpFileName, path);
    if ((f = fopen(path, "rb")) != NULL)
    {
        while ((c = fgetc(f)) != EOF)
        {
            if (c == '\n') { if (!line.empty() && line.back() == '\r') line.pop_back(); lines.push_back(line); line.clear(); }
            else line += (char)c;
        }
        if (!line.empty()) lines.push_back(line);
        fclose(f);
    }

    /* find the section */
    size_t sec = lines.size(), end = lines.size();
    for (size_t i = 0; i < lines.size(); i++)
    {
        std::string t = trim(lines[i]);
        if (t.size() >= 2 && t[0] == '[')
        {
            if (sec != lines.size()) { end = i; break; }
            if (strcasecmp(t.substr(1, t.find(']') - 1).c_str(), lpAppName) == 0) sec = i;
        }
    }

    if (lpKeyName == NULL)
    {
        if (sec != lines.size()) lines.erase(lines.begin() + sec, lines.begin() + end);
    }
    else
    {
        if (sec == lines.size())
        {
            if (lpString == NULL) return 1;
            lines.push_back(std::string("[") + lpAppName + "]");
            sec = lines.size() - 1;
            end = lines.size();
        }
        size_t key = end;
        for (size_t i = sec + 1; i < end; i++)
        {
            size_t eq = lines[i].find('=');
            if (eq != std::string::npos && strcasecmp(trim(lines[i].substr(0, eq)).c_str(), lpKeyName) == 0) { key = i; break; }
        }
        if (lpString == NULL)
        {
            if (key != end) lines.erase(lines.begin() + key);
        }
        else if (key != end)
        {
            lines[key] = std::string(lpKeyName) + "=" + lpString;
        }
        else
        {
            /* add after the last non-empty line of the section */
            size_t at = end;
            while (at > sec + 1 && trim(lines[at - 1]).empty()) at--;
            lines.insert(lines.begin() + at, std::string(lpKeyName) + "=" + lpString);
        }
    }

    if ((f = fopen(path, "wb")) == NULL) return 0;
    for (const std::string &l : lines) fprintf(f, "%s\r\n", l.c_str());
    fclose(f);
    return 1;
}

/* ------------------------------------------------------------- ADVAPI32 */

EXTERN_C uint32_t GetUserNameA_c(char *lpBuffer, void *pcbBuffer)
{
    const char *name = getenv("USER");
    uint32_t size = rd32(pcbBuffer), need;
    if (name == NULL) name = "Player";
    need = (uint32_t)strlen(name) + 1;
    wr32(pcbBuffer, need);
    if (lpBuffer == NULL || size < need)
    {
        Winapi_SetLastError(122);   /* ERROR_INSUFFICIENT_BUFFER */
        return 0;
    }
    memcpy(lpBuffer, name, need);
    return 1;
}

/* ------------------------------------------------------------- SHELL32 */

/* No tray icon on macOS. */
EXTERN_C uint32_t Shell_NotifyIconA_c(uint32_t dwMessage, void *lpData) { return 1; }

/* Child processes are not supported. */
EXTERN_C uint32_t CreateProcessA_c(const char *lpApplicationName, const char *lpCommandLine, void *a, void *b, uint32_t c,
                                   uint32_t d, void *e, const char *f, void *g, void *h)
{
    LOG_ONCE("CreateProcessA(%s, %s): not supported\n", lpApplicationName ? lpApplicationName : "", lpCommandLine ? lpCommandLine : "");
    Winapi_SetLastError(ERROR_FILE_NOT_FOUND_);
    return 0;
}

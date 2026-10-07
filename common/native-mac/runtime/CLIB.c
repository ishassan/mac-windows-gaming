/**
 *
 *  Copyright (C) 2019-2026 Roman Pauer
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is furnished to do
 *  so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 */

#define _FILE_OFFSET_BITS 64
#define _TIME_BITS 64
#ifdef DEBUG_CLIB
#include <inttypes.h>
#endif
#include <stdlib.h>
#include "CLIB.h"
#include "Game-Memory.h"

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include "printf_x86.h"
#include "ptr32.h"

#if (defined(__WIN32__) || defined(__WINDOWS__)) && !defined(_WIN32)
#define _WIN32
#endif

#ifdef _WIN32
#include <process.h>
#else
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#endif


#define eprintf(...) fprintf(stderr,__VA_ARGS__)


#ifdef __cplusplus
extern "C" {
#endif
extern void CCALL run_thread_asm(void *arglist, void(*start_address)(void *));
#ifdef __cplusplus
}
#endif


void * CCALL memset_c(void *s, int32_t c, uint32_t n)
{
#ifdef DEBUG_CLIB
    eprintf("memset: 0x%" PRIxPTR ", 0x%x, %i\n", (uintptr_t) s, c, n);
#endif

    return memset(s, c, n);
}

void * CCALL memcpy_c(void *dest, const void *src, uint32_t n)
{
#ifdef DEBUG_CLIB
    eprintf("memcpy: 0x%" PRIxPTR ", 0x%" PRIxPTR ", %i\n", (uintptr_t) dest, (uintptr_t) src, n);
#endif

    // either IDA misidentified memmove as memcpy
    // or Septerra Core uses memcpy on overlapping regions
    //return memcpy(dest, src, n);
    return memmove(dest, src, n);
}


int32_t CCALL _stricmp_c(const char *s1, const char *s2)
{
#ifdef DEBUG_CLIB
    eprintf("_stricmp: 0x%" PRIxPTR " (%s), 0x%" PRIxPTR " (%s) - %i\n", (uintptr_t) s1, s1, (uintptr_t) s2, s2, strcasecmp(s1, s2));
#endif

    return strcasecmp(s1, s2);
}

char * CCALL strncpy_c(char *dest, const char *src, uint32_t n)
{
#ifdef DEBUG_CLIB
    eprintf("strncpy: 0x%" PRIxPTR ", 0x%" PRIxPTR " (%s), %i\n", (uintptr_t) dest, (uintptr_t) src, src, n);
#endif

    return strncpy(dest, src, n);
}

int32_t CCALL strncmp_c(const char *s1, const char *s2, uint32_t n)
{
#ifdef DEBUG_CLIB
    eprintf("strncmp: 0x%" PRIxPTR " (%s), 0x%" PRIxPTR " (%s), %i - %i\n", (uintptr_t) s1, s1, (uintptr_t) s2, s2, n, strncmp(s1, s2, n));
#endif

    return strncmp(s1, s2, n);
}

char * CCALL strncat_c(char *dest, const char *src, uint32_t n)
{
#ifdef DEBUG_CLIB
    eprintf("strncat: 0x%" PRIxPTR " (%s), 0x%" PRIxPTR " (%s), %i\n", (uintptr_t) dest, dest, (uintptr_t) src, src, n);
#endif

    return strncat(dest, src, n);
}

void * CCALL malloc_c(uint32_t size)
{
#ifdef DEBUG_CLIB
    eprintf("malloc: %i\n", size);
#endif

    return x86_malloc(size);
}

void CCALL free_c(void *ptr)
{
#ifdef DEBUG_CLIB
    eprintf("free: 0x%" PRIxPTR "\n", (uintptr_t) ptr);
#endif

    x86_free(ptr);
}

void * CCALL calloc_c(uint32_t nmemb, uint32_t size)
{
#ifdef DEBUG_CLIB
    eprintf("calloc: %i, %i\n", nmemb, size);
#endif

    return x86_calloc(nmemb, size);
}


int32_t CCALL atol_c(const char *nptr)
{
#ifdef DEBUG_CLIB
    eprintf("atol: 0x%" PRIxPTR " (%s) - %i\n", (uintptr_t) nptr, nptr, (int)atol(nptr));
#endif

    return atol(nptr);
}

int32_t CCALL toupper_c(int32_t c)
{
#ifdef DEBUG_CLIB
    eprintf("toupper: %i (%c) - %i\n", c, c, toupper(c));
#endif

    return toupper(c);
}


int32_t CCALL sprintf2_c(char *str, const char *format, uint32_t *ap)
{
    int res;

#ifdef DEBUG_CLIB
    eprintf("sprintf: 0x%" PRIxPTR ", 0x%" PRIxPTR " (%s) - ", (uintptr_t) str, (uintptr_t) format, format);
#endif

    res = vsprintf_x86(str, format, ap);

#ifdef DEBUG_CLIB
    eprintf("%i (%s)\n", res, str);
#endif

    return res;
}

/* sscanf2_c: see WinApi-msvcrt.c (all conversions, guest pointers) */


int32_t CCALL system_c(const char *command)
{
#ifdef DEBUG_CLIB
    eprintf("system: 0x%" PRIxPTR " (%s)\n", (uintptr_t) command, command);
#endif

    return 0;
}

void CCALL srand_c(uint32_t seed)
{
#ifdef DEBUG_CLIB
    eprintf("srand: 0x%x\n", seed);
#endif

    srand(seed);
}

int32_t CCALL rand_c(void)
{
#ifdef DEBUG_CLIB
    eprintf("rand\n");
#endif

    // Microsoft implementation returns values in range 0-0x7fff
    return rand() & 0x7fff;
}


void CCALL __report_gsfailure_c(void)
{
    fprintf(stderr, "security cookie check failed\n");
    exit(0x409);
}

int32_t CCALL _except_handler4_c(int32_t _1, void *TargetFrame, int32_t _3)
{
    fprintf(stderr, "exception handler: %i\n", 4);
    exit(0);
}

typedef struct {
    void(*start_address)(void *);
    void *arglist;
} run_thread_args;

#ifdef _WIN32
static void run_thread(void *arg)
{
    void(*start_address)(void *) = ((run_thread_args *)arg)->start_address;
    void *arglist = ((run_thread_args *)arg)->arglist;
    free(arg);

    run_thread_asm(arglist, start_address);
}
#else
static void *run_thread(void *arg)
{
    void(*start_address)(void *) = ((run_thread_args *)arg)->start_address;
    void *arglist = ((run_thread_args *)arg)->arglist;
    free(arg);

    run_thread_asm(arglist, start_address);
    return NULL;
}
#endif

uint32_t CCALL _beginthread_c(void(*start_address)(void *), uint32_t stack_size, void *arglist)
{
    run_thread_args *thread_args;

#ifdef DEBUG_CLIB
    eprintf("_beginthread: 0x%" PRIxPTR ", %i, 0x%" PRIxPTR "\n", (uintptr_t) start_address, stack_size, (uintptr_t) arglist);
#endif

    thread_args = (run_thread_args *) malloc(sizeof(run_thread_args));
    if (thread_args == NULL)
    {
        return 0;
    }

    thread_args->start_address = start_address;
    thread_args->arglist = arglist;

#ifdef _WIN32
    if ((intptr_t)-1 == (intptr_t)_beginthread(run_thread, stack_size, thread_args))
    {
        free(thread_args);
        return 0;
    }

    // function should return handle to thread, but Septerra Core only checks whether return value is 0
    return 1;
#else
    pthread_attr_t attr;
    pthread_t thread;

    if (0 != pthread_attr_init(&attr))
    {
        free(thread_args);
        return 0;
    }

    if (0 != pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
    {
        pthread_attr_destroy(&attr);
        free(thread_args);
        return 0;
    }

    if (stack_size != 0)
    {
        if (0 != pthread_attr_setstacksize(&attr, stack_size))
        {
            pthread_attr_destroy(&attr);
            free(thread_args);
            return 0;
        }
    }

    if (0 != pthread_create(&thread, &attr, &run_thread, thread_args))
    {
        pthread_attr_destroy(&attr);
        free(thread_args);
        return 0;
    }

    pthread_attr_destroy(&attr);
    // function should return handle to thread, but Septerra Core only checks whether return value is 0
    return 1;
#endif
}

void CCALL sync_c(void)
{
#ifdef DEBUG_CLIB
    eprintf("sync\n");
#endif

#if !defined(_WIN32)
    sync();
#endif
}


#if !defined(_WIN32)
/*
 * Map a Windows path to a host path. Port change: drive "C:" is the game
 * folder (the current directory). The macOS volume is case-insensitive, so
 * only the drive letter and the backslashes change.
 * Returns 1 if the file exists, 0 if not (dst is filled in both cases).
 */
/* Port change: the current directory of the guest, relative to drive C:
   (empty: C:\\). Relative paths start there (SetCurrentDirectoryA). */
static char clib_cur_dir[1024];

void CLIB_SetCurrentDir(const char *path)
{
    char tmp[1024];
    int absolute = 0;
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':')
    {
        path += 2;
        absolute = 1;
    }
    if (*path == '\\' || *path == '/') absolute = 1;
    while (*path == '\\' || *path == '/') path++;
    if (absolute || clib_cur_dir[0] == 0) snprintf(tmp, sizeof(tmp), "%s", path);
    else snprintf(tmp, sizeof(tmp), "%s\\%s", clib_cur_dir, path);
    size_t n = strlen(tmp);
    while (n > 0 && (tmp[n - 1] == '\\' || tmp[n - 1] == '/')) tmp[--n] = 0;
    strcpy(clib_cur_dir, tmp);
}

const char *CLIB_GetCurrentDir(void)
{
    return clib_cur_dir;
}

int CLIB_FindFile(const char *src, char *dst)
{
    char *d;
    char joined[2048];

    if (((src[0] >= 'A' && src[0] <= 'Z') || (src[0] >= 'a' && src[0] <= 'z')) && src[1] == ':')
    {
        src += 2;
    }
    else if (clib_cur_dir[0] != 0 && src[0] != '\\' && src[0] != '/')
    {
        /* relative to the current directory */
        snprintf(joined, sizeof(joined), "%s%s%s", clib_cur_dir, src[0] ? "\\" : "", src);
        src = joined;
    }
    while (*src == '\\' || *src == '/')
    {
        src++;
    }
    if (*src == 0)
    {
        strcpy(dst, ".");
        return 1;
    }

    for (d = dst; *src; src++, d++)
    {
        *d = (*src == '\\') ? '/' : *src;
    }
    *d = 0;

    return (0 == access(dst, F_OK)) ? 1 : 0;
}
#endif


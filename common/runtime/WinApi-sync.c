/*
 *  Native port: Win32 threads and synchronization objects (events, mutexes,
 *  thread handles, waits) and winmm multimedia timers. First needed by
 *  Revenant, which has a loader thread, a map update thread and a periodic
 *  timer that pulses an event.
 *  MIT license, see the README.md of the repository.
 *
 *  All objects share one host mutex and one condition variable: every state
 *  change wakes all waiters, and each waiter checks its own objects again.
 *  This is simple and correct, and the games have only a few threads.
 */

#include "game-info.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>
#include "WinApi.h"
#include "Game-Memory.h"
#include "guest.h"
#include "platform.h"

#define WAIT_OBJECT_0   0x00000000u
#define WAIT_TIMEOUT    0x00000102u
#define WAIT_FAILED     0xffffffffu
#define INFINITE_WAIT   0xffffffffu
#define MAXIMUM_WAIT_OBJECTS 64
#define STILL_ACTIVE    259

enum sync_kind { SK_EVENT, SK_MUTEX, SK_THREAD };

/* The first field matches handle_types in WinApi.h (HT_SYNC). The object
 * lives in guest memory because the game stores the handle as a 32-bit value. */
typedef struct sync_object {
    handle_types handle_type;
    enum sync_kind kind;
    int manual_reset;           /* event */
    int signaled;               /* event: set; thread: finished */
    uint32_t pulse_gen;         /* event: PulseEvent counter */
    int pulse_tokens;           /* auto-reset event: waiters a pulse can still release */
    int waiters;
    pthread_t owner;            /* mutex */
    int owned;                  /* mutex: recursion count, 0 = free */
    uint32_t exit_code;         /* thread */
    uint32_t start, param;      /* thread: guest start address and parameter */
} sync_object;

static pthread_mutex_t sync_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sync_cond = PTHREAD_COND_INITIALIZER;

extern "C" uint32_t CCALL CallX86Function(uint32_t address, int nargs, const uint32_t *args);
extern "C" void x86_deinitialize_cpu(void);

/* Sleep until an absolute CLOCK_MONOTONIC time (macOS has no clock_nanosleep). */
static int clock_nanosleep_abstime(const struct timespec *t)
{
    struct timespec now, d;
    clock_gettime(CLOCK_MONOTONIC, &now);
    d.tv_sec = t->tv_sec - now.tv_sec;
    d.tv_nsec = t->tv_nsec - now.tv_nsec;
    if (d.tv_nsec < 0) { d.tv_nsec += 1000000000; d.tv_sec--; }
    if (d.tv_sec < 0) return 0;
    return nanosleep(&d, NULL) == 0 ? 0 : errno;
}

static int trace_sync(void)
{
    static int trace = -1;
    if (trace < 0) trace = (game_getenv("TRACE_SYNC") != NULL);
    return trace;
}

static sync_object *new_object(enum sync_kind kind)
{
    sync_object *o = (sync_object *)x86_malloc(sizeof(sync_object));
    if (o == NULL)
    {
        Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    memset(o, 0, sizeof(*o));
    o->handle_type = HT_SYNC;
    o->kind = kind;
    return o;
}

static sync_object *as_sync(void *h)
{
    uint32_t g = to_guest(h);
    if (h == NULL || g < 0x10000 || g >= 0xfff00000u) return NULL;   /* pseudo handles */
    sync_object *o = (sync_object *)h;
    return o->handle_type == HT_SYNC ? o : NULL;
}

/* Called by CloseHandle (WinApi-kernel32.c). A running thread keeps its
 * object (the thread writes its exit code into it), so it is not freed. */
extern "C" void Winapi_CloseSyncHandle(void *h)
{
    sync_object *o = as_sync(h);
    if (o == NULL) return;
    pthread_mutex_lock(&sync_lock);
    int keep = (o->kind == SK_THREAD && !o->signaled) || o->waiters > 0;
    pthread_mutex_unlock(&sync_lock);
    if (!keep) x86_free(o);
}

/* ---------------------------------------------------------------- events */

extern "C" void *CreateEventA_c(void *lpEventAttributes, uint32_t bManualReset, uint32_t bInitialState, const char *lpName)
{
    sync_object *o = new_object(SK_EVENT);
    if (o == NULL) return NULL;
    o->manual_reset = bManualReset != 0;
    o->signaled = bInitialState != 0;
    Winapi_SetLastError(0);
    if (trace_sync()) fprintf(stderr, "CreateEventA(manual %u, initial %u) -> %p\n", bManualReset, bInitialState, (void *)o);
    return o;
}

extern "C" uint32_t SetEvent_c(void *hEvent)
{
    sync_object *o = as_sync(hEvent);
    if (o == NULL || o->kind != SK_EVENT) return 0;
    pthread_mutex_lock(&sync_lock);
    o->signaled = 1;
    pthread_cond_broadcast(&sync_cond);
    pthread_mutex_unlock(&sync_lock);
    return 1;
}

extern "C" uint32_t ResetEvent_c(void *hEvent)
{
    sync_object *o = as_sync(hEvent);
    if (o == NULL || o->kind != SK_EVENT) return 0;
    pthread_mutex_lock(&sync_lock);
    o->signaled = 0;
    pthread_mutex_unlock(&sync_lock);
    return 1;
}

/* PulseEvent: release the threads that wait now (manual reset: all of them,
 * auto reset: one), then the event is reset. With no waiters nothing happens. */
extern "C" uint32_t PulseEvent_c(void *hEvent)
{
    sync_object *o = as_sync(hEvent);
    if (o == NULL || o->kind != SK_EVENT) return 0;
    pthread_mutex_lock(&sync_lock);
    if (o->waiters > 0)
    {
        o->pulse_gen++;
        o->pulse_tokens = o->manual_reset ? o->waiters : 1;
        pthread_cond_broadcast(&sync_cond);
    }
    o->signaled = 0;
    pthread_mutex_unlock(&sync_lock);
    return 1;
}

/* --------------------------------------------------------------- mutexes */

extern "C" void *CreateMutexA_c(void *lpMutexAttributes, uint32_t bInitialOwner, const char *lpName)
{
    sync_object *o = new_object(SK_MUTEX);
    if (o == NULL) return NULL;
    if (bInitialOwner)
    {
        o->owner = pthread_self();
        o->owned = 1;
    }
    Winapi_SetLastError(0);   /* named mutexes: never ERROR_ALREADY_EXISTS (one instance) */
    return o;
}

extern "C" uint32_t ReleaseMutex_c(void *hMutex)
{
    sync_object *o = as_sync(hMutex);
    if (o == NULL || o->kind != SK_MUTEX) return 0;
    pthread_mutex_lock(&sync_lock);
    if (o->owned == 0 || !pthread_equal(o->owner, pthread_self()))
    {
        pthread_mutex_unlock(&sync_lock);
        Winapi_SetLastError(288);   /* ERROR_NOT_OWNER */
        return 0;
    }
    if (--o->owned == 0) pthread_cond_broadcast(&sync_cond);
    pthread_mutex_unlock(&sync_lock);
    return 1;
}

/* ----------------------------------------------------------------- waits */

/* With sync_lock held: can this object be acquired now? */
static int is_ready(sync_object *o, uint32_t gen)
{
    switch (o->kind)
    {
        case SK_EVENT:
            return o->signaled || (o->pulse_gen != gen && o->pulse_tokens > 0);
        case SK_MUTEX:
            return o->owned == 0 || pthread_equal(o->owner, pthread_self());
        default:
            return o->signaled;
    }
}

static void acquire(sync_object *o, uint32_t gen)
{
    switch (o->kind)
    {
        case SK_EVENT:
            if (o->signaled)
            {
                if (!o->manual_reset) o->signaled = 0;
            }
            else
            {
                o->pulse_tokens--;
            }
            break;
        case SK_MUTEX:
            o->owner = pthread_self();
            o->owned++;
            break;
        default:
            break;
    }
}

static uint32_t wait_objects(uint32_t count, sync_object **objs, int wait_all, uint32_t ms)
{
    uint32_t gen[MAXIMUM_WAIT_OBJECTS], i, result = WAIT_TIMEOUT;
    struct timespec deadline;

    if (ms != INFINITE_WAIT)
    {
        struct timeval now;
        gettimeofday(&now, NULL);
        uint64_t ns = (uint64_t)now.tv_usec * 1000 + (uint64_t)ms * 1000000;
        deadline.tv_sec = now.tv_sec + (time_t)(ns / 1000000000);
        deadline.tv_nsec = (long)(ns % 1000000000);
    }

    pthread_mutex_lock(&sync_lock);
    for (i = 0; i < count; i++)
    {
        gen[i] = objs[i]->pulse_gen;
        objs[i]->waiters++;
    }
    for (;;)
    {
        if (wait_all)
        {
            for (i = 0; i < count && is_ready(objs[i], gen[i]); i++) {}
            if (i == count)
            {
                for (i = 0; i < count; i++) acquire(objs[i], gen[i]);
                result = WAIT_OBJECT_0;
                break;
            }
        }
        else
        {
            for (i = 0; i < count && !is_ready(objs[i], gen[i]); i++) {}
            if (i < count)
            {
                acquire(objs[i], gen[i]);
                result = WAIT_OBJECT_0 + i;
                break;
            }
        }
        if (ms == 0) break;
        if (ms == INFINITE_WAIT)
        {
            pthread_cond_wait(&sync_cond, &sync_lock);
        }
        else if (pthread_cond_timedwait(&sync_cond, &sync_lock, &deadline) == ETIMEDOUT)
        {
            /* one last check below the loop top would repeat the work: stop */
            break;
        }
    }
    for (i = 0; i < count; i++) objs[i]->waiters--;
    pthread_mutex_unlock(&sync_lock);
    return result;
}

extern "C" uint32_t WaitForSingleObject_c(void *hHandle, uint32_t dwMilliseconds)
{
    sync_object *o = as_sync(hHandle);
    if (o == NULL)
    {
        /* pseudo handles and files: always signaled */
        if (hHandle != NULL) return WAIT_OBJECT_0;
        Winapi_SetLastError(6);   /* ERROR_INVALID_HANDLE */
        return WAIT_FAILED;
    }
    return wait_objects(1, &o, 0, dwMilliseconds);
}

extern "C" uint32_t WaitForMultipleObjects_c(uint32_t nCount, void *lpHandles, uint32_t bWaitAll, uint32_t dwMilliseconds)
{
    sync_object *objs[MAXIMUM_WAIT_OBJECTS];
    uint32_t i;
    if (nCount == 0 || nCount > MAXIMUM_WAIT_OBJECTS || lpHandles == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return WAIT_FAILED;
    }
    for (i = 0; i < nCount; i++)
    {
        objs[i] = as_sync(from_guest(rd32((uint8_t *)lpHandles + 4 * i)));
        if (objs[i] == NULL)
        {
            Winapi_SetLastError(6);
            return WAIT_FAILED;
        }
    }
    return wait_objects(nCount, objs, bWaitAll != 0, dwMilliseconds);
}

/* --------------------------------------------------------------- threads */

static thread_local sync_object *current_thread_object;

static void finish_thread(sync_object *o, uint32_t code)
{
    pthread_mutex_lock(&sync_lock);
    o->exit_code = code;
    o->signaled = 1;
    pthread_cond_broadcast(&sync_cond);
    pthread_mutex_unlock(&sync_lock);
}

static void *thread_main(void *arg)
{
    sync_object *o = (sync_object *)arg;
    current_thread_object = o;
    uint32_t code = CallX86Function(o->start, 1, &o->param);
    x86_deinitialize_cpu();
    if (trace_sync()) fprintf(stderr, "thread %p (start %s) ended: %u\n", (void *)o, x86_code_name(o->start), code);
    finish_thread(o, code);
    return NULL;
}

extern "C" void *CreateThread_c(void *lpThreadAttributes, uint32_t dwStackSize, uint32_t lpStartAddress, uint32_t lpParameter,
                                uint32_t dwCreationFlags, void *lpThreadId)
{
    static uint32_t next_id = 0x100;
    pthread_attr_t attr;
    pthread_t thread;
    sync_object *o;

    if (dwCreationFlags & 4)   /* CREATE_SUSPENDED: ResumeThread is not implemented */
    {
        fprintf(stderr, "CreateThread: CREATE_SUSPENDED is not supported (from %s)\n", guest_caller());
    }
    o = new_object(SK_THREAD);
    if (o == NULL) return NULL;
    o->start = lpStartAddress;
    o->param = lpParameter;
    o->exit_code = STILL_ACTIVE;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 8u << 20);   /* host stack for the recompiled code */
    if (pthread_create(&thread, &attr, thread_main, o) != 0)
    {
        pthread_attr_destroy(&attr);
        x86_free(o);
        Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    pthread_attr_destroy(&attr);
    if (lpThreadId) wr32(lpThreadId, __atomic_add_fetch(&next_id, 1, __ATOMIC_SEQ_CST));
    if (trace_sync()) fprintf(stderr, "CreateThread(start %s, param 0x%x) -> %p\n", x86_code_name(lpStartAddress), lpParameter, (void *)o);
    return o;
}

extern "C" void ExitThread_c(uint32_t dwExitCode)
{
    if (current_thread_object != NULL)
    {
        finish_thread(current_thread_object, dwExitCode);
        pthread_exit(NULL);
    }
    /* ExitThread on the main thread ends the program */
    exit((int)dwExitCode);
}

/* ------------------------------------------------- winmm multimedia timers */

typedef struct mm_timer {
    uint32_t id, delay, callback, user, flags;
    volatile int stop;
    struct mm_timer *next;
} mm_timer;

static mm_timer *timers;
static uint32_t next_timer_id = 1;

static void *timer_main(void *arg)
{
    mm_timer *t = (mm_timer *)arg;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    do {
        uint64_t ns = (uint64_t)next.tv_nsec + (uint64_t)t->delay * 1000000;
        next.tv_sec += (time_t)(ns / 1000000000);
        next.tv_nsec = (long)(ns % 1000000000);
        while (clock_nanosleep_abstime(&next) == EINTR) {}
        if (t->stop) break;
        if (t->flags & 0x10)   /* TIME_CALLBACK_EVENT_SET: callback is an event handle */
        {
            SetEvent_c(from_guest(t->callback));
        }
        else if (t->flags & 0x20)   /* TIME_CALLBACK_EVENT_PULSE */
        {
            PulseEvent_c(from_guest(t->callback));
        }
        else
        {
            /* void CALLBACK TimeProc(UINT uID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2) */
            uint32_t args[5] = { t->id, 0, t->user, 0, 0 };
            CallX86Function(t->callback, 5, args);
        }
    } while ((t->flags & 1) && !t->stop);   /* TIME_PERIODIC */
    return NULL;
}

extern "C" uint32_t timeSetEvent_c(uint32_t uDelay, uint32_t uResolution, uint32_t lpTimeProc, uint32_t dwUser, uint32_t fuEvent)
{
    pthread_t thread;
    mm_timer *t = (mm_timer *)calloc(1, sizeof(mm_timer));
    if (t == NULL || uDelay == 0) { free(t); return 0; }
    pthread_mutex_lock(&sync_lock);
    t->id = next_timer_id++;
    t->delay = uDelay;
    t->callback = lpTimeProc;
    t->user = dwUser;
    t->flags = fuEvent;
    t->next = timers;
    timers = t;
    pthread_mutex_unlock(&sync_lock);
    if (pthread_create(&thread, NULL, timer_main, t) != 0) return 0;
    pthread_detach(thread);
    if (trace_sync()) fprintf(stderr, "timeSetEvent(%u ms, %s, flags 0x%x) -> %u\n", uDelay, x86_code_name(lpTimeProc), fuEvent, t->id);
    return t->id;
}

extern "C" uint32_t timeKillEvent_c(uint32_t uTimerID)
{
    pthread_mutex_lock(&sync_lock);
    for (mm_timer *t = timers; t; t = t->next)
    {
        if (t->id == uTimerID && !t->stop)
        {
            t->stop = 1;   /* the timer thread ends; the record stays (small leak) */
            pthread_mutex_unlock(&sync_lock);
            return 0;      /* TIMERR_NOERROR */
        }
    }
    pthread_mutex_unlock(&sync_lock);
    return 97;   /* MMSYSERR_INVALPARAM */
}

extern "C" uint32_t timeBeginPeriod_c(uint32_t uPeriod) { return 0; }
extern "C" uint32_t timeEndPeriod_c(uint32_t uPeriod) { return 0; }

/* TIMECAPS { UINT wPeriodMin, wPeriodMax } */
extern "C" uint32_t timeGetDevCaps_c(void *ptc, uint32_t cbtc)
{
    if (ptc == NULL || cbtc < 8) return 97;
    wr32(ptc, 1);
    wr32((uint8_t *)ptc + 4, 1000000);
    return 0;
}

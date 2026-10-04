/**
 *  Port addition: <GAME>_TRACE_LAG=1 measures where the time of each game
 *  frame goes, to find the cause of stutter while the game is played with
 *  a real mouse and keyboard.
 *
 *  Once per second it prints one line. It also prints a "slow" line for a
 *  single event that is above its limit:
 *    work     time between two Sleep calls (the game's own code; limit 50 ms)
 *    oversleep  how much longer a Sleep took than asked (limit 10 ms)
 *    present  time that SDL_RenderPresent blocks (limit 20 ms)
 *    key      time from the key press to the game reading it (limit 40 ms)
 */

#include "game-info.h"
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
#define LAG_EXTERN extern "C"
#else
#define LAG_EXTERN
#endif

static int lag_on = -1;
static double ms_per_count;
static Uint64 second_start, sleep_end;
static unsigned n_present, n_sleep, n_motion, n_key, n_warp;
static double present_sum, present_max, upload_max, work_max, over_max, key_max;

static int lag_enabled(void)
{
    if (lag_on < 0)
    {
        lag_on = game_getenv("TRACE_LAG") != NULL;
        ms_per_count = 1000.0 / (double)SDL_GetPerformanceFrequency();
    }
    return lag_on;
}

static double now_s(void)
{
    return SDL_GetTicks() / 1000.0;
}

static void lag_second(Uint64 now)
{
    if (second_start == 0) second_start = now;
    if ((now - second_start) * ms_per_count < 1000.0) return;
    fprintf(stderr, "%7.1f s lag: frames %u, work max %.1f ms, oversleep max %.1f ms, presents %u (blocked %.1f ms, max %.1f, upload max %.1f), mouse moves %u, warps %u, keys %u (delay max %.1f ms)\n",
            now_s(), n_sleep, work_max, over_max, n_present, present_sum, present_max, upload_max, n_motion, n_warp, n_key, key_max);
    second_start = now;
    n_present = n_sleep = n_motion = n_key = n_warp = 0;
    present_sum = present_max = upload_max = work_max = over_max = key_max = 0;
}

LAG_EXTERN void LagTrace_Present(Uint64 t_start, Uint64 t_upload, Uint64 t_end)
{
    double up, pr;

    if (!lag_enabled()) return;
    up = (t_upload - t_start) * ms_per_count;
    pr = (t_end - t_upload) * ms_per_count;
    n_present++;
    present_sum += pr;
    if (pr > present_max) present_max = pr;
    if (up > upload_max) upload_max = up;
    if (pr > 20.0) fprintf(stderr, "%7.1f s slow: present blocked %.1f ms\n", now_s(), pr);
    lag_second(t_end);
}

LAG_EXTERN void LagTrace_SleepBegin(uint32_t ms, uint64_t *t_begin)
{
    double work;

    if (!lag_enabled()) return;
    *t_begin = SDL_GetPerformanceCounter();
    if (sleep_end != 0)
    {
        work = (*t_begin - sleep_end) * ms_per_count;
        if (work > work_max) work_max = work;
        if (work > 50.0) fprintf(stderr, "%7.1f s slow: game work %.1f ms before Sleep(%u)\n", now_s(), work, ms);
    }
}

LAG_EXTERN void LagTrace_SleepEnd(uint32_t ms, uint64_t t_begin)
{
    double over;

    if (!lag_enabled()) return;
    sleep_end = SDL_GetPerformanceCounter();
    over = (sleep_end - t_begin) * ms_per_count - ms;
    n_sleep++;
    if (over > over_max) over_max = over;
    if (over > 10.0) fprintf(stderr, "%7.1f s slow: Sleep(%u) took %.1f ms more\n", now_s(), ms, over);
    lag_second(sleep_end);
}

/* An input event that the game takes out of the queue */
LAG_EXTERN void LagTrace_Event(const SDL_Event *event)
{
    double delay;

    if (!lag_enabled()) return;
    if (event->type == SDL_MOUSEMOTION) n_motion++;
    else if (event->type == SDL_KEYDOWN && !event->key.repeat)
    {
        n_key++;
        delay = (double)(SDL_GetTicks() - event->key.timestamp);
        if (delay > key_max) key_max = delay;
        if (delay > 40.0) fprintf(stderr, "%7.1f s slow: key 0x%x read %.0f ms after the press\n", now_s(), (unsigned)event->key.keysym.sym, delay);
    }
}

LAG_EXTERN void LagTrace_Warp(void)
{
    if (!lag_enabled()) return;
    n_warp++;
}

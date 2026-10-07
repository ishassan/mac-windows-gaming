/*
 *  Port change: Miles Sound System (mss32.dll) functions that the game
 *  uses, on the native mixer (audio-mixer.c).
 *  Samples play WAV images in guest memory (MP3 images through
 *  audio-decode.c). Streams play WAV files from disk.
 *  CD audio is in WinApi-mss32-redbook.c.
 *  MIT license, see README.md.
 */

#include "game-info.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "audio-mixer.h"
#include "CLIB.h"
#include "Game-Memory.h"
#include "guest.h"
#include "WinApi.h"
#include "platform.h"

#ifndef EXTERN_C
#define EXTERN_C extern "C"
#endif

/* Sample and stream status values */
#define SMP_FREE    0x0001
#define SMP_DONE    0x0002
#define SMP_PLAYING 0x0004
#define SMP_STOPPED 0x0008

#define DIGITAL_DRIVER_HANDLE 0xfa00

/* <GAME>_TRACE_SOUND=1 logs streams and samples */
static int trace_sound(void)
{
    static int trace = -1;
    if (trace < 0) trace = (game_getenv("TRACE_SOUND") != NULL);
    return trace;
}

/* <GAME>_TRACE_SOUND=2 also logs each call (once per function) */
#define TRACE_CALL() do { static int seen_; if (!seen_ && trace_sound() && game_getenv("TRACE_SOUND")[0] == '2') { seen_ = 1; fprintf(stderr, "Miles call: %s\n", __func__); } } while (0)

/* A handle is a small guest memory block (unique guest pointer):
 *   [0] mixer channel id   [1] flags (bit 0: started, EOS not yet sent)
 *   [2] EOS callback (guest code address)   [3] magic
 *   [4..11] user data (AIL_set_sample_user_data)   [12] length in ms
 *   [13..31] Miles 6.5: 3D position and volume (see "Miles 6.5" below) */
#define HANDLE_DWORDS 32
#define USER_DATA     4
static void *new_handle(int channel)
{
    uint32_t *h = (uint32_t *)x86_malloc(4 * HANDLE_DWORDS);
    if (h == NULL) return NULL;
    memset(h, 0, 4 * HANDLE_DWORDS);
    h[0] = (uint32_t)channel;
    h[3] = 0x4d535333;
    return h;
}

static uint32_t *handle_of(uint32_t guest)
{
    uint32_t *h;
    if (guest == 0) return NULL;
    h = (uint32_t *)from_guest(guest);
    return (h[3] == 0x4d535333) ? h : NULL;
}

static int channel_of(uint32_t guest)
{
    uint32_t *h = handle_of(guest);
    return h ? (int)h[0] : 0;
}

static uint32_t status_of(uint32_t guest)
{
    uint32_t *h = handle_of(guest);
    int ch;
    if (h == NULL) return SMP_FREE;
    ch = (int)h[0];
    if (Mixer_IsPlaying(ch)) return Mixer_IsPaused(ch) ? SMP_STOPPED : SMP_PLAYING;
    return SMP_DONE;
}

static int mss_set_image(uint32_t S, const uint8_t *img, uint32_t size, const char *hint);

/* ------------------------------------------------------------ driver */

static int preferences[64];
static int first_open_after_setup;

EXTERN_C uint32_t AIL_startup_c(void)
{
    TRACE_CALL();
    return 1;
}

EXTERN_C uint32_t AIL_shutdown_c(void)
{
    TRACE_CALL();
    Mixer_Shutdown();
    return 0;
}

EXTERN_C uint32_t AIL_set_preference_c(uint32_t number, uint32_t value)
{
    TRACE_CALL();
    uint32_t old = 0;
#ifdef GAME_MSS_FAIL_FIRST_WAVEOUT
    if (number == 0) first_open_after_setup = 1;
#endif
    if (number < 64)
    {
        old = (uint32_t)preferences[number];
        preferences[number] = (int)value;
    }
    return old;
}

EXTERN_C uint32_t AIL_get_preference_c(uint32_t number)
{
    TRACE_CALL();
    return (number < 64) ? (uint32_t)preferences[number] : 0;
}

EXTERN_C uint32_t AIL_waveOutOpen_c(void *drvr, void *lphWaveOut, uint32_t wDeviceID, void *lpFormat)
{
    TRACE_CALL();
    /* GAME_MSS_FAIL_FIRST_WAVEOUT (Commandos): the game sound set-up (0x5D76C0) calls AIL_set_preference(0, 16),
     * then AIL_waveOutOpen. If that first call succeeds, the game uses its
     * DirectSound mode, where it reads the internal DirectSound data of the
     * Miles driver and sample structures. This layer does not have them.
     * So the first call fails; the game then changes preference 15
     * (DIG_USE_WAVEOUT) and calls again, and in that mode it uses only the
     * Miles functions. */
    if (first_open_after_setup)
    {
        first_open_after_setup = 0;
        if (drvr) wr32(drvr, 0);
        return 1;
    }
    if (Mixer_Init() != 0)
    {
        fprintf(stderr, "Miles: no audio device, sound is off\n");
        if (drvr) wr32(drvr, 0);
        return 1;   /* error */
    }
    if (drvr) wr32(drvr, DIGITAL_DRIVER_HANDLE);
    if (lphWaveOut) wr32(lphWaveOut, 0);
    return 0;
}

EXTERN_C uint32_t AIL_set_DirectSound_HWND_c(uint32_t dig, void *wnd)
{
    TRACE_CALL();
    return 1;
}

EXTERN_C void AIL_set_digital_master_volume_c(uint32_t dig, uint32_t master_volume)
{
    TRACE_CALL();
    Mixer_SetMaster((int)master_volume);
}

EXTERN_C uint32_t AIL_digital_master_volume_c(uint32_t dig)
{
    TRACE_CALL();
    return (uint32_t)Mixer_GetMaster();
}

EXTERN_C void AIL_lock_c(void) { TRACE_CALL(); }
EXTERN_C void AIL_unlock_c(void) { TRACE_CALL(); }

EXTERN_C void *AIL_mem_alloc_lock_c(uint32_t size)
{
    TRACE_CALL();
    return x86_malloc(size);
}

EXTERN_C void AIL_mem_free_lock_c(void *ptr)
{
    TRACE_CALL();
    if (ptr) x86_free(ptr);
}

/* ------------------------------------------------------------ samples */

EXTERN_C uint32_t AIL_allocate_sample_handle_c(uint32_t dig)
{
    TRACE_CALL();
    int ch = Mixer_Allocate();
    void *h;
    if (ch == 0) return 0;
    h = new_handle(ch);
    if (h == NULL)
    {
        Mixer_Release(ch);
        return 0;
    }
    return to_guest(h);
}

static void eos_forget(uint32_t S);

EXTERN_C void AIL_release_sample_handle_c(uint32_t S)
{
    TRACE_CALL();
    uint32_t *h = handle_of(S);
    if (h == NULL) return;
    eos_forget(S);
    Mixer_Release((int)h[0]);
    h[3] = 0;
    x86_free(h);
}

EXTERN_C void AIL_init_sample_c(uint32_t S)
{
    TRACE_CALL();
    int ch = channel_of(S);
    Mixer_Stop(ch);
    Mixer_SetVolume(ch, 127);
    Mixer_SetPan(ch, 64);
    Mixer_SetLoops(ch, 1);
}

EXTERN_C uint32_t AIL_set_sample_file_c(uint32_t S, void *file_image, uint32_t block)
{
    TRACE_CALL();
    const uint8_t *pcm;
    uint32_t bytes;
    int rate, bits, chans;
    int ch = channel_of(S);

    if (ch == 0 || file_image == NULL) return 0;
    /* The image size is not given: the RIFF header has it. */
    {
        uint32_t riff_size;
        memcpy(&riff_size, (uint8_t *)file_image + 4, 4);
        if (Mixer_ParseWav((const uint8_t *)file_image, riff_size + 8, &pcm, &bytes, &rate, &bits, &chans) != 0)
        {
            /* not 16-bit PCM: ADPCM or MP3 (Miles 6.5 games) */
            return (uint32_t)mss_set_image(S, (const uint8_t *)file_image, riff_size + 8, ".wav");
        }
    }
    Mixer_SetData(ch, pcm, bytes, rate, bits, chans, NULL);
    handle_of(S)[12] = (uint32_t)((uint64_t)bytes * 1000 / ((uint64_t)rate * (bits / 8) * chans));
    if (trace_sound()) fprintf(stderr, "Miles: sample %u bytes, %d Hz, %d bit, %d ch\n", bytes, rate, bits, chans);
    return 1;
}

EXTERN_C void AIL_start_sample_c(uint32_t S)
{
    TRACE_CALL();
    uint32_t *h = handle_of(S);
    if (h == NULL) return;
    h[1] |= 1;
    Mixer_Start((int)h[0]);
}

EXTERN_C void AIL_end_sample_c(uint32_t S)
{
    TRACE_CALL();
    Mixer_Stop(channel_of(S));
}

EXTERN_C uint32_t AIL_sample_status_c(uint32_t S)
{
    TRACE_CALL();
    return status_of(S);
}

EXTERN_C void AIL_set_sample_volume_c(uint32_t S, uint32_t volume)
{
    TRACE_CALL();
    Mixer_SetVolume(channel_of(S), (int)volume);
}

EXTERN_C void AIL_set_sample_pan_c(uint32_t S, uint32_t pan)
{
    TRACE_CALL();
    Mixer_SetPan(channel_of(S), (int)pan);
}

EXTERN_C void AIL_set_sample_loop_count_c(uint32_t S, uint32_t loop_count)
{
    TRACE_CALL();
    Mixer_SetLoops(channel_of(S), (int)loop_count);
}

/* ------------------------------------------------------------ streams */

static uint32_t open_stream_65(const char *filename);
static uint32_t cb_open;

static uint32_t open_stream_disk(uint32_t dig, const char *filename, uint32_t stream_mem);

EXTERN_C uint32_t AIL_open_stream_c(uint32_t dig, const char *filename, uint32_t stream_mem)
{
    TRACE_CALL();
    /* Miles 6.5: the file through the game's file callbacks */
    if (filename != NULL && cb_open != 0)
    {
        uint32_t s = open_stream_65(filename);
        if (s != 0) return s;
    }
    return open_stream_disk(dig, filename, stream_mem);
}

static uint32_t open_stream_disk(uint32_t dig, const char *filename, uint32_t stream_mem)
{
    char path[4096];
    FILE *f;
    long size;
    uint8_t *img;
    const uint8_t *pcm;
    uint32_t bytes;
    int rate, bits, chans, ch;
    void *h;

    if (filename != NULL && strncmp(filename, "\\\\\\\\", 4) == 0)
    {
        /* "\\\\<handle>": the game opened the file (on disk or inside
         * an archive such as WARGAME.DIR of Commandos) and gives the Win32 file handle; the WAV image
         * starts at the current file position. */
        handle hf = (handle)from_guest((uint32_t)strtoul(filename + 4, NULL, 10));
        uint8_t head[12];
        uint32_t riff_size;

        if (hf == NULL || hf->handle_type != HT_FILE || hf->fh.f == NULL) return 0;
        f = (FILE *)hf->fh.f;
        if (fread(head, 1, 12, f) != 12 || memcmp(head, "RIFF", 4) != 0)
        {
            if (trace_sound()) fprintf(stderr, "Miles: stream handle %s is not a WAV file\n", filename);
            return 0;
        }
        memcpy(&riff_size, head + 4, 4);
        size = (long)riff_size + 8;
        img = (uint8_t *)malloc(size);
        if (img == NULL) return 0;
        memcpy(img, head, 12);
        size = 12 + (long)fread(img + 12, 1, size - 12, f);
    }
    else
    {
        if (filename == NULL || !CLIB_FindFile(filename, path))
        {
            if (trace_sound()) fprintf(stderr, "Miles: stream file not found: %s\n", filename ? filename : "(null)");
            return 0;
        }
        f = fopen(path, "rb");
        if (f == NULL) return 0;
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
        img = (uint8_t *)malloc(size > 0 ? size : 1);
        if (img == NULL || fread(img, 1, size, f) != (size_t)size)
        {
            fclose(f);
            free(img);
            return 0;
        }
        fclose(f);
    }

    if (Mixer_ParseWav(img, (uint32_t)size, &pcm, &bytes, &rate, &bits, &chans) != 0)
    {
        fprintf(stderr, "Miles: stream format not supported: %s\n", filename);
        free(img);
        return 0;
    }
    ch = Mixer_Allocate();
    if (ch == 0)
    {
        free(img);
        return 0;
    }
    Mixer_SetData(ch, pcm, bytes, rate, bits, chans, img);
    if (trace_sound()) fprintf(stderr, "Miles: stream %s, %d Hz, %d bit, %d ch\n", filename, rate, bits, chans);
    h = new_handle(ch);
    if (h == NULL)
    {
        Mixer_Release(ch);
        return 0;
    }
    return to_guest(h);
}

EXTERN_C void AIL_close_stream_c(uint32_t stream)
{
    TRACE_CALL();
    AIL_release_sample_handle_c(stream);
}

EXTERN_C void AIL_start_stream_c(uint32_t stream)
{
    TRACE_CALL();
    AIL_start_sample_c(stream);
}

EXTERN_C void AIL_pause_stream_c(uint32_t stream, uint32_t onoff)
{
    TRACE_CALL();
    Mixer_Pause(channel_of(stream), onoff ? 1 : 0);
}

EXTERN_C void AIL_set_stream_loop_count_c(uint32_t stream, uint32_t count)
{
    TRACE_CALL();
    Mixer_SetLoops(channel_of(stream), (int)count);
}

EXTERN_C void AIL_set_stream_volume_c(uint32_t stream, uint32_t volume)
{
    TRACE_CALL();
    Mixer_SetVolume(channel_of(stream), (int)volume);
}

EXTERN_C void AIL_set_stream_pan_c(uint32_t stream, uint32_t pan)
{
    TRACE_CALL();
    Mixer_SetPan(channel_of(stream), (int)pan);
}

EXTERN_C uint32_t AIL_stream_volume_c(uint32_t stream)
{
    TRACE_CALL();
    return (uint32_t)Mixer_GetVolume(channel_of(stream));
}

EXTERN_C uint32_t AIL_stream_pan_c(uint32_t stream)
{
    TRACE_CALL();
    return (uint32_t)Mixer_GetPan(channel_of(stream));
}

EXTERN_C uint32_t AIL_stream_status_c(uint32_t stream)
{
    TRACE_CALL();
    return status_of(stream);
}

/* CD audio (AIL_redbook_*): WinApi-mss32-redbook.c */

/* ------------------------------------------------- more sample functions */

EXTERN_C uint32_t AIL_set_named_sample_file_c(uint32_t S, const char *file_type_suffix, void *file_image, uint32_t file_size, uint32_t block)
{
    TRACE_CALL();
    const uint8_t *pcm;
    uint32_t bytes;
    int rate, bits, chans;
    int ch = channel_of(S);

    if (ch == 0 || file_image == NULL) return 0;
    if (memcmp(file_image, "RIFF", 4) != 0)
    {
        /* Not WAV (Revenant speech is MP3): Miles uses an ASI codec, here AudioToolbox */
        int16_t *decoded;
        if (file_size == 0 || Mixer_DecodeImage((const uint8_t *)file_image, file_size, file_type_suffix, &decoded, &bytes, &rate, &chans) != 0)
        {
            LOG_ONCE("Miles: sample format not supported (%s)\n", file_type_suffix ? file_type_suffix : "?");
            return 0;
        }
        bits = 16;
        Mixer_SetData(ch, decoded, bytes, rate, bits, chans, decoded);
    }
    else
    {
        if (file_size == 0)
        {
            uint32_t riff_size;
            memcpy(&riff_size, (uint8_t *)file_image + 4, 4);
            file_size = riff_size + 8;
        }
        if (Mixer_ParseWav((const uint8_t *)file_image, file_size, &pcm, &bytes, &rate, &bits, &chans) != 0)
        {
            LOG_ONCE("Miles: sample format not supported (%s)\n", file_type_suffix ? file_type_suffix : "?");
            return 0;
        }
        Mixer_SetData(ch, pcm, bytes, rate, bits, chans, NULL);
    }
    handle_of(S)[12] = (uint32_t)((uint64_t)bytes * 1000 / ((uint64_t)rate * (bits / 8) * chans));
    if (trace_sound()) fprintf(stderr, "Miles: named sample (%s) %u bytes, %d Hz, %d bit, %d ch\n", file_type_suffix ? file_type_suffix : "?", bytes, rate, bits, chans);
    return 1;
}

EXTERN_C uint32_t AIL_sample_user_data_c(uint32_t S, uint32_t index)
{
    uint32_t *h = handle_of(S);
    return (h && index < 8) ? h[USER_DATA + index] : 0;
}

EXTERN_C void AIL_set_sample_user_data_c(uint32_t S, uint32_t index, uint32_t value)
{
    uint32_t *h = handle_of(S);
    if (h && index < 8) h[USER_DATA + index] = value;
}

EXTERN_C void AIL_stop_sample_c(uint32_t S)
{
    TRACE_CALL();
    Mixer_Pause(channel_of(S), 1);
}

EXTERN_C void AIL_resume_sample_c(uint32_t S)
{
    TRACE_CALL();
    int ch = channel_of(S);
    if (Mixer_IsPlaying(ch)) Mixer_Pause(ch, 0);
}

EXTERN_C void AIL_sample_ms_position_c(uint32_t S, void *total_milliseconds, void *current_milliseconds)
{
    uint32_t *h = handle_of(S);
    if (total_milliseconds) wr32(total_milliseconds, h ? h[12] : 0);
    if (current_milliseconds) wr32(current_milliseconds, h ? (uint32_t)(Mixer_Position((int)h[0]) * 1000.0) : 0);
}

EXTERN_C void AIL_waveOutClose_c(uint32_t drvr)
{
    TRACE_CALL();
}

/* End-of-sample callbacks. Miles calls them from its own thread; here a
 * service thread checks the samples every 5 ms and calls the guest
 * function "void __stdcall EOS(HSAMPLE S)".
 * Port change: a game whose game.h defines GAME_MSS_EOS_MAIN_THREAD gets
 * the callbacks on the main thread, from Sleep and PeekMessageA (outside
 * the game logic), and no service thread. The callback of Generals Zero
 * Hour starts the next sound and changes the lists of the audio manager;
 * on a second thread this ran at the same time as the main loop and
 * changed its data (crash after 75 s in the menu scene). */
#include <SDL.h>
#include <pthread.h>
EXTERN_C uint32_t CCALL CallX86Function(uint32_t address, int nargs, const uint32_t *args);
extern "C" void x86_deinitialize_cpu(void);

#define MAX_EOS 64
static SDL_mutex *eos_lock;
static uint32_t eos_samples[MAX_EOS];

/* calls the callbacks of the samples that stopped */
static void eos_service(void)
{
    uint32_t due[MAX_EOS];
    int n = 0;
    SDL_LockMutex(eos_lock);
    for (int i = 0; i < MAX_EOS; i++)
    {
        uint32_t *h = handle_of(eos_samples[i]);
        if (h == NULL || h[2] == 0 || !(h[1] & 1)) continue;
        if (Mixer_IsPlaying((int)h[0])) continue;
        h[1] &= ~1u;
        due[n++] = eos_samples[i];
    }
    SDL_UnlockMutex(eos_lock);
    for (int i = 0; i < n; i++)
    {
        uint32_t *h = handle_of(due[i]);
        if (h == NULL || h[2] == 0) continue;
        if (trace_sound()) fprintf(stderr, "Miles: EOS callback for sample 0x%x\n", due[i]);
        CallX86Function(h[2], 1, &due[i]);
    }
}

static int SDLCALL eos_thread(void *unused)
{
    for (;;)
    {
        SDL_Delay(5);
        eos_service();
    }
    return 0;
}

/* Called from Sleep and PeekMessageA. Does nothing on other threads, in a
 * callback, or less than 2 ms after the last check. */
EXTERN_C void Miles_ServiceCallbacks(void)
{
#ifdef GAME_MSS_EOS_MAIN_THREAD
    static int busy;
    static uint64_t last_ms;
    if (eos_lock == NULL || busy || !pthread_main_np()) return;
    uint64_t now = SDL_GetTicks64();
    if (now - last_ms < 2) return;
    last_ms = now;
    busy = 1;
    eos_service();
    busy = 0;
#endif
}

EXTERN_C uint32_t AIL_register_EOS_callback_c(uint32_t S, uint32_t EOS)
{
    TRACE_CALL();
    uint32_t *h = handle_of(S);
    uint32_t old;
    if (h == NULL) return 0;
    if (eos_lock == NULL)
    {
        eos_lock = SDL_CreateMutex();
#ifndef GAME_MSS_EOS_MAIN_THREAD
        SDL_DetachThread(SDL_CreateThread(eos_thread, "Miles EOS", NULL));
#endif
    }
    SDL_LockMutex(eos_lock);
    old = h[2];
    h[2] = EOS;
    int free_slot = -1, found = 0;
    for (int i = 0; i < MAX_EOS; i++)
    {
        if (eos_samples[i] == S) found = 1;
        if (eos_samples[i] == 0 && free_slot < 0) free_slot = i;
    }
    if (!found && free_slot >= 0) eos_samples[free_slot] = S;
    SDL_UnlockMutex(eos_lock);
    return old;
}

/* Called by AIL_release_sample_handle */
static void eos_forget(uint32_t S)
{
    if (eos_lock == NULL) return;
    SDL_LockMutex(eos_lock);
    for (int i = 0; i < MAX_EOS; i++)
    {
        if (eos_samples[i] == S) eos_samples[i] = 0;
    }
    SDL_UnlockMutex(eos_lock);
}

/* ------------------------------------------------------------ 3D sound */
/* No 3D providers: the game then uses 2D samples. A game with
   GAME_MSS_3D_PROVIDER in its game.h gets the 2D positional provider of
   "Miles 6.5" below. */

EXTERN_C uint32_t AIL_enumerate_3D_providers_65(void *next, void *dest, void *name);
EXTERN_C uint32_t AIL_open_3D_provider_65(uint32_t lib);

EXTERN_C uint32_t AIL_enumerate_3D_providers_c(void *next, void *dest, void *name)
{
    TRACE_CALL();
#ifdef GAME_MSS_3D_PROVIDER
    return AIL_enumerate_3D_providers_65(next, dest, name);
#else
    return 0;
#endif
}

EXTERN_C uint32_t AIL_open_3D_provider_c(uint32_t lib)
{
#ifdef GAME_MSS_3D_PROVIDER
    return AIL_open_3D_provider_65(lib);
#else
    return 1;   /* M3D_NOERR is 0: fail */
#endif
}

/* ================================================================ Miles 6.5 */
/*
 * The functions of Miles 6.5 (Generals Zero Hour) that older games do not
 * use: quick start-up, file callbacks (the game gives the files from its
 * archives), float volume and pan, stream callbacks, ADPCM, and 3D samples.
 *
 * 3D: there is one provider ("Miles Fast 2D Positional Audio"). A 3D
 * sample is a mixer channel; its volume and pan come from its position
 * relative to the listener (distance between the min and max distance,
 * and the side), as with the 2D positional provider of Miles.
 */

#define H_KIND      13      /* 1: 3D sample, 2: listener */
#define H_POS       14      /* 3 floats */
#define H_VOL3D     17      /* float */
#define H_MINDIST   18
#define H_MAXDIST   19
#define H_FACE      20      /* listener: 3 floats */
#define H_UP        23      /* listener: 3 floats */
#define H_RATE      26
#define H_VOL       27      /* 2D: float volume */
#define H_PAN       28      /* 2D: float pan */
#define H_LOOPS     29
#define H_OCCL      30

#define KIND_3D       1
#define KIND_LISTENER 2

static float hf(uint32_t *h, int i) { float f; memcpy(&f, &h[i], 4); return f; }
static void hset(uint32_t *h, int i, float f) { memcpy(&h[i], &f, 4); }
static float fbits(uint32_t v) { float f; memcpy(&f, &v, 4); return f; }

static int vol127(float v) { if (v < 0) v = 0; if (v > 1) v = 1; return (int)(v * 127.0f + 0.5f); }

/* ------------------------------------------------- images: PCM, IMA ADPCM, MP3 */

static const int ima_steps[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724,
    796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
    20350, 22385, 24623, 27086, 29794, 32767 };
static const int ima_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

/* Microsoft IMA ADPCM (WAVE format 0x11) to 16-bit PCM */
static int16_t *ima_decode(const uint8_t *data, uint32_t len, int chans, int block_align, uint32_t *out_bytes)
{
    if (chans < 1 || chans > 2 || block_align < 4 * chans) return NULL;
    int per_block = (block_align - 4 * chans) * 8 / (4 * chans) + 1;
    uint32_t blocks = (len + block_align - 1) / block_align;
    int16_t *out = (int16_t *)malloc((size_t)blocks * per_block * chans * 2 + 4);
    if (out == NULL) return NULL;
    size_t n = 0;
    for (uint32_t b = 0; b < blocks; b++)
    {
        const uint8_t *p = data + (size_t)b * block_align;
        uint32_t avail = len - b * block_align;
        if (avail > (uint32_t)block_align) avail = block_align;
        if (avail < (uint32_t)(4 * chans)) break;
        int pred[2], idx[2];
        for (int c = 0; c < chans; c++)
        {
            pred[c] = (int16_t)(p[4 * c] | (p[4 * c + 1] << 8));
            idx[c] = p[4 * c + 2];
            if (idx[c] > 88) idx[c] = 88;
            out[n + c] = (int16_t)pred[c];
        }
        n += chans;
        const uint8_t *d = p + 4 * chans;
        int frames = (int)((avail - 4 * chans) * 8 / (4 * chans));
        /* data: 4 bytes (8 samples) of channel 0, then 4 bytes of channel 1, ... */
        for (int f = 0; f < frames; f += 8)
        {
            for (int c = 0; c < chans; c++)
            {
                for (int k = 0; k < 8 && f + k < frames; k++)
                {
                    int byte = d[(f / 8) * 4 * chans + c * 4 + k / 2];
                    int nib = (k & 1) ? (byte >> 4) : (byte & 15);
                    int step = ima_steps[idx[c]];
                    int diff = step >> 3;
                    if (nib & 1) diff += step >> 2;
                    if (nib & 2) diff += step >> 1;
                    if (nib & 4) diff += step;
                    if (nib & 8) pred[c] -= diff; else pred[c] += diff;
                    if (pred[c] > 32767) pred[c] = 32767;
                    if (pred[c] < -32768) pred[c] = -32768;
                    idx[c] += ima_index[nib];
                    if (idx[c] < 0) idx[c] = 0;
                    if (idx[c] > 88) idx[c] = 88;
                    out[n + (size_t)k * chans + c] = (int16_t)pred[c];
                }
            }
            int got = (frames - f < 8) ? frames - f : 8;
            n += (size_t)got * chans;
        }
    }
    *out_bytes = (uint32_t)(n * 2);
    return out;
}

/* WAV header facts: format, channels, rate, bits, block align, data */
typedef struct { int format, chans, rate, bits, block; const uint8_t *data; uint32_t len; } wav_info;

static int wav_parse(const uint8_t *img, uint32_t size, wav_info *w)
{
    uint32_t pos = 12;
    int have_fmt = 0;
    memset(w, 0, sizeof(*w));
    if (size < 12 || memcmp(img, "RIFF", 4) != 0 || memcmp(img + 8, "WAVE", 4) != 0) return -1;
    while (pos + 8 <= size)
    {
        uint32_t len;
        memcpy(&len, img + pos + 4, 4);
        if (memcmp(img + pos, "fmt ", 4) == 0 && len >= 16)
        {
            w->format = img[pos + 8] | (img[pos + 9] << 8);
            w->chans = img[pos + 10] | (img[pos + 11] << 8);
            memcpy(&w->rate, img + pos + 12, 4);
            w->block = img[pos + 20] | (img[pos + 21] << 8);
            w->bits = img[pos + 22] | (img[pos + 23] << 8);
            have_fmt = 1;
        }
        else if (memcmp(img + pos, "data", 4) == 0 && have_fmt)
        {
            w->data = img + pos + 8;
            w->len = (pos + 8 + len <= size) ? len : size - pos - 8;
            return 0;
        }
        pos += 8 + len + (len & 1);
    }
    return -1;
}

/* Put an image into the channel of S. Returns 1 if it plays. */
static int mss_set_image(uint32_t S, const uint8_t *img, uint32_t size, const char *hint)
{
    uint32_t *h = handle_of(S);
    if (h == NULL || img == NULL) return 0;
    int ch = (int)h[0];
    wav_info w;
    int16_t *pcm16 = NULL;
    uint32_t bytes = 0;
    int rate = 0, chans = 0, bits = 16;
    if (size == 0 && memcmp(img, "RIFF", 4) == 0)
    {
        uint32_t riff;
        memcpy(&riff, img + 4, 4);
        size = riff + 8;
    }
    if (wav_parse(img, size, &w) == 0 && w.format == 1)
    {
        Mixer_SetData(ch, w.data, w.len, w.rate, w.bits, w.chans, NULL);
        bytes = w.len; rate = w.rate; chans = w.chans; bits = w.bits;
    }
    else if (w.format == 0x11)
    {
        pcm16 = ima_decode(w.data, w.len, w.chans, w.block, &bytes);
        if (pcm16 == NULL) return 0;
        rate = w.rate; chans = w.chans;
        Mixer_SetData(ch, pcm16, bytes, rate, 16, chans, pcm16);
    }
    else
    {
        if (size == 0 || Mixer_DecodeImage(img, size, hint, &pcm16, &bytes, &rate, &chans) != 0)
        {
            LOG_ONCE("Miles: sound format not supported (WAV format 0x%x)\n", w.format);
            return 0;
        }
        Mixer_SetData(ch, pcm16, bytes, rate, 16, chans, pcm16);
    }
    if (rate > 0 && chans > 0 && bits > 0)
        h[12] = (uint32_t)((uint64_t)bytes * 1000 / ((uint64_t)rate * (bits / 8) * chans));
    h[H_RATE] = (uint32_t)rate;
    return 1;
}

/* ------------------------------------------------- start-up */

EXTERN_C void *AIL_set_redist_directory_c(void *dir)
{
    static char *cur;
    if (cur == NULL)
    {
        cur = (char *)x86_malloc(2);
        strcpy(cur, ".");
    }
    return cur;
}

EXTERN_C uint32_t AIL_quick_startup_c(uint32_t use_digital, uint32_t use_MIDI, uint32_t rate, uint32_t bits, uint32_t chans)
{
    TRACE_CALL();
    if (use_digital && Mixer_Init() != 0)
    {
        fprintf(stderr, "Miles: no audio device, sound is off\n");
        return 0;
    }
    return 1;
}

EXTERN_C void AIL_quick_handles_c(void *pdig, void *pmdi, void *pdls)
{
    if (pdig) wr32(pdig, DIGITAL_DRIVER_HANDLE);
    if (pmdi) wr32(pmdi, 0);
    if (pdls) wr32(pdls, 0);
}

EXTERN_C void AIL_get_DirectSound_info_c(uint32_t S, void *lplpDS, void *lplpDSB)
{
    if (lplpDS) wr32(lplpDS, 0);
    if (lplpDSB) wr32(lplpDSB, 0);
}

/* ------------------------------------------------- file callbacks */

static uint32_t cb_close, cb_seek, cb_read;

EXTERN_C void AIL_set_file_callbacks_c(uint32_t opencb, uint32_t closecb, uint32_t seekcb, uint32_t readcb)
{
    cb_open = opencb;
    cb_close = closecb;
    cb_seek = seekcb;
    cb_read = readcb;
}

/* The whole file through the game's callbacks (host memory, or NULL) */
static uint8_t *read_by_callbacks(const char *filename, uint32_t *size_out)
{
    if (cb_open == 0 || cb_read == 0 || cb_seek == 0) return NULL;
    char *gname = (char *)x86_malloc((unsigned)strlen(filename) + 1);
    uint32_t *gh = (uint32_t *)x86_malloc(4);
    strcpy(gname, filename);
    *gh = 0;
    uint32_t args[3] = { to_guest(gname), to_guest(gh), 0 };
    uint32_t ok = CallX86Function(cb_open, 2, args);
    uint32_t fh = *gh;
    x86_free(gname);
    x86_free(gh);
    if (!ok) return NULL;
    uint32_t sargs[3] = { fh, 0, 2 };   /* seek to the end */
    uint32_t size = CallX86Function(cb_seek, 3, sargs);
    sargs[2] = 0;                       /* back to the start */
    CallX86Function(cb_seek, 3, sargs);
    uint8_t *gbuf = (uint8_t *)x86_malloc(size ? size : 1);
    uint32_t rargs[3] = { fh, to_guest(gbuf), size };
    uint32_t got = CallX86Function(cb_read, 3, rargs);
    uint8_t *buf = (uint8_t *)malloc(got ? got : 1);
    memcpy(buf, gbuf, got);
    x86_free(gbuf);
    if (cb_close) CallX86Function(cb_close, 1, &fh);
    *size_out = got;
    return buf;
}

/* Streams of Miles 6.5: the file comes through the callbacks if they are
   set, else from the disk (AIL_open_stream above). */
static uint32_t open_stream_65(const char *filename)
{
    uint32_t size = 0;
    uint8_t *img = read_by_callbacks(filename, &size);
    if (img == NULL) return 0;
    int ch = Mixer_Allocate();
    if (ch == 0) { free(img); return 0; }
    uint32_t *h = (uint32_t *)new_handle(ch);
    if (h == NULL) { Mixer_Release(ch); free(img); return 0; }
    const char *dot = strrchr(filename, '.');
    if (!mss_set_image(to_guest(h), img, size, dot))
    {
        Mixer_Release(ch);
        h[3] = 0;
        x86_free(h);
        free(img);
        return 0;
    }
    /* the mixer keeps PCM it decoded; a PCM WAV plays from img */
    wav_info w;
    if (wav_parse(img, size, &w) == 0 && w.format == 1)
    {
        Mixer_SetData(ch, w.data, w.len, w.rate, w.bits, w.chans, img);   /* the mixer frees img */
    }
    else
    {
        free(img);
    }
    hset(h, H_VOL, 1.0f);
    hset(h, H_PAN, 0.5f);
    if (trace_sound()) fprintf(stderr, "Miles: stream %s (%u bytes, through the game's file callbacks)\n", filename, size);
    return to_guest(h);
}


/* ------------------------------------------------- volume, pan, rate */

EXTERN_C void AIL_set_sample_volume_pan_c(uint32_t S, uint32_t volume, uint32_t pan)
{
    uint32_t *h = handle_of(S);
    if (h == NULL) return;
    hset(h, H_VOL, fbits(volume));
    hset(h, H_PAN, fbits(pan));
    Mixer_SetVolume((int)h[0], vol127(fbits(volume)));
    Mixer_SetPan((int)h[0], vol127(fbits(pan)));
}

EXTERN_C void AIL_sample_volume_pan_c(uint32_t S, void *volume, void *pan)
{
    uint32_t *h = handle_of(S);
    if (volume) wr32(volume, h ? h[H_VOL] : 0);
    if (pan) wr32(pan, h ? h[H_PAN] : 0);
}

EXTERN_C void AIL_set_stream_volume_pan_c(uint32_t stream, uint32_t volume, uint32_t pan)
{
    AIL_set_sample_volume_pan_c(stream, volume, pan);
}

EXTERN_C void AIL_stream_volume_pan_c(uint32_t stream, void *volume, void *pan)
{
    AIL_sample_volume_pan_c(stream, volume, pan);
}

/* The playback rate is kept, not applied (the mixer has no resampling
   per channel); sounds play at their own rate. */
EXTERN_C void AIL_set_sample_playback_rate_c(uint32_t S, uint32_t rate)
{
    uint32_t *h = handle_of(S);
    if (h) h[H_RATE] = rate;
}

EXTERN_C uint32_t AIL_sample_playback_rate_c(uint32_t S)
{
    uint32_t *h = handle_of(S);
    return h ? h[H_RATE] : 0;
}

EXTERN_C uint32_t AIL_stream_loop_count_c(uint32_t stream)
{
    uint32_t *h = handle_of(stream);
    return h ? h[H_LOOPS] : 0;
}

EXTERN_C void AIL_stream_ms_position_c(uint32_t stream, void *total, void *current)
{
    AIL_sample_ms_position_c(stream, total, current);
}

/* The end of a stream: the same service thread as AIL_register_EOS_callback */
EXTERN_C uint32_t AIL_register_stream_callback_c(uint32_t stream, uint32_t callback)
{
    return AIL_register_EOS_callback_c(stream, callback);
}

/* Filters (reverb, ...): none */
EXTERN_C uint32_t AIL_enumerate_filters_c(void *next, void *dest, void *name) { return 0; }
EXTERN_C uint32_t AIL_set_sample_processor_c(uint32_t S, uint32_t stage, uint32_t provider) { return 0; }
EXTERN_C uint32_t AIL_set_filter_sample_preference_c(uint32_t S, void *name, void *val) { return 0; }

/* ------------------------------------------------- WAV info, ADPCM */

/* AILSOUNDINFO: format, data_ptr, data_len, rate, bits, channels, samples, block_size, initial_ptr */
EXTERN_C uint32_t AIL_WAV_info_c(void *data, void *info)
{
    wav_info w;
    uint32_t riff;
    if (data == NULL || info == NULL) return 0;
    memcpy(&riff, (uint8_t *)data + 4, 4);
    if (wav_parse((const uint8_t *)data, riff + 8, &w) != 0) return 0;
    uint8_t *i = (uint8_t *)info;
    wr32(i + 0, (uint32_t)w.format);
    wr32(i + 4, to_guest(w.data));
    wr32(i + 8, w.len);
    wr32(i + 12, (uint32_t)w.rate);
    wr32(i + 16, (uint32_t)w.bits);
    wr32(i + 20, (uint32_t)w.chans);
    uint32_t samples = 0;
    if (w.format == 1 && w.bits && w.chans) samples = w.len / (w.bits / 8) / w.chans;
    else if (w.format == 0x11 && w.block && w.chans)
        samples = (w.len / w.block) * ((w.block - 4 * w.chans) * 8 / (4 * w.chans) + 1);
    wr32(i + 24, samples);
    wr32(i + 28, (uint32_t)w.block);
    wr32(i + 32, to_guest(data));
    return 1;
}

EXTERN_C uint32_t AIL_decompress_ADPCM_c(void *info, void *outdata, void *outsize)
{
    const uint8_t *i = (const uint8_t *)info;
    uint32_t bytes = 0;
    int16_t *pcm = ima_decode((const uint8_t *)from_guest(rd32(i + 4)), rd32(i + 8), (int)rd32(i + 20), (int)rd32(i + 28), &bytes);
    if (pcm == NULL) return 0;
    /* a WAV image with 16-bit PCM, freed with AIL_mem_free_lock */
    uint8_t *g = (uint8_t *)x86_malloc(44 + bytes);
    uint32_t rate = rd32(i + 12), ch = rd32(i + 20);
    memcpy(g, "RIFF", 4); wr32(g + 4, 36 + bytes); memcpy(g + 8, "WAVEfmt ", 8); wr32(g + 16, 16);
    wr16(g + 20, 1); wr16(g + 22, (uint16_t)ch); wr32(g + 24, rate); wr32(g + 28, rate * 2 * ch);
    wr16(g + 32, (uint16_t)(2 * ch)); wr16(g + 34, 16); memcpy(g + 36, "data", 4); wr32(g + 40, bytes);
    memcpy(g + 44, pcm, bytes);
    free(pcm);
    if (outdata) wr32(outdata, to_guest(g));
    if (outsize) wr32(outsize, 44 + bytes);
    return 1;
}

/* ------------------------------------------------- quick functions */

EXTERN_C uint32_t AIL_quick_load_and_play_c(void *filename, uint32_t loop_count, uint32_t wait_request)
{
    if (filename == NULL) return 0;
    uint32_t S = AIL_allocate_sample_handle_c(DIGITAL_DRIVER_HANDLE);
    if (S == 0) return 0;
    uint32_t size = 0;
    uint8_t *img = read_by_callbacks((const char *)filename, &size);
    if (img == NULL)
    {
        AIL_release_sample_handle_c(S);
        return 0;
    }
    uint8_t *g = (uint8_t *)x86_malloc(size);
    memcpy(g, img, size);
    free(img);
    mss_set_image(S, g, size, strrchr((const char *)filename, '.'));
    Mixer_SetLoops(channel_of(S), (int)loop_count);
    AIL_start_sample_c(S);
    return S;
}

EXTERN_C void AIL_quick_set_volume_c(uint32_t audio, uint32_t volume, uint32_t extravol)
{
    Mixer_SetVolume(channel_of(audio), vol127(fbits(volume)));
}

EXTERN_C void AIL_quick_unload_c(uint32_t audio)
{
    AIL_release_sample_handle_c(audio);
}

/* ------------------------------------------------- 3D */

static const char provider_name[] = "Miles Fast 2D Positional Audio";
static char *provider_name_guest;
#define PROVIDER_HANDLE 0x3d0001u
static uint32_t listener;   /* guest handle */

EXTERN_C uint32_t AIL_enumerate_3D_providers_65(void *next, void *dest, void *name)
{
    if (next == NULL || rd32(next) != 0) return 0;
    if (provider_name_guest == NULL)
    {
        provider_name_guest = (char *)x86_malloc(sizeof(provider_name));
        memcpy(provider_name_guest, provider_name, sizeof(provider_name));
    }
    wr32(next, 1);
    if (dest) wr32(dest, PROVIDER_HANDLE);
    if (name) wr32(name, to_guest(provider_name_guest));
    return 1;
}

EXTERN_C uint32_t AIL_open_3D_provider_65(uint32_t lib)
{
    return (lib == PROVIDER_HANDLE) ? 0 : 1;   /* M3D_NOERR */
}

EXTERN_C void AIL_close_3D_provider_c(uint32_t lib) {}
EXTERN_C void AIL_set_3D_speaker_type_c(uint32_t lib, uint32_t type) {}

EXTERN_C void *AIL_open_3D_listener_c(uint32_t lib)
{
    uint32_t *h = (uint32_t *)new_handle(0);
    if (h == NULL) return NULL;
    h[H_KIND] = KIND_LISTENER;
    hset(h, H_FACE + 2, 1.0f);   /* facing +z */
    hset(h, H_UP + 1, 1.0f);     /* up +y */
    listener = to_guest(h);
    return h;
}

EXTERN_C void AIL_close_3D_listener_c(uint32_t obj)
{
    uint32_t *h = handle_of(obj);
    if (h == NULL) return;
    if (obj == listener) listener = 0;
    h[3] = 0;
    x86_free(h);
}

/* The volume and pan of a 3D sample from its place to the listener */
static void update_3d(uint32_t *s)
{
    uint32_t *l = handle_of(listener);
    float d = 0, side = 0;
    if (l != NULL)
    {
        float rel[3], face[3], up[3], right[3];
        for (int i = 0; i < 3; i++)
        {
            rel[i] = hf(s, H_POS + i) - hf(l, H_POS + i);
            face[i] = hf(l, H_FACE + i);
            up[i] = hf(l, H_UP + i);
        }
        /* right = up x face (left-handed, as Direct3D and Miles) */
        right[0] = up[1] * face[2] - up[2] * face[1];
        right[1] = up[2] * face[0] - up[0] * face[2];
        right[2] = up[0] * face[1] - up[1] * face[0];
        d = sqrtf(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
        float rl = sqrtf(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
        if (d > 0.001f && rl > 0.001f) side = (rel[0] * right[0] + rel[1] * right[1] + rel[2] * right[2]) / (d * rl);
    }
    float mind = hf(s, H_MINDIST), maxd = hf(s, H_MAXDIST);
    if (mind <= 0) mind = 1;
    float gain = (d <= mind) ? 1.0f : mind / d;
    if (maxd > 0 && d > maxd) gain = 0;
    gain *= hf(s, H_VOL3D) * (1.0f - 0.5f * hf(s, H_OCCL));
    Mixer_SetVolume((int)s[0], vol127(gain));
    Mixer_SetPan((int)s[0], vol127(0.5f + 0.5f * side));
}

static void update_all_3d(void);

EXTERN_C void *AIL_allocate_3D_sample_handle_c(uint32_t lib)
{
    uint32_t S = AIL_allocate_sample_handle_c(DIGITAL_DRIVER_HANDLE);
    uint32_t *h = handle_of(S);
    if (h == NULL) return NULL;
    h[H_KIND] = KIND_3D;
    hset(h, H_VOL3D, 1.0f);
    hset(h, H_MINDIST, 1.0f);
    hset(h, H_MAXDIST, 1000.0f);
    return h;
}

EXTERN_C void AIL_release_3D_sample_handle_c(uint32_t S) { AIL_release_sample_handle_c(S); }

EXTERN_C uint32_t AIL_set_3D_sample_file_c(uint32_t S, void *file_image)
{
    uint32_t *h = handle_of(S);
    if (h == NULL) return 0;
    Mixer_Stop((int)h[0]);
    Mixer_SetLoops((int)h[0], 1);
    int ok = mss_set_image(S, (const uint8_t *)file_image, 0, ".wav");
    update_3d(h);
    return (uint32_t)ok;
}

EXTERN_C void AIL_start_3D_sample_c(uint32_t S)
{
    uint32_t *h = handle_of(S);
    if (h == NULL) return;
    update_3d(h);
    AIL_start_sample_c(S);
}

EXTERN_C void AIL_stop_3D_sample_c(uint32_t S) { AIL_stop_sample_c(S); }
EXTERN_C void AIL_resume_3D_sample_c(uint32_t S) { AIL_resume_sample_c(S); }
EXTERN_C void AIL_end_3D_sample_c(uint32_t S) { AIL_end_sample_c(S); }
EXTERN_C uint32_t AIL_3D_sample_status_c(uint32_t S) { return AIL_sample_status_c(S); }
EXTERN_C void AIL_set_3D_sample_loop_count_c(uint32_t S, uint32_t loops) { AIL_set_sample_loop_count_c(S, loops); }
EXTERN_C uint32_t AIL_register_3D_EOS_callback_c(uint32_t S, uint32_t cb) { return AIL_register_EOS_callback_c(S, cb); }
EXTERN_C uint32_t AIL_3D_user_data_c(uint32_t S, uint32_t index) { return AIL_sample_user_data_c(S, index); }
EXTERN_C void AIL_set_3D_user_data_c(uint32_t S, uint32_t index, uint32_t value) { AIL_set_sample_user_data_c(S, index, value); }
EXTERN_C uint32_t AIL_3D_sample_playback_rate_c(uint32_t S) { return AIL_sample_playback_rate_c(S); }
EXTERN_C void AIL_set_3D_sample_playback_rate_c(uint32_t S, uint32_t rate) { AIL_set_sample_playback_rate_c(S, rate); }

EXTERN_C void AIL_set_3D_sample_volume_c(uint32_t S, uint32_t volume)
{
    uint32_t *h = handle_of(S);
    if (h == NULL) return;
    hset(h, H_VOL3D, fbits(volume));
    update_3d(h);
}

EXTERN_C void AIL_set_3D_sample_distances_c(uint32_t S, uint32_t max_dist, uint32_t min_dist)
{
    uint32_t *h = handle_of(S);
    if (h == NULL) return;
    hset(h, H_MAXDIST, fbits(max_dist));
    hset(h, H_MINDIST, fbits(min_dist));
    update_3d(h);
}

EXTERN_C void AIL_set_3D_sample_occlusion_c(uint32_t S, uint32_t occlusion)
{
    uint32_t *h = handle_of(S);
    if (h == NULL) return;
    hset(h, H_OCCL, fbits(occlusion));
    update_3d(h);
}

/* The samples whose place the listener changes */
#define MAX_3D 128
static uint32_t samples_3d[MAX_3D];

static void track_3d(uint32_t S)
{
    int free_slot = -1;
    for (int i = 0; i < MAX_3D; i++)
    {
        if (samples_3d[i] == S) return;
        if (free_slot < 0 && handle_of(samples_3d[i]) == NULL) free_slot = i;
    }
    if (free_slot >= 0) samples_3d[free_slot] = S;
}

static void update_all_3d(void)
{
    for (int i = 0; i < MAX_3D; i++)
    {
        uint32_t *h = handle_of(samples_3d[i]);
        if (h != NULL && h[H_KIND] == KIND_3D) update_3d(h);
    }
}

EXTERN_C void AIL_set_3D_position_c(uint32_t obj, uint32_t x, uint32_t y, uint32_t z)
{
    uint32_t *h = handle_of(obj);
    if (h == NULL) return;
    h[H_POS] = x;
    h[H_POS + 1] = y;
    h[H_POS + 2] = z;
    if (h[H_KIND] == KIND_LISTENER) update_all_3d();
    else
    {
        track_3d(obj);
        update_3d(h);
    }
}

EXTERN_C void AIL_set_3D_orientation_c(uint32_t obj, uint32_t fx, uint32_t fy, uint32_t fz, uint32_t ux, uint32_t uy, uint32_t uz)
{
    uint32_t *h = handle_of(obj);
    if (h == NULL) return;
    h[H_FACE] = fx; h[H_FACE + 1] = fy; h[H_FACE + 2] = fz;
    h[H_UP] = ux; h[H_UP + 1] = uy; h[H_UP + 2] = uz;
    if (h[H_KIND] == KIND_LISTENER) update_all_3d();
}

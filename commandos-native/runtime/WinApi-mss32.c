/*
 *  Commandos port: Miles Sound System (mss32.dll) functions that the game
 *  uses, on the native mixer (audio-mixer.c).
 *  Samples play WAV images in guest memory. Streams play WAV files from disk.
 *  There is no CD audio: AIL_redbook_open fails.
 *  MIT license, see README.md.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio-mixer.h"
#include "CLIB.h"
#include "Game-Memory.h"
#include "guest.h"
#include "WinApi.h"

#define EXTERN_C extern "C"

/* Sample and stream status values */
#define SMP_FREE    0x0001
#define SMP_DONE    0x0002
#define SMP_PLAYING 0x0004
#define SMP_STOPPED 0x0008

#define DIGITAL_DRIVER_HANDLE 0xfa00

/* COMMANDOS_TRACE_SOUND=1 logs streams and samples */
static int trace_sound(void)
{
    static int trace = -1;
    if (trace < 0) trace = (getenv("COMMANDOS_TRACE_SOUND") != NULL);
    return trace;
}

/* COMMANDOS_TRACE_SOUND=2 also logs each call (once per function) */
#define TRACE_CALL() do { static int seen_; if (!seen_ && trace_sound() && getenv("COMMANDOS_TRACE_SOUND")[0] == '2') { seen_ = 1; fprintf(stderr, "Miles call: %s\n", __func__); } } while (0)

/* A handle is a small guest memory block (unique guest pointer). Its first
 * dword holds the mixer channel id; the second holds flags. */
static void *new_handle(int channel)
{
    uint32_t *h = (uint32_t *)x86_malloc(16);
    if (h == NULL) return NULL;
    h[0] = (uint32_t)channel;
    h[1] = 0;   /* bit 0: started at least once */
    h[2] = 0;
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
    if (number == 0) first_open_after_setup = 1;
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
    /* The game sound set-up (0x5D76C0) calls AIL_set_preference(0, 16),
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

EXTERN_C void AIL_release_sample_handle_c(uint32_t S)
{
    TRACE_CALL();
    uint32_t *h = handle_of(S);
    if (h == NULL) return;
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
            LOG_ONCE("Miles: sample format not supported\n");
            return 0;
        }
    }
    Mixer_SetData(ch, pcm, bytes, rate, bits, chans, NULL);
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

EXTERN_C uint32_t AIL_open_stream_c(uint32_t dig, const char *filename, uint32_t stream_mem)
{
    TRACE_CALL();
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
         * WARGAME.DIR) and gives the Win32 file handle; the WAV image
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

/* ------------------------------------------------------------ CD audio */

EXTERN_C uint32_t AIL_redbook_open_c(uint32_t which) { TRACE_CALL(); return 0; }
EXTERN_C void AIL_redbook_close_c(uint32_t hand) { TRACE_CALL(); }
EXTERN_C uint32_t AIL_redbook_play_c(uint32_t hand, uint32_t startms, uint32_t endms) { TRACE_CALL(); return 0; }
EXTERN_C uint32_t AIL_redbook_position_c(uint32_t hand) { TRACE_CALL(); return 0; }
EXTERN_C uint32_t AIL_redbook_set_volume_c(uint32_t hand, uint32_t volume) { TRACE_CALL(); return 0; }
EXTERN_C uint32_t AIL_redbook_stop_c(uint32_t hand) { TRACE_CALL(); return 0; }
EXTERN_C uint32_t AIL_redbook_tracks_c(uint32_t hand) { TRACE_CALL(); return 0; }
EXTERN_C uint32_t AIL_redbook_volume_c(uint32_t hand) { TRACE_CALL(); return 0; }

EXTERN_C void AIL_redbook_track_info_c(uint32_t hand, uint32_t tracknum, void *startms, void *endms)
{
    TRACE_CALL();
    if (startms) wr32(startms, 0);
    if (endms) wr32(endms, 0);
}

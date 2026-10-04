/*
 *  Native port: Miles CD audio (AIL_redbook_*) from audio files.
 *
 *  GOG versions of old games replace the CD audio tracks with files (for
 *  Revenant: Music\Track02.ogg ... Track14.ogg). GAME_CD_TRACK_FILE in the
 *  game's game.h gives the file name pattern for track n. Without it, or
 *  without files, AIL_redbook_open fails (no CD).
 *
 *  This layer makes a virtual disc: track 1 is a short data track, then
 *  each file is one audio track, with 2 s between tracks. The macOS
 *  AudioToolbox decodes the files (Ogg Vorbis, MP3, WAV, ...) to 16-bit PCM
 *  on a worker thread; the mixer (audio-mixer.c) plays them.
 *  Trace with <GAME>_TRACE_SOUND=1.
 *  MIT license, see the README.md of the repository.
 */

#include "game-info.h"
#include <AudioToolbox/AudioToolbox.h>
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio-mixer.h"
#include "CLIB.h"
#include "guest.h"

#define REDBOOK_ERROR   0
#define REDBOOK_PLAYING 1
#define REDBOOK_PAUSED  2
#define REDBOOK_STOPPED 3

#define REDBOOK_HANDLE  0xfb00
#define MAX_TRACKS      99
#define DATA_TRACK_MS   2000
#define GAP_MS          2000

typedef struct {
    char path[4096];
    uint32_t start_ms, end_ms;   /* on the virtual disc */
} cd_track;

static cd_track tracks[MAX_TRACKS + 1];
static int track_count;          /* the highest track number */
static int disc_open;

static SDL_mutex *lock;
static int channel;
static int16_t *pcm;             /* decoded data of the playing track */
static uint32_t generation;      /* a new play or stop makes old decodes stale */
static int state = REDBOOK_STOPPED;
static int volume = 127;
static uint32_t play_start_ms;   /* disc position of the start of pcm */
static int started;              /* the decode of the current play is done and playing */

static int trace(void)
{
    static int value = -1;
    if (value < 0) value = (game_getenv("TRACE_SOUND") != NULL);
    return value;
}

static ExtAudioFileRef open_audio(const char *path)
{
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, (CFIndex)strlen(path), false);
    ExtAudioFileRef f = NULL;
    OSStatus err = ExtAudioFileOpenURL(url, &f);
    CFRelease(url);
    return (err == noErr) ? f : NULL;
}

/* Duration in ms, or 0 if the file cannot be read */
static uint32_t duration_ms(const char *path)
{
    ExtAudioFileRef f = open_audio(path);
    if (f == NULL) return 0;
    AudioStreamBasicDescription fmt;
    SInt64 frames = 0;
    UInt32 size = sizeof(fmt);
    uint32_t ms = 0;
    if (ExtAudioFileGetProperty(f, kExtAudioFileProperty_FileDataFormat, &size, &fmt) == noErr)
    {
        size = sizeof(frames);
        if (ExtAudioFileGetProperty(f, kExtAudioFileProperty_FileLengthFrames, &size, &frames) == noErr && fmt.mSampleRate > 0)
        {
            ms = (uint32_t)(frames * 1000.0 / fmt.mSampleRate);
        }
    }
    ExtAudioFileDispose(f);
    return ms;
}

static void build_disc(void)
{
#ifdef GAME_CD_TRACK_FILE
    uint32_t pos = DATA_TRACK_MS;
    tracks[1].start_ms = 0;
    tracks[1].end_ms = DATA_TRACK_MS;
    track_count = 1;
    for (int n = 2; n <= MAX_TRACKS; n++)
    {
        char name[256];
        snprintf(name, sizeof(name), GAME_CD_TRACK_FILE, n);
        if (!CLIB_FindFile(name, tracks[n].path)) break;
        uint32_t ms = duration_ms(tracks[n].path);
        if (ms == 0) break;
        pos += GAP_MS;
        tracks[n].start_ms = pos;
        tracks[n].end_ms = pos + ms;
        pos += ms;
        track_count = n;
        if (trace()) fprintf(stderr, "CD audio: track %d = %s (%u ms)\n", n, tracks[n].path, ms);
    }
#endif
}

typedef struct {
    uint32_t gen;
    int track;
    uint32_t from_ms, to_ms;    /* inside the track */
} decode_job;

static int SDLCALL decode_thread(void *arg)
{
    decode_job job = *(decode_job *)arg;
    free(arg);
    ExtAudioFileRef f = open_audio(tracks[job.track].path);
    if (f == NULL) return 0;
    AudioStreamBasicDescription out = {};
    out.mSampleRate = 44100;
    out.mFormatID = kAudioFormatLinearPCM;
    out.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    out.mBytesPerPacket = 4;
    out.mFramesPerPacket = 1;
    out.mBytesPerFrame = 4;
    out.mChannelsPerFrame = 2;
    out.mBitsPerChannel = 16;
    ExtAudioFileSetProperty(f, kExtAudioFileProperty_ClientDataFormat, sizeof(out), &out);
    SInt64 first = (SInt64)job.from_ms * 44100 / 1000;
    SInt64 last = (SInt64)job.to_ms * 44100 / 1000;
    if (first > 0) ExtAudioFileSeek(f, first);
    size_t frames = (last > first) ? (size_t)(last - first) : 0;
    int16_t *data = (int16_t *)malloc(frames * 4 + 4);
    size_t got = 0;
    while (got < frames)
    {
        AudioBufferList list;
        list.mNumberBuffers = 1;
        list.mBuffers[0].mNumberChannels = 2;
        UInt32 n = (UInt32)((frames - got > 16384) ? 16384 : frames - got);
        list.mBuffers[0].mDataByteSize = n * 4;
        list.mBuffers[0].mData = data + got * 2;
        if (ExtAudioFileRead(f, &n, &list) != noErr || n == 0) break;
        got += n;
        if (generation != job.gen) break;   /* stale: stop early */
    }
    ExtAudioFileDispose(f);

    SDL_LockMutex(lock);
    if (generation != job.gen || got == 0)
    {
        SDL_UnlockMutex(lock);
        free(data);
        return 0;
    }
    Mixer_Stop(channel);
    free(pcm);
    pcm = data;
    play_start_ms = tracks[job.track].start_ms + job.from_ms;
    Mixer_SetData(channel, pcm, (uint32_t)(got * 4), 44100, 16, 2, NULL);
    Mixer_SetLoops(channel, 1);
    Mixer_SetVolume(channel, volume);
    Mixer_Start(channel);
    started = 1;
    if (state == REDBOOK_PAUSED) Mixer_Pause(channel, 1);
    SDL_UnlockMutex(lock);
    if (trace()) fprintf(stderr, "CD audio: playing track %d from %u ms (%.1f s decoded)\n", job.track, job.from_ms, got / 44100.0);
    return 0;
}

static int track_at(uint32_t ms)
{
    for (int n = 2; n <= track_count; n++)
    {
        if (ms >= tracks[n].start_ms && ms < tracks[n].end_ms) return n;
    }
    return 0;
}

EXTERN_C uint32_t AIL_redbook_open_c(uint32_t which)
{
    if (!disc_open)
    {
        build_disc();
        if (track_count < 2) return 0;   /* no audio tracks: no CD */
        lock = SDL_CreateMutex();
        disc_open = 1;
    }
    if (channel == 0) channel = Mixer_Allocate();
    return channel ? REDBOOK_HANDLE : 0;
}

EXTERN_C uint32_t AIL_redbook_stop_c(uint32_t hand)
{
    if (!disc_open) return 0;
    SDL_LockMutex(lock);
    generation++;
    Mixer_Stop(channel);
    started = 0;
    state = REDBOOK_STOPPED;
    SDL_UnlockMutex(lock);
    return 1;
}

EXTERN_C void AIL_redbook_close_c(uint32_t hand)
{
    AIL_redbook_stop_c(hand);
}

EXTERN_C uint32_t AIL_redbook_tracks_c(uint32_t hand)
{
    return disc_open ? (uint32_t)track_count : 0;
}

EXTERN_C void AIL_redbook_track_info_c(uint32_t hand, uint32_t tracknum, void *startms, void *endms)
{
    uint32_t s = 0, e = 0;
    if (disc_open && tracknum >= 1 && (int)tracknum <= track_count)
    {
        s = tracks[tracknum].start_ms;
        e = tracks[tracknum].end_ms;
    }
    if (startms) wr32(startms, s);
    if (endms) wr32(endms, e);
}

EXTERN_C uint32_t AIL_redbook_play_c(uint32_t hand, uint32_t startms, uint32_t endms)
{
    if (!disc_open) return 0;
    int n = track_at(startms);
    if (trace()) fprintf(stderr, "CD audio: play %u..%u ms (track %d)\n", startms, endms, n);
    if (n == 0) return 0;
    uint32_t to = (endms > tracks[n].start_ms && endms < tracks[n].end_ms) ? endms : tracks[n].end_ms;
    decode_job *job = (decode_job *)malloc(sizeof(decode_job));
    SDL_LockMutex(lock);
    generation++;
    Mixer_Stop(channel);
    job->gen = generation;
    job->track = n;
    job->from_ms = startms - tracks[n].start_ms;
    job->to_ms = to - tracks[n].start_ms;
    started = 0;
    state = REDBOOK_PLAYING;
    SDL_UnlockMutex(lock);
    SDL_DetachThread(SDL_CreateThread(decode_thread, "CD audio", job));
    return 1;
}

EXTERN_C uint32_t AIL_redbook_status_c(uint32_t hand)
{
    if (!disc_open) return REDBOOK_ERROR;
    SDL_LockMutex(lock);
    int s = state;
    /* while the decode runs, the state stays "playing"; then it is
     * "playing" until the mixer reaches the end of the data */
    if (s == REDBOOK_PLAYING && started && !Mixer_IsPlaying(channel)) s = state = REDBOOK_STOPPED;
    SDL_UnlockMutex(lock);
    return (uint32_t)s;
}

EXTERN_C uint32_t AIL_redbook_position_c(uint32_t hand)
{
    if (!disc_open) return 0;
    return play_start_ms + (uint32_t)(Mixer_Position(channel) * 1000.0);
}

EXTERN_C uint32_t AIL_redbook_volume_c(uint32_t hand)
{
    return (uint32_t)volume;
}

EXTERN_C uint32_t AIL_redbook_set_volume_c(uint32_t hand, uint32_t v)
{
    volume = (v > 127) ? 127 : (int)v;
    if (disc_open) Mixer_SetVolume(channel, volume);
    return (uint32_t)volume;
}

EXTERN_C uint32_t AIL_redbook_pause_c(uint32_t hand)
{
    if (!disc_open || state != REDBOOK_PLAYING) return 0;
    state = REDBOOK_PAUSED;
    Mixer_Pause(channel, 1);
    return 1;
}

EXTERN_C uint32_t AIL_redbook_resume_c(uint32_t hand)
{
    if (!disc_open || state != REDBOOK_PAUSED) return 0;
    state = REDBOOK_PLAYING;
    Mixer_Pause(channel, 0);
    return 1;
}

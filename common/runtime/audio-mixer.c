/*
 *  Port change: software sound mixer on SDL audio.
 *  The Miles Sound System layer (WinApi-mss32.c) and the video player
 *  (WinApi-amstream.c) play PCM sounds through this mixer.
 *  MIT license, see README.md.
 */

#include <SDL.h>
#include <stdint.h>
#include <stdlib.h>
#include <alloca.h>
#include <string.h>
#include "audio-mixer.h"

#define MAX_CHANNELS 48
#define OUT_RATE 44100

typedef struct {
    int used;
    int playing;
    int paused;
    const uint8_t *data;    /* PCM frames */
    void *owned;            /* freed when the channel is released */
    uint32_t frames;
    int bits, chans, rate;
    double pos, step;
    int loops;              /* 0: loop for ever */
    int volume;             /* 0..127 */
    int pan;                /* 0..127, 64 is center */
} channel_t;

static SDL_AudioDeviceID device;
static channel_t channels[MAX_CHANNELS];
static int master_volume = 127;

static inline int sample_at(const channel_t *c, uint32_t frame, int ch)
{
    if (c->chans == 1) ch = 0;
    if (c->bits == 8)
    {
        return ((int)c->data[frame * c->chans + ch] - 128) << 8;
    }
    else
    {
        int16_t v;
        memcpy(&v, c->data + 2 * (frame * c->chans + ch), 2);
        return v;
    }
}

static void SDLCALL mix_callback(void *userdata, Uint8 *stream, int len)
{
    int frames = len / 4;
    int32_t *acc = (int32_t *)alloca(frames * 2 * sizeof(int32_t));
    int16_t *out = (int16_t *)stream;
    int i, n;

    memset(acc, 0, frames * 2 * sizeof(int32_t));

    for (n = 0; n < MAX_CHANNELS; n++)
    {
        channel_t *c = &channels[n];
        int lvol, rvol;

        if (!c->used || !c->playing || c->paused || c->frames == 0) continue;

        /* volume 0..127 and pan 0..127 to two gains in 1/16384 units */
        lvol = c->volume * master_volume * (c->pan <= 64 ? 64 : 127 - c->pan) / 64;
        rvol = c->volume * master_volume * (c->pan >= 64 ? 64 : c->pan) / 64;
        lvol = lvol * 16384 / (127 * 127);
        rvol = rvol * 16384 / (127 * 127);

        for (i = 0; i < frames; i++)
        {
            uint32_t f = (uint32_t)c->pos;
            double frac = c->pos - f;
            uint32_t f2 = f + 1;
            int l, r;

            if (f2 >= c->frames) f2 = (c->loops != 1) ? 0 : f;
            l = (int)(sample_at(c, f, 0) * (1.0 - frac) + sample_at(c, f2, 0) * frac);
            r = (int)(sample_at(c, f, 1) * (1.0 - frac) + sample_at(c, f2, 1) * frac);
            acc[2 * i] += (l * lvol) >> 14;
            acc[2 * i + 1] += (r * rvol) >> 14;

            c->pos += c->step;
            if (c->pos >= c->frames)
            {
                if (c->loops == 1)
                {
                    c->playing = 0;
                    break;
                }
                if (c->loops > 1) c->loops--;
                c->pos -= c->frames;
            }
        }
    }

    for (i = 0; i < frames * 2; i++)
    {
        int32_t v = acc[i];
        if (v > 32767) v = 32767;
        else if (v < -32768) v = -32768;
        out[i] = (int16_t)v;
    }
}

int Mixer_Init(void)
{
    SDL_AudioSpec want, have;

    if (device) return 0;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return -1;

    memset(&want, 0, sizeof(want));
    want.freq = OUT_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = mix_callback;
    device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (device == 0) return -1;
    SDL_PauseAudioDevice(device, 0);
    return 0;
}

void Mixer_Shutdown(void)
{
    int n;
    if (!device) return;
    SDL_CloseAudioDevice(device);
    device = 0;
    for (n = 0; n < MAX_CHANNELS; n++)
    {
        if (channels[n].owned) free(channels[n].owned);
        memset(&channels[n], 0, sizeof(channel_t));
    }
}

static int valid(int id)
{
    return id > 0 && id <= MAX_CHANNELS && channels[id - 1].used;
}

int Mixer_Allocate(void)
{
    int n, id = 0;
    if (!device) return 0;
    SDL_LockAudioDevice(device);
    for (n = 0; n < MAX_CHANNELS; n++)
    {
        if (!channels[n].used)
        {
            memset(&channels[n], 0, sizeof(channel_t));
            channels[n].used = 1;
            channels[n].volume = 127;
            channels[n].pan = 64;
            channels[n].loops = 1;
            id = n + 1;
            break;
        }
    }
    SDL_UnlockAudioDevice(device);
    return id;
}

void Mixer_Release(int id)
{
    void *owned = NULL;
    if (!valid(id)) return;
    SDL_LockAudioDevice(device);
    owned = channels[id - 1].owned;
    memset(&channels[id - 1], 0, sizeof(channel_t));
    SDL_UnlockAudioDevice(device);
    if (owned) free(owned);
}

void Mixer_SetData(int id, const void *pcm, uint32_t bytes, int rate, int bits, int chans, void *owned)
{
    channel_t *c;
    void *old;
    if (!valid(id)) return;
    SDL_LockAudioDevice(device);
    c = &channels[id - 1];
    old = c->owned;
    c->data = (const uint8_t *)pcm;
    c->owned = owned;
    c->bits = (bits == 8) ? 8 : 16;
    c->chans = (chans == 2) ? 2 : 1;
    c->rate = (rate > 0) ? rate : 22050;
    c->frames = bytes / (c->chans * c->bits / 8);
    c->step = (double)c->rate / OUT_RATE;
    c->pos = 0;
    c->playing = 0;
    c->paused = 0;
    SDL_UnlockAudioDevice(device);
    if (old && old != owned) free(old);
}

void Mixer_Start(int id)
{
    if (!valid(id)) return;
    SDL_LockAudioDevice(device);
    channels[id - 1].pos = 0;
    channels[id - 1].playing = 1;
    channels[id - 1].paused = 0;
    SDL_UnlockAudioDevice(device);
}

void Mixer_Stop(int id)
{
    if (!valid(id)) return;
    SDL_LockAudioDevice(device);
    channels[id - 1].playing = 0;
    SDL_UnlockAudioDevice(device);
}

void Mixer_Pause(int id, int paused)
{
    if (!valid(id)) return;
    SDL_LockAudioDevice(device);
    channels[id - 1].paused = paused;
    SDL_UnlockAudioDevice(device);
}

int Mixer_IsPlaying(int id)
{
    return valid(id) && channels[id - 1].playing;
}

int Mixer_IsPaused(int id)
{
    return valid(id) && channels[id - 1].paused;
}

void Mixer_SetVolume(int id, int volume)
{
    if (!valid(id)) return;
    if (volume < 0) volume = 0;
    if (volume > 127) volume = 127;
    channels[id - 1].volume = volume;
}

int Mixer_GetVolume(int id)
{
    return valid(id) ? channels[id - 1].volume : 0;
}

void Mixer_SetPan(int id, int pan)
{
    if (!valid(id)) return;
    if (pan < 0) pan = 0;
    if (pan > 127) pan = 127;
    channels[id - 1].pan = pan;
}

int Mixer_GetPan(int id)
{
    return valid(id) ? channels[id - 1].pan : 64;
}

void Mixer_SetLoops(int id, int loops)
{
    if (!valid(id)) return;
    channels[id - 1].loops = (loops < 0) ? 1 : loops;
}

double Mixer_Position(int id)
{
    double t;
    if (!valid(id) || channels[id - 1].rate == 0) return 0;
    SDL_LockAudioDevice(device);
    t = channels[id - 1].pos / channels[id - 1].rate;
    SDL_UnlockAudioDevice(device);
    return t;
}

void Mixer_SetMaster(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 127) volume = 127;
    master_volume = volume;
}

int Mixer_GetMaster(void)
{
    return master_volume;
}

/* Parse a RIFF WAVE image (PCM only). Returns 0 on success. */
int Mixer_ParseWav(const uint8_t *img, uint32_t size, const uint8_t **pcm, uint32_t *bytes, int *rate, int *bits, int *chans)
{
    uint32_t pos = 12;
    int have_fmt = 0;

    if (size < 12 || memcmp(img, "RIFF", 4) != 0 || memcmp(img + 8, "WAVE", 4) != 0) return -1;
    while (pos + 8 <= size)
    {
        uint32_t len;
        memcpy(&len, img + pos + 4, 4);
        if (memcmp(img + pos, "fmt ", 4) == 0 && len >= 16)
        {
            uint16_t fmt, ch, b;
            uint32_t r;
            memcpy(&fmt, img + pos + 8, 2);
            memcpy(&ch, img + pos + 10, 2);
            memcpy(&r, img + pos + 12, 4);
            memcpy(&b, img + pos + 22, 2);
            if (fmt != 1) return -1;
            *chans = ch;
            *rate = (int)r;
            *bits = b;
            have_fmt = 1;
        }
        else if (memcmp(img + pos, "data", 4) == 0 && have_fmt)
        {
            *pcm = img + pos + 8;
            *bytes = (pos + 8 + len <= size) ? len : size - pos - 8;
            return 0;
        }
        pos += 8 + len + (len & 1);
    }
    return -1;
}

/*
 *  Port change: software sound mixer on SDL audio.
 *  Channel ids are 1..48 (0 is no channel). Volume and pan are 0..127.
 *  Loops: 0 plays for ever, 1 plays once, n plays n times.
 *  MIT license, see README.md.
 */
#ifndef NATIVE_AUDIO_MIXER_H
#define NATIVE_AUDIO_MIXER_H

#include <stdint.h>

int Mixer_Init(void);
void Mixer_Shutdown(void);
int Mixer_Allocate(void);
void Mixer_Release(int id);
/* pcm stays owned by the caller, except "owned" (freed by the mixer) */
void Mixer_SetData(int id, const void *pcm, uint32_t bytes, int rate, int bits, int chans, void *owned);
void Mixer_Start(int id);
void Mixer_Stop(int id);
void Mixer_Pause(int id, int paused);
int Mixer_IsPlaying(int id);
int Mixer_IsPaused(int id);
void Mixer_SetVolume(int id, int volume);
int Mixer_GetVolume(int id);
void Mixer_SetPan(int id, int pan);
int Mixer_GetPan(int id);
void Mixer_SetLoops(int id, int loops);
double Mixer_Position(int id);   /* seconds from the start of the data */
void Mixer_SetMaster(int volume);
int Mixer_GetMaster(void);
/* MP3 and other compressed images (audio-decode.c): *pcm is malloc'ed 16-bit data */
int Mixer_DecodeImage(const uint8_t *img, uint32_t size, const char *type_hint,
                      int16_t **pcm, uint32_t *bytes, int *rate, int *chans);
int Mixer_ParseWav(const uint8_t *img, uint32_t size, const uint8_t **pcm, uint32_t *bytes, int *rate, int *bits, int *chans);

#endif

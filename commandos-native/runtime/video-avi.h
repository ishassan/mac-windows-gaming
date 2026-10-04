/*
 *  Commandos port: AVI reader (Cinepak video, MS ADPCM or PCM audio).
 *  MIT license, see README.md.
 */
#ifndef COMMANDOS_VIDEO_AVI_H
#define COMMANDOS_VIDEO_AVI_H

#include <stdint.h>

typedef struct avi_s avi_t;

avi_t *Avi_Open(const char *path);   /* host path; decodes all the audio */
void Avi_Close(avi_t *a);
int Avi_Width(const avi_t *a);
int Avi_Height(const avi_t *a);
double Avi_Fps(const avi_t *a);
int Avi_Frames(const avi_t *a);
int Avi_NextFrame(const avi_t *a);   /* index of the frame Avi_DecodeNext decodes */
int Avi_DecodeNext(avi_t *a);        /* 0 = ok, -1 = end or error */
void Avi_Rewind(avi_t *a);
const uint8_t *Avi_FrameRGB(const avi_t *a);   /* RGB24 */
const int16_t *Avi_Audio(const avi_t *a, uint32_t *bytes, int *rate, int *chans);

#endif

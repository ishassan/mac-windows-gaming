/*
 *  Port change: the DirectShow multimedia stream objects that the game
 *  uses to play its AVI videos (CLSID_AMMultiMediaStream from amstream.dll):
 *  IAMMultiMediaStream, IDirectDrawMediaStream and IDirectDrawStreamSample.
 *  The video is decoded by video-avi.c into the game's DirectDraw surface.
 *  The sound plays through the mixer (audio-mixer.c).
 *  MIT license, see README.md.
 */

#include "game-info.h"
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio-mixer.h"
#include "video-avi.h"
#include "CLIB.h"
#include "Game-Memory.h"
#include "guest.h"

#ifndef EXTERN_C
#define EXTERN_C extern "C"
#endif

#define S_OK                 0
#define S_FALSE              1
#define E_NOTIMPL            0x80004001u
#define E_NOINTERFACE        0x80004002u
#define E_POINTER            0x80004003u
#define E_FAIL               0x80004005u
#define E_INVALIDARG         0x80070057u
#define MS_S_ENDOFSTREAM     0x00040003u
#define MS_E_NOSTREAM        0x80040410u
#define VFW_E_NOT_FOUND      0x80040216u

#define STREAMSTATE_STOP 0
#define STREAMSTATE_RUN  1

/* first dword of the GUIDs */
#define GUID_MSPID_PrimaryVideo        0xa35ff56au
#define GUID_MSPID_PrimaryAudio        0xa35ff56bu
#define GUID_IID_IDirectDrawMediaStream 0xf4104fceu
#define GUID_IID_IMediaStream          0xb502d1bdu
#define GUID_IID_IAMMultiMediaStream   0xbebe595cu
#define GUID_IID_IMultiMediaStream     0xb502d1bcu

EXTERN_C uint8_t IAMMultiMediaStreamVtbl_asm2c[];
EXTERN_C uint8_t IDirectDrawMediaStreamVtbl_asm2c[];
EXTERN_C uint8_t IDirectDrawStreamSampleVtbl_asm2c[];

EXTERN_C int DDraw_SurfaceInfo(uint32_t guest_surface, uint8_t **pixels, int *pitch, int *width, int *height, int *bpp);
EXTERN_C uint32_t DDraw_SurfaceAddRef(uint32_t guest_surface);
EXTERN_C int DDraw_DisplayBpp(void);
EXTERN_C uint32_t IDirectDrawSurface_Release_c(void *lpThis);

struct mstream_s;

/* objects live in guest memory; the first field is the vtable */
typedef struct {
    uint32_t lpVtbl;
    uint32_t RefCount;
    struct mmstream_s *owner;
    uint32_t purpose;               /* first dword of the purpose GUID */
} mstream_t;

typedef struct mmstream_s {
    uint32_t lpVtbl;
    uint32_t RefCount;
    mstream_t *video, *audio;
    avi_t *avi;
    int state;
    int channel;                    /* mixer channel of the sound, or 0 */
    uint32_t start_ticks;
    int ended;
} mmstream_t;

typedef struct {
    uint32_t lpVtbl;
    uint32_t RefCount;
    mstream_t *stream;
    uint32_t surface;               /* guest IDirectDrawSurface */
} sample_t;

static mmstream_t *open_stream;     /* the stream that plays now */

/* ---------------------------------------------------------- helpers */

static double stream_clock(mmstream_t *m)
{
    if (m->channel && Mixer_IsPlaying(m->channel)) return Mixer_Position(m->channel);
    return (SDL_GetTicks() - m->start_ticks) / 1000.0;
}

static void stop_sound(mmstream_t *m)
{
    if (m->channel)
    {
        Mixer_Release(m->channel);
        m->channel = 0;
    }
}

static void start_sound(mmstream_t *m)
{
    const int16_t *pcm;
    uint32_t bytes;
    int rate, chans;

    stop_sound(m);
    if (m->avi == NULL || m->audio == NULL) return;
    pcm = Avi_Audio(m->avi, &bytes, &rate, &chans);
    if (pcm == NULL || bytes == 0) return;
    if (Mixer_Init() != 0) return;
    m->channel = Mixer_Allocate();
    if (m->channel == 0) return;
    Mixer_SetData(m->channel, pcm, bytes, rate, 16, chans, NULL);
    Mixer_SetLoops(m->channel, 1);
    Mixer_Start(m->channel);
}

static void copy_frame_to_surface(mmstream_t *m, uint32_t surface)
{
    uint8_t *pixels;
    int pitch, sw, sh, bpp, w, h, x, y;
    const uint8_t *rgb;

    if (DDraw_SurfaceInfo(surface, &pixels, &pitch, &sw, &sh, &bpp) != 0) return;
    rgb = Avi_FrameRGB(m->avi);
    w = Avi_Width(m->avi);
    h = Avi_Height(m->avi);
    if (w > sw) w = sw;
    if (h > sh) h = sh;

    for (y = 0; y < h; y++)
    {
        const uint8_t *src = rgb + 3 * y * Avi_Width(m->avi);
        uint8_t *dst = pixels + y * pitch;
        if (bpp == 16)
        {
            uint16_t *d = (uint16_t *)dst;
            for (x = 0; x < w; x++, src += 3)
                d[x] = (uint16_t)(((src[0] >> 3) << 11) | ((src[1] >> 2) << 5) | (src[2] >> 3));
        }
        else if (bpp == 32)
        {
            uint32_t *d = (uint32_t *)dst;
            for (x = 0; x < w; x++, src += 3)
                d[x] = 0xff000000u | (src[0] << 16) | (src[1] << 8) | src[2];
        }
        else if (bpp == 24)
        {
            for (x = 0; x < w; x++, src += 3)
            {
                dst[3 * x] = src[2];
                dst[3 * x + 1] = src[1];
                dst[3 * x + 2] = src[0];
            }
        }
    }
}

static mstream_t *new_media_stream(mmstream_t *owner, uint32_t purpose)
{
    mstream_t *s = (mstream_t *)x86_calloc(1, sizeof(mstream_t));
    if (s == NULL) return NULL;
    s->lpVtbl = to_guest(IDirectDrawMediaStreamVtbl_asm2c);
    s->RefCount = 1;
    s->owner = owner;
    s->purpose = purpose;
    return s;
}

/* ---------------------------------------------------------- CoCreateInstance */

/* Called from CoCreateInstance_c for CLSID_AMMultiMediaStream. */
EXTERN_C uint32_t AMStream_Create(uint32_t *ppv)
{
    mmstream_t *m = (mmstream_t *)x86_calloc(1, sizeof(mmstream_t));
    if (m == NULL) return E_FAIL;
    m->lpVtbl = to_guest(IAMMultiMediaStreamVtbl_asm2c);
    m->RefCount = 1;
    *ppv = to_guest(m);
    return S_OK;
}

/* ---------------------------------------------------------- IAMMultiMediaStream */

EXTERN_C uint32_t IAMMultiMediaStream_QueryInterface_c(mmstream_t *m, uint32_t *riid, uint32_t *ppv)
{
    if (ppv == NULL) return E_POINTER;
    if (riid && (riid[0] == GUID_IID_IAMMultiMediaStream || riid[0] == GUID_IID_IMultiMediaStream || riid[0] == 0))
    {
        m->RefCount++;
        *ppv = to_guest(m);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

EXTERN_C uint32_t IAMMultiMediaStream_AddRef_c(mmstream_t *m) { return ++m->RefCount; }

EXTERN_C uint32_t IAMMultiMediaStream_Release_c(mmstream_t *m)
{
    if (--m->RefCount > 0) return m->RefCount;
    stop_sound(m);
    if (m->avi) Avi_Close(m->avi);
    if (open_stream == m) open_stream = NULL;
    /* the media stream objects may live on; they no longer have an owner */
    if (m->video) m->video->owner = NULL;
    if (m->audio) m->audio->owner = NULL;
    x86_free(m);
    return 0;
}

EXTERN_C uint32_t IAMMultiMediaStream_GetInformation_c(mmstream_t *m, uint32_t *pdwFlags, uint32_t *pStreamType)
{
    if (pdwFlags) *pdwFlags = 3;    /* MMSSF_HASCLOCK | MMSSF_SUPPORTSEEK */
    if (pStreamType) *pStreamType = 0;
    return S_OK;
}

EXTERN_C uint32_t IAMMultiMediaStream_GetMediaStream_c(mmstream_t *m, uint32_t *idPurpose, uint32_t *ppMediaStream)
{
    mstream_t *s = NULL;
    if (ppMediaStream == NULL || idPurpose == NULL) return E_POINTER;
    if (idPurpose[0] == GUID_MSPID_PrimaryVideo) s = m->video;
    else if (idPurpose[0] == GUID_MSPID_PrimaryAudio) s = m->audio;
    if (s == NULL)
    {
        *ppMediaStream = 0;
        return MS_E_NOSTREAM;
    }
    s->RefCount++;
    *ppMediaStream = to_guest(s);
    return S_OK;
}

EXTERN_C uint32_t IAMMultiMediaStream_EnumMediaStreams_c(mmstream_t *m, uint32_t Index, uint32_t *ppMediaStream)
{
    mstream_t *s = (Index == 0) ? m->video : (Index == 1 ? m->audio : NULL);
    if (ppMediaStream == NULL) return E_POINTER;
    if (s == NULL)
    {
        *ppMediaStream = 0;
        return S_FALSE;
    }
    s->RefCount++;
    *ppMediaStream = to_guest(s);
    return S_OK;
}

EXTERN_C uint32_t IAMMultiMediaStream_GetState_c(mmstream_t *m, uint32_t *pCurrentState)
{
    if (pCurrentState) *pCurrentState = (uint32_t)m->state;
    return S_OK;
}

EXTERN_C uint32_t IAMMultiMediaStream_SetState_c(mmstream_t *m, uint32_t NewState)
{
    if (NewState == STREAMSTATE_RUN && m->state != STREAMSTATE_RUN)
    {
        m->state = STREAMSTATE_RUN;
        m->ended = 0;
        if (m->avi && Avi_NextFrame(m->avi) == 0)
        {
            m->start_ticks = SDL_GetTicks();
            start_sound(m);
        }
        else
        {
            Mixer_Pause(m->channel, 0);
        }
        open_stream = m;
    }
    else if (NewState == STREAMSTATE_STOP)
    {
        m->state = STREAMSTATE_STOP;
        Mixer_Pause(m->channel, 1);
    }
    return S_OK;
}

EXTERN_C uint32_t IAMMultiMediaStream_GetTime_c(mmstream_t *m, uint32_t *pCurrentTime)
{
    /* STREAM_TIME: 100 ns units, 64 bit */
    uint64_t t = (uint64_t)(stream_clock(m) * 1e7);
    if (pCurrentTime == NULL) return E_POINTER;
    pCurrentTime[0] = (uint32_t)t;
    pCurrentTime[1] = (uint32_t)(t >> 32);
    return S_OK;
}

EXTERN_C uint32_t IAMMultiMediaStream_GetDuration_c(mmstream_t *m, uint32_t *pDuration)
{
    uint64_t t = 0;
    if (pDuration == NULL) return E_POINTER;
    if (m->avi && Avi_Fps(m->avi) > 0) t = (uint64_t)(Avi_Frames(m->avi) / Avi_Fps(m->avi) * 1e7);
    pDuration[0] = (uint32_t)t;
    pDuration[1] = (uint32_t)(t >> 32);
    return S_OK;
}

EXTERN_C uint32_t IAMMultiMediaStream_Seek_c(mmstream_t *m, uint32_t SeekTimeLow, uint32_t SeekTimeHigh)
{
    /* The game only seeks to the start. */
    if (m->avi == NULL) return E_FAIL;
    Avi_Rewind(m->avi);
    m->ended = 0;
    m->start_ticks = SDL_GetTicks();
    if (m->state == STREAMSTATE_RUN) start_sound(m);
    else stop_sound(m);
    return S_OK;
}

EXTERN_C uint32_t IAMMultiMediaStream_GetEndOfStreamEventHandle_c(mmstream_t *m, uint32_t *phEOS)
{
    if (phEOS) *phEOS = 0;
    return E_NOTIMPL;
}

EXTERN_C uint32_t IAMMultiMediaStream_Initialize_c(mmstream_t *m, uint32_t StreamType, uint32_t dwFlags, void *pFilterGraph)
{
    return S_OK;
}

EXTERN_C uint32_t IAMMultiMediaStream_GetFilterGraph_c(mmstream_t *m, uint32_t *pp)
{
    if (pp) *pp = 0;
    return E_NOTIMPL;
}

EXTERN_C uint32_t IAMMultiMediaStream_GetFilter_c(mmstream_t *m, uint32_t *pp)
{
    if (pp) *pp = 0;
    return E_NOTIMPL;
}

EXTERN_C uint32_t IAMMultiMediaStream_AddMediaStream_c(mmstream_t *m, uint32_t pStreamObject, uint32_t *PurposeId, uint32_t dwFlags, uint32_t *ppNewStream)
{
    mstream_t *s;
    if (PurposeId == NULL) return E_POINTER;
    if (PurposeId[0] != GUID_MSPID_PrimaryVideo && PurposeId[0] != GUID_MSPID_PrimaryAudio) return E_INVALIDARG;
    s = new_media_stream(m, PurposeId[0]);
    if (s == NULL) return E_FAIL;
    if (PurposeId[0] == GUID_MSPID_PrimaryVideo) m->video = s;
    else m->audio = s;
    if (ppNewStream)
    {
        s->RefCount++;
        *ppNewStream = to_guest(s);
    }
    return S_OK;
}

EXTERN_C uint32_t IAMMultiMediaStream_OpenFile_c(mmstream_t *m, const uint16_t *pszFileName, uint32_t dwFlags)
{
    char name[1024], path[4096];
    int i;

    if (pszFileName == NULL) return E_POINTER;
    for (i = 0; i < (int)sizeof(name) - 1 && pszFileName[i]; i++)
        name[i] = (char)(pszFileName[i] < 128 ? pszFileName[i] : '_');
    name[i] = 0;

    if (!CLIB_FindFile(name, path))
    {
        fprintf(stderr, "video: file not found: %s\n", name);
        return VFW_E_NOT_FOUND;
    }
    if (m->avi) Avi_Close(m->avi);
    m->avi = Avi_Open(path);
    if (m->avi == NULL)
    {
        fprintf(stderr, "video: cannot read %s\n", name);
        return E_FAIL;
    }
    m->ended = 0;
    if (game_getenv("TRACE_VIDEO"))
    {
        fprintf(stderr, "video: open %s, %dx%d, %d frames, %.2f fps\n", name, Avi_Width(m->avi), Avi_Height(m->avi), Avi_Frames(m->avi), Avi_Fps(m->avi));
        x86_print_stack_trace(200);
    }
    return S_OK;
}

EXTERN_C uint32_t IAMMultiMediaStream_OpenMoniker_c(mmstream_t *m, void *pCtx, void *pMoniker, uint32_t dwFlags) { return E_NOTIMPL; }
EXTERN_C uint32_t IAMMultiMediaStream_Render_c(mmstream_t *m, uint32_t dwFlags) { return E_NOTIMPL; }

/* ---------------------------------------------------------- IDirectDrawMediaStream */

EXTERN_C uint32_t IDirectDrawMediaStream_QueryInterface_c(mstream_t *s, uint32_t *riid, uint32_t *ppv)
{
    if (ppv == NULL) return E_POINTER;
    if (riid && (riid[0] == GUID_IID_IDirectDrawMediaStream || riid[0] == GUID_IID_IMediaStream || riid[0] == 0))
    {
        s->RefCount++;
        *ppv = to_guest(s);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

EXTERN_C uint32_t IDirectDrawMediaStream_AddRef_c(mstream_t *s) { return ++s->RefCount; }

EXTERN_C uint32_t IDirectDrawMediaStream_Release_c(mstream_t *s)
{
    if (--s->RefCount > 0) return s->RefCount;
    if (s->owner)
    {
        if (s->owner->video == s) s->owner->video = NULL;
        if (s->owner->audio == s) s->owner->audio = NULL;
    }
    x86_free(s);
    return 0;
}

EXTERN_C uint32_t IDirectDrawMediaStream_GetMultiMediaStream_c(mstream_t *s, uint32_t *pp)
{
    if (pp == NULL) return E_POINTER;
    if (s->owner == NULL)
    {
        *pp = 0;
        return E_FAIL;
    }
    s->owner->RefCount++;
    *pp = to_guest(s->owner);
    return S_OK;
}

EXTERN_C uint32_t IDirectDrawMediaStream_GetInformation_c(mstream_t *s, uint32_t *pPurposeId, uint32_t *pType)
{
    if (pPurposeId)
    {
        pPurposeId[0] = s->purpose;
        pPurposeId[1] = 0x11d09fda;
        pPurposeId[2] = 0xc0008fdf;
        pPurposeId[3] = 0x9d18d94f;
    }
    if (pType) *pType = 0;
    return S_OK;
}

EXTERN_C uint32_t IDirectDrawMediaStream_SetSameFormat_c(mstream_t *s, uint32_t other, uint32_t dwFlags) { return S_OK; }
EXTERN_C uint32_t IDirectDrawMediaStream_AllocateSample_c(mstream_t *s, uint32_t dwFlags, uint32_t *pp) { if (pp) *pp = 0; return E_NOTIMPL; }
EXTERN_C uint32_t IDirectDrawMediaStream_CreateSharedSample_c(mstream_t *s, uint32_t existing, uint32_t dwFlags, uint32_t *pp) { if (pp) *pp = 0; return E_NOTIMPL; }
EXTERN_C uint32_t IDirectDrawMediaStream_SendEndOfStream_c(mstream_t *s, uint32_t dwFlags) { return S_OK; }

EXTERN_C uint32_t IDirectDrawMediaStream_GetFormat_c(mstream_t *s, uint8_t *pDDSDCurrent, uint32_t *ppPalette, uint8_t *pDDSDDesired, uint32_t *pdwFlags)
{
    avi_t *avi = s->owner ? s->owner->avi : NULL;
    if (ppPalette) *ppPalette = 0;
    if (pdwFlags) *pdwFlags = 0;
    if (avi == NULL) return MS_E_NOSTREAM;
    if (pDDSDCurrent)
    {
        /* DDSURFACEDESC: size, flags (CAPS|HEIGHT|WIDTH), height, width,
         * caps OFFSCREENPLAIN|SYSTEMMEMORY. The surface gets the display
         * pixel format. */
        memset(pDDSDCurrent, 0, 108);
        wr32(pDDSDCurrent, 108);
        wr32(pDDSDCurrent + 4, 0x7);
        wr32(pDDSDCurrent + 8, (uint32_t)Avi_Height(avi));
        wr32(pDDSDCurrent + 12, (uint32_t)Avi_Width(avi));
        wr32(pDDSDCurrent + 104, 0x40 | 0x800);
    }
    if (pDDSDDesired && pDDSDCurrent) memcpy(pDDSDDesired, pDDSDCurrent, 108);
    return S_OK;
}

EXTERN_C uint32_t IDirectDrawMediaStream_SetFormat_c(mstream_t *s, uint8_t *pDDSD, uint32_t pPalette) { return S_OK; }
EXTERN_C uint32_t IDirectDrawMediaStream_GetDirectDraw_c(mstream_t *s, uint32_t *pp) { if (pp) *pp = 0; return E_NOTIMPL; }
EXTERN_C uint32_t IDirectDrawMediaStream_SetDirectDraw_c(mstream_t *s, uint32_t pDD) { return S_OK; }

EXTERN_C uint32_t IDirectDrawMediaStream_CreateSample_c(mstream_t *s, uint32_t pSurface, void *pRect, uint32_t dwFlags, uint32_t *ppSample)
{
    sample_t *smp;
    if (ppSample == NULL) return E_POINTER;
    smp = (sample_t *)x86_calloc(1, sizeof(sample_t));
    if (smp == NULL) return E_FAIL;
    smp->lpVtbl = to_guest(IDirectDrawStreamSampleVtbl_asm2c);
    smp->RefCount = 1;
    smp->stream = s;
    s->RefCount++;
    smp->surface = pSurface;
    if (pSurface) DDraw_SurfaceAddRef(pSurface);
    *ppSample = to_guest(smp);
    return S_OK;
}

EXTERN_C uint32_t IDirectDrawMediaStream_GetTimePerFrame_c(mstream_t *s, uint32_t *pFrameTime)
{
    uint64_t t = 0;
    avi_t *avi = s->owner ? s->owner->avi : NULL;
    if (pFrameTime == NULL) return E_POINTER;
    if (avi && Avi_Fps(avi) > 0) t = (uint64_t)(1e7 / Avi_Fps(avi));
    pFrameTime[0] = (uint32_t)t;
    pFrameTime[1] = (uint32_t)(t >> 32);
    return S_OK;
}

/* ---------------------------------------------------------- IDirectDrawStreamSample */

EXTERN_C uint32_t IDirectDrawStreamSample_QueryInterface_c(sample_t *smp, uint32_t *riid, uint32_t *ppv)
{
    if (ppv == NULL) return E_POINTER;
    smp->RefCount++;
    *ppv = to_guest(smp);
    return S_OK;
}

EXTERN_C uint32_t IDirectDrawStreamSample_AddRef_c(sample_t *smp) { return ++smp->RefCount; }

EXTERN_C uint32_t IDirectDrawStreamSample_Release_c(sample_t *smp)
{
    if (--smp->RefCount > 0) return smp->RefCount;
    if (smp->surface) IDirectDrawSurface_Release_c(from_guest(smp->surface));
    if (smp->stream) IDirectDrawMediaStream_Release_c(smp->stream);
    x86_free(smp);
    return 0;
}

EXTERN_C uint32_t IDirectDrawStreamSample_GetMediaStream_c(sample_t *smp, uint32_t *pp)
{
    if (pp == NULL) return E_POINTER;
    smp->stream->RefCount++;
    *pp = to_guest(smp->stream);
    return S_OK;
}

EXTERN_C uint32_t IDirectDrawStreamSample_GetSampleTimes_c(sample_t *smp, uint32_t *pStart, uint32_t *pEnd, uint32_t *pCurrent)
{
    if (pStart) { pStart[0] = 0; pStart[1] = 0; }
    if (pEnd) { pEnd[0] = 0; pEnd[1] = 0; }
    if (pCurrent && smp->stream->owner) IAMMultiMediaStream_GetTime_c(smp->stream->owner, pCurrent);
    return S_OK;
}

EXTERN_C uint32_t IDirectDrawStreamSample_SetSampleTimes_c(sample_t *smp, void *pStart, void *pEnd) { return E_NOTIMPL; }

/* Synchronous update: wait until the next frame is due, decode it into the
 * surface. MS_S_ENDOFSTREAM after the last frame. */
EXTERN_C uint32_t IDirectDrawStreamSample_Update_c(sample_t *smp, uint32_t dwFlags, uint32_t hEvent, uint32_t pfnAPC, uint32_t dwAPCData)
{
    mmstream_t *m = smp->stream->owner;
    double fps, now;
    int due, waited = 0;

    if (m == NULL || m->avi == NULL) return MS_S_ENDOFSTREAM;
    if (m->ended) return MS_S_ENDOFSTREAM;
    if (m->state != STREAMSTATE_RUN) return MS_S_ENDOFSTREAM;
    fps = Avi_Fps(m->avi);

    for (;;)
    {
        now = stream_clock(m);
        due = (int)(now * fps);
        if (Avi_NextFrame(m->avi) <= due || waited > 1000) break;
        SDL_Delay(2);
        waited += 2;
    }

    if (Avi_NextFrame(m->avi) >= Avi_Frames(m->avi))
    {
        m->ended = 1;
        return MS_S_ENDOFSTREAM;
    }
    /* decode every frame (inter frames need the previous image); show the
     * newest one */
    do
    {
        if (Avi_DecodeNext(m->avi) != 0) break;
    } while (Avi_NextFrame(m->avi) <= due && Avi_NextFrame(m->avi) < Avi_Frames(m->avi));

    copy_frame_to_surface(m, smp->surface);
    return S_OK;
}

EXTERN_C uint32_t IDirectDrawStreamSample_CompletionStatus_c(sample_t *smp, uint32_t dwFlags, uint32_t dwMilliseconds) { return S_OK; }

EXTERN_C uint32_t IDirectDrawStreamSample_GetSurface_c(sample_t *smp, uint32_t *ppSurface, void *pRect)
{
    if (ppSurface)
    {
        if (smp->surface) DDraw_SurfaceAddRef(smp->surface);
        *ppSurface = smp->surface;
    }
    if (pRect)
    {
        uint8_t *pixels;
        int pitch, w = 0, h = 0, bpp;
        DDraw_SurfaceInfo(smp->surface, &pixels, &pitch, &w, &h, &bpp);
        wr32(pRect, 0);
        wr32((uint8_t *)pRect + 4, 0);
        wr32((uint8_t *)pRect + 8, (uint32_t)w);
        wr32((uint8_t *)pRect + 12, (uint32_t)h);
    }
    return S_OK;
}

EXTERN_C uint32_t IDirectDrawStreamSample_SetRect_c(sample_t *smp, void *pRect) { return S_OK; }

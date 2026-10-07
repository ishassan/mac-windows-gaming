/*
 *  Port change: Bink video (binkw32.dll) on FFmpeg (libavformat reads the
 *  .bik file, libavcodec decodes the Bink video and audio, libswscale
 *  converts the frames). FFmpeg comes from the conda env of the repository
 *  (LGPL build); the app bundle has a copy.
 *
 *  The x86 code gets a guest BINK struct; it reads Width, Height, Frames
 *  and FrameNum from it. The sound of the video is decoded at BinkOpen and
 *  plays on the mixer from the first frame.
 *
 *  Built only for games with USE_BINK=1 in game.conf.
 *  MIT license, see README.md.
 */

#include "game-info.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <SDL.h>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}
#include "audio-mixer.h"
#include "CLIB.h"
#include "Game-Memory.h"
#include "guest.h"

#ifndef EXTERN_C
#define EXTERN_C extern "C"
#endif

/* BINK struct (Bink 1.x): the fields that games read */
#define B_WIDTH     0
#define B_HEIGHT    4
#define B_FRAMES    8
#define B_FRAMENUM  12
#define B_LASTFRAME 16
#define B_RATE      20
#define B_RATEDIV   24
#define B_SIZE      512

/* BinkCopyToBuffer surface types */
#define BINKSURFACE24   1
#define BINKSURFACE24R  2
#define BINKSURFACE32   3
#define BINKSURFACE32R  4
#define BINKSURFACE32A  5
#define BINKSURFACE32RA 6
#define BINKSURFACE4444 7
#define BINKSURFACE5551 8
#define BINKSURFACE555  9
#define BINKSURFACE565  10
#define BINKSURFACEMASK 15

struct bink_video {
    uint8_t *guest;              /* the BINK struct */
    AVFormatContext *fmt;
    AVCodecContext *vctx;
    int vstream, astream;
    AVFrame *frame;              /* the current frame */
    AVPacket *pkt;
    SwsContext *sws;
    int sws_fmt, sws_w, sws_h;
    int have_frame;
    Uint64 start;                /* SDL_GetTicks64 at the first frame */
    int channel;                 /* mixer channel of the sound (0: none) */
    int sound_started;
    int volume;                  /* 0..127 */
};

static int trace(void)
{
    static int t = -1;
    if (t < 0) t = game_getenv("TRACE_VIDEO") != NULL;
    return t;
}

static bink_video *video_of(void *bnk)
{
    if (bnk == NULL) return NULL;
    uint64_t p;
    memcpy(&p, (uint8_t *)bnk + B_SIZE - 8, 8);
    return (bink_video *)(uintptr_t)p;
}

/* All sound of the file as 16-bit stereo or mono PCM */
static int decode_sound(const char *path, int stream, int16_t **pcm, uint32_t *bytes, int *rate, int *chans)
{
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, path, NULL, NULL) != 0) return -1;
    if (avformat_find_stream_info(fmt, NULL) < 0) { avformat_close_input(&fmt); return -1; }
    AVStream *st = fmt->streams[stream];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : NULL;
    if (ctx == NULL || avcodec_parameters_to_context(ctx, st->codecpar) < 0 || avcodec_open2(ctx, codec, NULL) < 0)
    {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return -1;
    }
    int out_ch = (ctx->ch_layout.nb_channels >= 2) ? 2 : 1;
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, out_ch);
    SwrContext *swr = NULL;
    swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_S16, ctx->sample_rate, &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate, 0, NULL);
    if (swr == NULL || swr_init(swr) < 0)
    {
        swr_free(&swr);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return -1;
    }
    std::vector<int16_t> out;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *fr = av_frame_alloc();
    while (av_read_frame(fmt, pkt) >= 0)
    {
        if (pkt->stream_index == stream && avcodec_send_packet(ctx, pkt) >= 0)
        {
            while (avcodec_receive_frame(ctx, fr) >= 0)
            {
                int max = swr_get_out_samples(swr, fr->nb_samples);
                size_t pos = out.size();
                out.resize(pos + (size_t)max * out_ch);
                uint8_t *dst = (uint8_t *)(out.data() + pos);
                int got = swr_convert(swr, &dst, max, (const uint8_t **)fr->extended_data, fr->nb_samples);
                out.resize(pos + (size_t)(got > 0 ? got : 0) * out_ch);
            }
        }
        av_packet_unref(pkt);
    }
    av_frame_free(&fr);
    av_packet_free(&pkt);
    *rate = ctx->sample_rate;
    *chans = out_ch;
    swr_free(&swr);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    *bytes = (uint32_t)(out.size() * 2);
    *pcm = (int16_t *)malloc(*bytes ? *bytes : 2);
    memcpy(*pcm, out.data(), *bytes);
    return 0;
}

EXTERN_C void *BinkOpen_c(const char *name, uint32_t flags)
{
    char path[4096];
    if (name == NULL || !CLIB_FindFile(name, path))
    {
        if (trace()) fprintf(stderr, "Bink: %s not found\n", name ? name : "(null)");
        return NULL;
    }
    bink_video *v = new bink_video();
    memset((void *)v, 0, sizeof(*v));
    v->vstream = v->astream = -1;
    v->volume = 127;
    if (avformat_open_input(&v->fmt, path, NULL, NULL) != 0 || avformat_find_stream_info(v->fmt, NULL) < 0)
    {
        fprintf(stderr, "Bink: cannot open %s\n", path);
        if (v->fmt) avformat_close_input(&v->fmt);
        delete v;
        return NULL;
    }
    for (unsigned i = 0; i < v->fmt->nb_streams; i++)
    {
        int type = v->fmt->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && v->vstream < 0) v->vstream = (int)i;
        if (type == AVMEDIA_TYPE_AUDIO && v->astream < 0) v->astream = (int)i;
    }
    if (v->vstream < 0)
    {
        avformat_close_input(&v->fmt);
        delete v;
        return NULL;
    }
    AVStream *vs = v->fmt->streams[v->vstream];
    const AVCodec *codec = avcodec_find_decoder(vs->codecpar->codec_id);
    v->vctx = codec ? avcodec_alloc_context3(codec) : NULL;
    if (v->vctx == NULL || avcodec_parameters_to_context(v->vctx, vs->codecpar) < 0 || avcodec_open2(v->vctx, codec, NULL) < 0)
    {
        fprintf(stderr, "Bink: no decoder for %s\n", path);
        avcodec_free_context(&v->vctx);
        avformat_close_input(&v->fmt);
        delete v;
        return NULL;
    }
    v->frame = av_frame_alloc();
    v->pkt = av_packet_alloc();

    /* the sound */
    if (v->astream >= 0)
    {
        int16_t *pcm;
        uint32_t bytes;
        int rate, chans;
        if (decode_sound(path, v->astream, &pcm, &bytes, &rate, &chans) == 0 && bytes > 0)
        {
            v->channel = Mixer_Allocate();
            if (v->channel) Mixer_SetData(v->channel, pcm, bytes, rate, 16, chans, pcm);
            else free(pcm);
        }
    }

    uint8_t *g = (uint8_t *)x86_calloc(1, B_SIZE);
    v->guest = g;
    /* the frame count is in the Bink header ("BIK", size, frames, ...) */
    uint32_t frames = 0;
    {
        FILE *f = fopen(path, "rb");
        uint8_t head[12];
        if (f != NULL)
        {
            if (fread(head, 1, 12, f) == 12 && head[0] == 'B' && head[1] == 'I' && head[2] == 'K') frames = rd32(head + 8);
            fclose(f);
        }
    }
    if (frames == 0) frames = (uint32_t)(vs->nb_frames > 0 ? vs->nb_frames : 1);
    AVRational fr = vs->avg_frame_rate.num ? vs->avg_frame_rate : vs->r_frame_rate;
    if (fr.num == 0) { fr.num = 15; fr.den = 1; }
    wr32(g + B_WIDTH, (uint32_t)v->vctx->width);
    wr32(g + B_HEIGHT, (uint32_t)v->vctx->height);
    wr32(g + B_FRAMES, frames);
    wr32(g + B_FRAMENUM, 1);
    wr32(g + B_LASTFRAME, 0);
    wr32(g + B_RATE, (uint32_t)fr.num);
    wr32(g + B_RATEDIV, (uint32_t)fr.den);
    uint64_t p = (uint64_t)(uintptr_t)v;
    memcpy(g + B_SIZE - 8, &p, 8);
    if (trace()) fprintf(stderr, "Bink: %s %dx%d, %u frames at %d/%d, sound %s\n", name, v->vctx->width, v->vctx->height,
                         frames, fr.num, fr.den, v->channel ? "yes" : "no");
    return g;
}

EXTERN_C void BinkClose_c(void *bnk)
{
    bink_video *v = video_of(bnk);
    if (v == NULL) return;
    if (trace()) fprintf(stderr, "Bink: close at frame %u of %u\n", rd32(v->guest + B_FRAMENUM), rd32(v->guest + B_FRAMES));
    if (v->channel) Mixer_Release(v->channel);
    sws_freeContext(v->sws);
    av_frame_free(&v->frame);
    av_packet_free(&v->pkt);
    avcodec_free_context(&v->vctx);
    avformat_close_input(&v->fmt);
    memset(v->guest + B_SIZE - 8, 0, 8);
    x86_free(v->guest);
    delete v;
}

/* Decode the frame FrameNum (the frames come in order) */
EXTERN_C uint32_t BinkDoFrame_c(void *bnk)
{
    bink_video *v = video_of(bnk);
    if (v == NULL) return 0;
    if (v->start == 0) v->start = SDL_GetTicks64();
    if (v->channel && !v->sound_started)
    {
        v->sound_started = 1;
        Mixer_SetVolume(v->channel, v->volume);
        Mixer_Start(v->channel);
    }
    for (;;)
    {
        int r = avcodec_receive_frame(v->vctx, v->frame);
        if (r == 0) { v->have_frame = 1; break; }
        if (r != AVERROR(EAGAIN)) break;
        /* read packets until the next video packet */
        int sent = 0;
        while (!sent)
        {
            if (av_read_frame(v->fmt, v->pkt) < 0)
            {
                avcodec_send_packet(v->vctx, NULL);   /* drain */
                sent = 1;
                break;
            }
            if (v->pkt->stream_index == v->vstream)
            {
                avcodec_send_packet(v->vctx, v->pkt);
                sent = 1;
            }
            av_packet_unref(v->pkt);
        }
    }
    wr32(v->guest + B_LASTFRAME, rd32(v->guest + B_FRAMENUM));
    return 0;
}

EXTERN_C void BinkNextFrame_c(void *bnk)
{
    bink_video *v = video_of(bnk);
    if (v == NULL) return;
    uint32_t n = rd32(v->guest + B_FRAMENUM);
    if (n < rd32(v->guest + B_FRAMES)) wr32(v->guest + B_FRAMENUM, n + 1);
    if (trace() && (n % 100 == 0 || n + 1 >= rd32(v->guest + B_FRAMES))) fprintf(stderr, "Bink: next frame %u of %u\n", n + 1, rd32(v->guest + B_FRAMES));
}

/* 1: wait (the next frame is not due yet) */
EXTERN_C uint32_t BinkWait_c(void *bnk)
{
    bink_video *v = video_of(bnk);
    if (v == NULL || v->start == 0) return 0;
    uint64_t rate = rd32(v->guest + B_RATE), div = rd32(v->guest + B_RATEDIV);
    if (rate == 0) return 0;
    uint64_t due = v->start + (uint64_t)(rd32(v->guest + B_FRAMENUM) - 1) * 1000 * div / rate;
    return SDL_GetTicks64() < due ? 1 : 0;
}

EXTERN_C void BinkGoto_c(void *bnk, uint32_t frame, uint32_t flags)
{
    bink_video *v = video_of(bnk);
    if (v == NULL) return;
    if (frame <= 1)
    {
        av_seek_frame(v->fmt, v->vstream, 0, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(v->vctx);
        wr32(v->guest + B_FRAMENUM, 1);
        v->start = 0;
        if (v->channel)
        {
            Mixer_Stop(v->channel);
            v->sound_started = 0;
        }
    }
}

static int pixel_format(uint32_t flags, int *bpp)
{
    switch (flags & BINKSURFACEMASK)
    {
        case BINKSURFACE24: *bpp = 3; return AV_PIX_FMT_BGR24;
        case BINKSURFACE24R: *bpp = 3; return AV_PIX_FMT_RGB24;
        case BINKSURFACE32: case BINKSURFACE32A: *bpp = 4; return AV_PIX_FMT_BGRA;
        case BINKSURFACE32R: case BINKSURFACE32RA: *bpp = 4; return AV_PIX_FMT_RGBA;
        case BINKSURFACE555: case BINKSURFACE5551: *bpp = 2; return AV_PIX_FMT_RGB555LE;
        case BINKSURFACE565: *bpp = 2; return AV_PIX_FMT_RGB565LE;
        case BINKSURFACE4444: *bpp = 2; return AV_PIX_FMT_RGB444LE;
        default: *bpp = 4; return AV_PIX_FMT_BGRA;
    }
}

EXTERN_C uint32_t BinkCopyToBuffer_c(void *bnk, void *dest, uint32_t destpitch, uint32_t destheight, uint32_t destx, uint32_t desty, uint32_t flags)
{
    bink_video *v = video_of(bnk);
    if (v == NULL || !v->have_frame || dest == NULL) return 0;
    int bpp;
    int fmt = pixel_format(flags, &bpp);
    int w = v->frame->width, h = v->frame->height;
    if ((uint32_t)h + desty > destheight) h = (int)(destheight - desty);
    if (h <= 0) return 0;
    if (v->sws == NULL || v->sws_fmt != fmt || v->sws_w != w || v->sws_h != v->frame->height)
    {
        sws_freeContext(v->sws);
        v->sws = sws_getContext(w, v->frame->height, (AVPixelFormat)v->frame->format, w, v->frame->height, (AVPixelFormat)fmt,
                                SWS_POINT, NULL, NULL, NULL);
        v->sws_fmt = fmt;
        v->sws_w = w;
        v->sws_h = v->frame->height;
    }
    if (v->sws == NULL) return 0;
    uint8_t *dst[4] = { (uint8_t *)dest + (size_t)desty * destpitch + (size_t)destx * bpp, NULL, NULL, NULL };
    int dst_stride[4] = { (int)destpitch, 0, 0, 0 };
    sws_scale(v->sws, v->frame->data, v->frame->linesize, 0, h, dst, dst_stride);
    if ((flags & BINKSURFACEMASK) == BINKSURFACE32 || (flags & BINKSURFACEMASK) == BINKSURFACE32R)
    {
        /* no alpha: opaque */
        for (int y = 0; y < h; y++)
        {
            uint8_t *row = dst[0] + (size_t)y * destpitch;
            for (int x = 0; x < w; x++) row[4 * x + 3] = 0xff;
        }
    }
    return 0;
}

/* volume: 0..32768 (32768 is the normal volume) */
EXTERN_C void BinkSetVolume_c(void *bnk, uint32_t trackid, uint32_t volume)
{
    bink_video *v = video_of(bnk);
    if (v == NULL) return;
    int vol = (int)((uint64_t)volume * 127 / 32768);
    if (vol > 127) vol = 127;
    v->volume = vol;
    if (v->channel) Mixer_SetVolume(v->channel, vol);
}

/* The sound goes to the mixer: the sound system of the game is not used */
EXTERN_C uint32_t BinkOpenDirectSound_c(uint32_t param) { return 1; }
EXTERN_C uint32_t BinkSetSoundSystem_c(uint32_t open, uint32_t param) { return 1; }
EXTERN_C void BinkSetSoundTrack_c(uint32_t total_tracks, void *tracks) {}

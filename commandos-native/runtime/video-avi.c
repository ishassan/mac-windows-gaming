/*
 *  Commandos port: AVI file reader with a Cinepak video decoder and an
 *  MS ADPCM audio decoder (the formats of the game videos in VIDEO\).
 *  The Cinepak decoding follows the public format description (as in the
 *  FFmpeg and Wine decoders): strips, V1/V4 codebooks, intra and inter
 *  coded 4x4 blocks.
 *  MIT license, see README.md.
 */

#define _FILE_OFFSET_BITS 64
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "video-avi.h"

#define MAX_STRIPS 32

typedef struct {
    uint8_t rgb[4][3];      /* four pixels of a 2x2 block */
} cvid_entry;

typedef struct {
    cvid_entry v4[256];
    cvid_entry v1[256];
} cvid_strip;

typedef struct {
    uint64_t offset;
    uint32_t size;
} chunk_ref;

struct avi_s {
    FILE *f;
    int width, height;
    double fps;
    chunk_ref *video;
    int frames, frames_alloc;
    int next_frame;
    uint8_t *frame;                 /* RGB24, width * height * 3 */
    cvid_strip strips[MAX_STRIPS];
    uint8_t *chunk;
    uint32_t chunk_alloc;
    int16_t *audio;                 /* PCM */
    uint32_t audio_bytes;
    int audio_rate, audio_chans;
};

static uint32_t rd32le(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16le(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32be(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint32_t rd24be(const uint8_t *p) { return (p[0] << 16) | (p[1] << 8) | p[2]; }
static uint16_t rd16be(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static uint8_t clip8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

/* ----------------------------------------------------------- MS ADPCM */

static const int adpcm_adapt[16] = { 230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230 };

typedef struct {
    int chans, block_align, samples_per_block, ncoef;
    int coef1[32], coef2[32];
} adpcm_format;

static int16_t adpcm_nibble(int nibble, int *s1, int *s2, int *delta, int c1, int c2)
{
    int signed_n = (nibble & 8) ? nibble - 16 : nibble;
    int pred = ((*s1) * c1 + (*s2) * c2) / 256;
    pred += signed_n * (*delta);
    if (pred > 32767) pred = 32767;
    else if (pred < -32768) pred = -32768;
    *s2 = *s1;
    *s1 = pred;
    *delta = (adpcm_adapt[nibble] * (*delta)) / 256;
    if (*delta < 16) *delta = 16;
    return (int16_t)pred;
}

/* Decode one block. Returns the number of frames written to out. */
static int adpcm_block(const adpcm_format *fmt, const uint8_t *in, int len, int16_t *out)
{
    int ch = fmt->chans, i, n = 0;
    int pred[2], delta[2], s1[2], s2[2];
    const uint8_t *p = in;
    int max_frames;

    if (len < 7 * ch) return 0;
    for (i = 0; i < ch; i++) { pred[i] = *p++; if (pred[i] >= fmt->ncoef) pred[i] = 0; }
    for (i = 0; i < ch; i++) { delta[i] = (int16_t)rd16le(p); p += 2; }
    for (i = 0; i < ch; i++) { s1[i] = (int16_t)rd16le(p); p += 2; }
    for (i = 0; i < ch; i++) { s2[i] = (int16_t)rd16le(p); p += 2; }

    for (i = 0; i < ch; i++) out[i] = (int16_t)s2[i];
    for (i = 0; i < ch; i++) out[ch + i] = (int16_t)s1[i];
    n = 2;

    max_frames = fmt->samples_per_block;
    if (ch == 2)
    {
        while (p < in + len && n < max_frames)
        {
            out[2 * n] = adpcm_nibble(*p >> 4, &s1[0], &s2[0], &delta[0], fmt->coef1[pred[0]], fmt->coef2[pred[0]]);
            out[2 * n + 1] = adpcm_nibble(*p & 15, &s1[1], &s2[1], &delta[1], fmt->coef1[pred[1]], fmt->coef2[pred[1]]);
            p++;
            n++;
        }
    }
    else
    {
        while (p < in + len && n < max_frames)
        {
            out[n++] = adpcm_nibble(*p >> 4, &s1[0], &s2[0], &delta[0], fmt->coef1[pred[0]], fmt->coef2[pred[0]]);
            if (n < max_frames) out[n++] = adpcm_nibble(*p & 15, &s1[0], &s2[0], &delta[0], fmt->coef1[pred[0]], fmt->coef2[pred[0]]);
            p++;
        }
    }
    return n;
}

/* ----------------------------------------------------------- Cinepak */

static void cvid_codebook(cvid_entry *book, int chunk_id, const uint8_t *data, const uint8_t *end)
{
    int n = (chunk_id & 0x04) ? 4 : 6;
    uint32_t flag = 0, mask = 0;
    int i, k;

    for (i = 0; i < 256; i++)
    {
        if ((chunk_id & 0x01) && !(mask >>= 1))
        {
            if (data + 4 > end) return;
            flag = rd32be(data);
            data += 4;
            mask = 0x80000000u;
        }
        if (!(chunk_id & 0x01) || (flag & mask))
        {
            int y[4], u = 0, v = 0;
            if (data + n > end) return;
            for (k = 0; k < 4; k++) y[k] = *data++;
            if (n == 6)
            {
                u = (int8_t)*data++;
                v = (int8_t)*data++;
            }
            for (k = 0; k < 4; k++)
            {
                book[i].rgb[k][0] = clip8(y[k] + 2 * v);
                book[i].rgb[k][1] = clip8(y[k] - u / 2 - v);
                book[i].rgb[k][2] = clip8(y[k] + 2 * u);
            }
        }
    }
}

static void put2x2(avi_t *a, int x, int y, const cvid_entry *e)
{
    int dx, dy;
    for (dy = 0; dy < 2; dy++)
    {
        if (y + dy >= a->height) break;
        for (dx = 0; dx < 2; dx++)
        {
            uint8_t *p;
            if (x + dx >= a->width) break;
            p = a->frame + 3 * ((y + dy) * a->width + x + dx);
            memcpy(p, e->rgb[dy * 2 + dx], 3);
        }
    }
}

static void put_v1(avi_t *a, int x, int y, const cvid_entry *e)
{
    /* each codebook pixel covers 2x2 pixels of the 4x4 block */
    int dx, dy;
    for (dy = 0; dy < 4; dy++)
    {
        if (y + dy >= a->height) break;
        for (dx = 0; dx < 4; dx++)
        {
            if (x + dx >= a->width) break;
            memcpy(a->frame + 3 * ((y + dy) * a->width + x + dx), e->rgb[(dy / 2) * 2 + dx / 2], 3);
        }
    }
}

static void cvid_vectors(avi_t *a, cvid_strip *s, int chunk_id, const uint8_t *data, const uint8_t *end,
                         int x1, int y1, int x2, int y2)
{
    uint32_t flag = 0, mask = 0;
    int x, y;

    for (y = y1; y < y2; y += 4)
    {
        for (x = x1; x < x2; x += 4)
        {
            if ((chunk_id & 0x01) && !(mask >>= 1))
            {
                if (data + 4 > end) return;
                flag = rd32be(data);
                data += 4;
                mask = 0x80000000u;
            }
            if (!(chunk_id & 0x01) || (flag & mask))
            {
                if (!(chunk_id & 0x02) && !(mask >>= 1))
                {
                    if (data + 4 > end) return;
                    flag = rd32be(data);
                    data += 4;
                    mask = 0x80000000u;
                }
                if ((chunk_id & 0x02) || (~flag & mask))
                {
                    if (data >= end) return;
                    put_v1(a, x, y, &s->v1[*data++]);
                }
                else if (flag & mask)
                {
                    if (data + 4 > end) return;
                    put2x2(a, x, y, &s->v4[data[0]]);
                    put2x2(a, x + 2, y, &s->v4[data[1]]);
                    put2x2(a, x, y + 2, &s->v4[data[2]]);
                    put2x2(a, x + 2, y + 2, &s->v4[data[3]]);
                    data += 4;
                }
            }
        }
    }
}

static void cvid_frame(avi_t *a, const uint8_t *data, uint32_t size)
{
    const uint8_t *end = data + size;
    int frame_flags, num_strips, i, y0 = 0;

    if (size < 10) return;
    frame_flags = data[0];
    num_strips = rd16be(data + 8);
    data += 10;
    if (num_strips > MAX_STRIPS) num_strips = MAX_STRIPS;

    for (i = 0; i < num_strips && data + 12 <= end; i++)
    {
        int strip_size = (int)rd24be(data + 1);
        int y1 = y0, y2 = y0 + rd16be(data + 8);
        const uint8_t *sp, *send;

        if (strip_size < 12 || data + strip_size > end) strip_size = (int)(end - data);
        if (i > 0 && !(frame_flags & 0x01))
        {
            memcpy(&a->strips[i], &a->strips[i - 1], sizeof(cvid_strip));
        }
        sp = data + 12;
        send = data + strip_size;
        while (sp + 4 <= send)
        {
            int chunk_id = sp[0];
            int chunk_size = (int)rd24be(sp + 1);
            const uint8_t *cd = sp + 4, *cend;
            if (chunk_size < 4 || sp + chunk_size > send) chunk_size = (int)(send - sp);
            cend = sp + chunk_size;
            switch (chunk_id)
            {
                case 0x20: case 0x21: case 0x24: case 0x25:
                    cvid_codebook(a->strips[i].v4, chunk_id, cd, cend);
                    break;
                case 0x22: case 0x23: case 0x26: case 0x27:
                    cvid_codebook(a->strips[i].v1, chunk_id, cd, cend);
                    break;
                case 0x30: case 0x31: case 0x32:
                    cvid_vectors(a, &a->strips[i], chunk_id, cd, cend, 0, y1, a->width, y2);
                    break;
                default:
                    break;
            }
            sp += chunk_size;
        }
        y0 = y2;
        data += strip_size;
    }
}

/* ----------------------------------------------------------- AVI */

static void add_video_chunk(avi_t *a, uint64_t offset, uint32_t size)
{
    if (a->frames == a->frames_alloc)
    {
        a->frames_alloc = a->frames_alloc ? 2 * a->frames_alloc : 1024;
        a->video = (chunk_ref *)realloc(a->video, a->frames_alloc * sizeof(chunk_ref));
    }
    a->video[a->frames].offset = offset;
    a->video[a->frames].size = size;
    a->frames++;
}

typedef struct {
    uint8_t *data;
    uint32_t size, alloc;
} buffer_t;

static void buf_append(buffer_t *b, FILE *f, uint32_t size)
{
    if (b->size + size > b->alloc)
    {
        b->alloc = (b->size + size) * 2;
        b->data = (uint8_t *)realloc(b->data, b->alloc);
    }
    b->size += (uint32_t)fread(b->data + b->size, 1, size, f);
}

/* Read the chunks of a LIST 'movi' (and 'rec ' lists inside it). */
static void read_movi(avi_t *a, buffer_t *audio, int vstream, int astream, uint64_t pos, uint64_t end)
{
    uint8_t h[12];
    while (pos + 8 <= end)
    {
        uint32_t size;
        fseeko(a->f, (off_t)pos, SEEK_SET);
        if (fread(h, 1, 8, a->f) != 8) return;
        size = rd32le(h + 4);
        if (memcmp(h, "LIST", 4) == 0)
        {
            read_movi(a, audio, vstream, astream, pos + 12, pos + 8 + size);
        }
        else if (h[0] >= '0' && h[0] <= '9' && h[1] >= '0' && h[1] <= '9')
        {
            int stream = (h[0] - '0') * 10 + (h[1] - '0');
            if (stream == vstream && (h[3] == 'c' || h[3] == 'b')) add_video_chunk(a, pos + 8, size);
            else if (stream == astream && h[2] == 'w' && h[3] == 'b') buf_append(audio, a->f, size);
        }
        pos += 8 + size + (size & 1);
    }
}

avi_t *Avi_Open(const char *path)
{
    avi_t *a;
    uint8_t h[12];
    uint64_t pos, file_end;
    int stream = 0, vstream = -1, astream = -1;
    uint32_t rate = 0, scale = 1, usec_per_frame = 0;
    int last_type = 0;      /* 1 video, 2 audio */
    adpcm_format afmt;
    int afmt_tag = 0, abits = 16;
    buffer_t audio = { NULL, 0, 0 };

    memset(&afmt, 0, sizeof(afmt));
    a = (avi_t *)calloc(1, sizeof(avi_t));
    if (a == NULL) return NULL;
    a->f = fopen(path, "rb");
    if (a->f == NULL || fread(h, 1, 12, a->f) != 12 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "AVI ", 4) != 0)
    {
        Avi_Close(a);
        return NULL;
    }
    fseeko(a->f, 0, SEEK_END);
    file_end = (uint64_t)ftello(a->f);

    /* walk the top level and the hdrl list */
    pos = 12;
    while (pos + 8 <= file_end)
    {
        uint32_t size;
        fseeko(a->f, (off_t)pos, SEEK_SET);
        if (fread(h, 1, 12, a->f) < 8) break;
        size = rd32le(h + 4);
        if (memcmp(h, "LIST", 4) == 0 && (memcmp(h + 8, "hdrl", 4) == 0 || memcmp(h + 8, "strl", 4) == 0))
        {
            pos += 12;      /* go into the list */
            continue;
        }
        if (memcmp(h, "LIST", 4) == 0 && memcmp(h + 8, "movi", 4) == 0)
        {
            read_movi(a, &audio, vstream, astream, pos + 12, pos + 8 + size);
        }
        else if (memcmp(h, "avih", 4) == 0)
        {
            uint8_t d[4];
            fseeko(a->f, (off_t)(pos + 8), SEEK_SET);
            if (fread(d, 1, 4, a->f) == 4) usec_per_frame = rd32le(d);
        }
        else if (memcmp(h, "strh", 4) == 0)
        {
            uint8_t d[48];
            fseeko(a->f, (off_t)(pos + 8), SEEK_SET);
            memset(d, 0, sizeof(d));
            if (fread(d, 1, size < 48 ? size : 48, a->f) < 24) break;
            if (memcmp(d, "vids", 4) == 0 && vstream < 0)
            {
                vstream = stream;
                scale = rd32le(d + 20);
                rate = rd32le(d + 24);
                last_type = 1;
            }
            else if (memcmp(d, "auds", 4) == 0 && astream < 0)
            {
                astream = stream;
                last_type = 2;
            }
            else last_type = 0;
            stream++;
        }
        else if (memcmp(h, "strf", 4) == 0)
        {
            uint8_t d[256];
            uint32_t n = size < sizeof(d) ? size : sizeof(d);
            fseeko(a->f, (off_t)(pos + 8), SEEK_SET);
            memset(d, 0, sizeof(d));
            n = (uint32_t)fread(d, 1, n, a->f);
            if (last_type == 1 && n >= 20)
            {
                a->width = (int)rd32le(d + 4);
                a->height = abs((int)rd32le(d + 8));
                if (memcmp(d + 16, "cvid", 4) != 0) fprintf(stderr, "video: codec %.4s not supported\n", (char *)d + 16);
            }
            else if (last_type == 2 && n >= 16)
            {
                afmt_tag = rd16le(d);
                afmt.chans = rd16le(d + 2);
                a->audio_rate = (int)rd32le(d + 4);
                afmt.block_align = rd16le(d + 12);
                abits = rd16le(d + 14);
                if (afmt_tag == 2 && n >= 22)
                {
                    int i;
                    afmt.samples_per_block = rd16le(d + 18);
                    afmt.ncoef = rd16le(d + 20);
                    if (afmt.ncoef > 32) afmt.ncoef = 32;
                    for (i = 0; i < afmt.ncoef && 22 + 4 * i + 4 <= (int)n; i++)
                    {
                        afmt.coef1[i] = (int16_t)rd16le(d + 22 + 4 * i);
                        afmt.coef2[i] = (int16_t)rd16le(d + 24 + 4 * i);
                    }
                }
            }
        }
        pos += 8 + size + (size & 1);
    }

    if (a->width <= 0 || a->height <= 0 || a->frames == 0)
    {
        free(audio.data);
        Avi_Close(a);
        return NULL;
    }
    a->fps = (rate && scale) ? (double)rate / scale : (usec_per_frame ? 1e6 / usec_per_frame : 15.0);
    a->frame = (uint8_t *)calloc(1, (size_t)a->width * a->height * 3);

    /* audio to PCM */
    a->audio_chans = afmt.chans ? afmt.chans : 1;
    if (afmt_tag == 2 && afmt.block_align > 0 && afmt.samples_per_block > 0)
    {
        uint32_t blocks = audio.size / afmt.block_align, b;
        a->audio = (int16_t *)malloc((size_t)(blocks + 1) * afmt.samples_per_block * a->audio_chans * 2);
        if (a->audio != NULL)
        {
            uint32_t frames = 0;
            for (b = 0; b < blocks; b++)
            {
                frames += adpcm_block(&afmt, audio.data + b * afmt.block_align, afmt.block_align, a->audio + frames * a->audio_chans);
            }
            a->audio_bytes = frames * a->audio_chans * 2;
        }
    }
    else if (afmt_tag == 1 && abits == 16)
    {
        a->audio = (int16_t *)audio.data;
        a->audio_bytes = audio.size;
        audio.data = NULL;
    }
    free(audio.data);
    return a;
}

void Avi_Close(avi_t *a)
{
    if (a == NULL) return;
    if (a->f) fclose(a->f);
    free(a->video);
    free(a->frame);
    free(a->chunk);
    free(a->audio);
    free(a);
}

int Avi_Width(const avi_t *a) { return a->width; }
int Avi_Height(const avi_t *a) { return a->height; }
double Avi_Fps(const avi_t *a) { return a->fps; }
int Avi_Frames(const avi_t *a) { return a->frames; }
int Avi_NextFrame(const avi_t *a) { return a->next_frame; }
const uint8_t *Avi_FrameRGB(const avi_t *a) { return a->frame; }

const int16_t *Avi_Audio(const avi_t *a, uint32_t *bytes, int *rate, int *chans)
{
    *bytes = a->audio_bytes;
    *rate = a->audio_rate;
    *chans = a->audio_chans;
    return a->audio;
}

void Avi_Rewind(avi_t *a)
{
    a->next_frame = 0;
    memset(a->frame, 0, (size_t)a->width * a->height * 3);
}

int Avi_DecodeNext(avi_t *a)
{
    chunk_ref *c;
    if (a->next_frame >= a->frames) return -1;
    c = &a->video[a->next_frame++];
    if (c->size == 0) return 0;     /* repeated frame */
    if (c->size > a->chunk_alloc)
    {
        a->chunk_alloc = c->size;
        a->chunk = (uint8_t *)realloc(a->chunk, a->chunk_alloc);
    }
    fseeko(a->f, (off_t)c->offset, SEEK_SET);
    if (fread(a->chunk, 1, c->size, a->f) != c->size) return -1;
    cvid_frame(a, a->chunk, c->size);
    return 0;
}

/*
 *  Native port: Smacker video (smackw32.dll) with a new decoder.
 *
 *  Format notes (from the public format description of the Smacker file
 *  format, multimedia.cx wiki): a 104-byte header, the frame sizes and
 *  frame types, four Huffman trees for the video (MMap, MClr, Full, Type),
 *  then the frames. A frame has an optional palette chunk, one chunk per
 *  audio track, then the video bit stream (4x4 blocks: mono, full, skip,
 *  fill). Bits are read from the low bit of each byte first.
 *
 *  SmackOpen reads the file header and decodes the sound of track 0 for
 *  the whole video into one PCM buffer, which the mixer (audio-mixer.c)
 *  plays. The video follows the sound position (SmackWait). SmackDoFrame
 *  decodes one frame and writes it to the buffer of SmackToBuffer, as
 *  16-bit RGB (555 or 565) or 8-bit palette indexes.
 *  Interlaced files (header flag 2) have every second line black; doubled
 *  files (flag 4) repeat each line. Both report the full height.
 *  Trace with <GAME>_TRACE_VIDEO=1.
 *  MIT license, see the README.md of the repository.
 */

#include "game-info.h"
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <vector>
#include "audio-mixer.h"
#include "CLIB.h"
#include "Game-Memory.h"
#include "guest.h"
#include "platform.h"

/* SmackToBuffer flags (smack.h of the Smacker SDK) */
#define SMACKBUFFER555 0x80000000u
#define SMACKBUFFER565 0xC0000000u

#define SMK_NODE 0x80000000u

/* Offsets in the guest Smack structure */
#define SMK_VERSION    0x000
#define SMK_WIDTH      0x004
#define SMK_HEIGHT     0x008
#define SMK_FRAMES     0x00c
#define SMK_MSPERFRAME 0x010
#define SMK_TYPE       0x014
#define SMK_NEWPALETTE 0x068
#define SMK_PALETTE    0x06c
#define SMK_FRAMENUM   0x374
#define SMK_STRUCT     0x400

static int trace(void)
{
    static int value = -1;
    if (value < 0) value = (game_getenv("TRACE_VIDEO") != NULL);
    return value;
}

/* ------------------------------------------------------------ bit reader */

typedef struct {
    const uint8_t *p;
    size_t size, pos;   /* pos in bits */
} bits_t;

static unsigned get_bit(bits_t *b)
{
    if ((b->pos >> 3) >= b->size) return 0;
    unsigned v = (b->p[b->pos >> 3] >> (b->pos & 7)) & 1;
    b->pos++;
    return v;
}

static unsigned get_bits(bits_t *b, int n)
{
    unsigned v = 0;
    for (int i = 0; i < n; i++) v |= get_bit(b) << i;
    return v;
}

/* ------------------------------------------------------------ Huffman trees */

/* An 8-bit tree: nodes[i] >= 0x100 is an inner node with children
 * left = nodes[i] - 0x100 ... stored as two arrays. */
typedef struct {
    std::vector<int> left, right, value;   /* value >= 0: leaf */
    int present;
} byte_tree;

static int read_byte_tree_node(bits_t *b, byte_tree *t, int depth)
{
    int i = (int)t->value.size();
    t->left.push_back(-1);
    t->right.push_back(-1);
    t->value.push_back(-1);
    if (depth > 32) { t->value[i] = 0; return i; }
    if (!get_bit(b))
    {
        t->value[i] = (int)get_bits(b, 8);
        return i;
    }
    int l = read_byte_tree_node(b, t, depth + 1);
    int r = read_byte_tree_node(b, t, depth + 1);
    t->left[i] = l;
    t->right[i] = r;
    return i;
}

/* A presence bit, the tree, an end bit */
static void read_byte_tree(bits_t *b, byte_tree *t)
{
    t->left.clear(); t->right.clear(); t->value.clear();
    t->present = get_bit(b);
    if (!t->present) return;
    read_byte_tree_node(b, t, 0);
    get_bit(b);
}

static int byte_code(bits_t *b, const byte_tree *t)
{
    if (!t->present) return 0;
    int i = 0;
    while (t->value[i] < 0) i = get_bit(b) ? t->right[i] : t->left[i];
    return t->value[i];
}

/* A 16-bit tree (the "big tree"): a flat table as in the format
 * description; an inner node holds SMK_NODE | (size of its left part). */
typedef struct {
    std::vector<uint32_t> table;
    int last[3];
} big_tree;

static uint32_t read_big_node(bits_t *b, big_tree *t, const byte_tree *lo, const byte_tree *hi, const int *escapes, int depth)
{
    if (depth > 64 || t->table.size() > (1u << 22)) return 1;
    if (!get_bit(b))
    {
        uint32_t v = (uint32_t)byte_code(b, lo) | ((uint32_t)byte_code(b, hi) << 8);
        int idx = (int)t->table.size();
        if ((int)v == escapes[0]) { t->last[0] = idx; v = 0; }
        else if ((int)v == escapes[1]) { t->last[1] = idx; v = 0; }
        else if ((int)v == escapes[2]) { t->last[2] = idx; v = 0; }
        t->table.push_back(v);
        return 1;
    }
    size_t at = t->table.size();
    t->table.push_back(0);
    uint32_t r = read_big_node(b, t, lo, hi, escapes, depth + 1);
    t->table[at] = SMK_NODE | r;
    r++;
    r += read_big_node(b, t, lo, hi, escapes, depth + 1);
    return r;
}

static void read_big_tree(bits_t *b, big_tree *t)
{
    t->table.clear();
    t->last[0] = t->last[1] = t->last[2] = -1;
    if (!get_bit(b))
    {
        t->table.push_back(0);
        t->last[0] = t->last[1] = t->last[2] = 0;
        return;
    }
    byte_tree lo, hi;
    read_byte_tree(b, &lo);
    read_byte_tree(b, &hi);
    int escapes[3];
    for (int i = 0; i < 3; i++) escapes[i] = (int)get_bits(b, 16);
    read_big_node(b, t, &lo, &hi, escapes, 0);
    get_bit(b);
    for (int i = 0; i < 3; i++)
    {
        if (t->last[i] < 0)
        {
            t->last[i] = (int)t->table.size();
            t->table.push_back(0);
        }
    }
}

static void big_reset(big_tree *t)
{
    for (int i = 0; i < 3; i++) t->table[t->last[i]] = 0;
}

static uint32_t big_code(bits_t *b, big_tree *t)
{
    uint32_t *table = t->table.data();
    size_t i = 0, n = t->table.size();
    while (i < n && (table[i] & SMK_NODE))
    {
        if (get_bit(b)) i += table[i] & ~SMK_NODE;
        i++;
    }
    if (i >= n) return 0;
    uint32_t v = table[i];
    if (v != table[t->last[0]])
    {
        table[t->last[2]] = table[t->last[1]];
        table[t->last[1]] = table[t->last[0]];
        table[t->last[0]] = v;
    }
    return v;
}

/* ------------------------------------------------------------ video state */

typedef struct {
    FILE *f;
    uint32_t width, height, frames, flags;
    uint32_t audio_size[7], audio_rate[7];
    double ms_per_frame;
    std::vector<uint32_t> frame_size;
    std::vector<uint8_t> frame_type;
    long first_frame;
    big_tree mmap, mclr, full, type;
    int smk4;
    uint8_t *image;        /* width x height, 8-bit indexes */
    uint8_t pal[768];
    uint32_t frame;        /* next frame to decode */
    long file_pos;
    /* SmackToBuffer */
    uint8_t *dst;
    uint32_t dst_left, dst_top, dst_pitch, dst_height, dst_flags;
    /* sound */
    int channel;
    int16_t *pcm;
    uint32_t pcm_bytes;
    int sound_on, sound_started;
    uint32_t start_ticks;
} smk_t;

static std::map<uint32_t, smk_t *> videos;

static const uint8_t block_runs[64] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 128, 0, 0, 0, 0 };

static int block_run(int i)
{
    static const int big[4] = { 256, 512, 1024, 2048 };
    return (i < 60) ? block_runs[i] : big[i - 60];
}

static void decode_palette(smk_t *s, const uint8_t *p, size_t n)
{
    uint8_t old[768];
    memcpy(old, s->pal, 768);
    size_t pos = 0;
    int i = 0;
    while (i < 256 && pos < n)
    {
        uint8_t t = p[pos++];
        if (t & 0x80)
        {
            i += (t & 0x7f) + 1;   /* these entries stay */
        }
        else if (t & 0x40)
        {
            if (pos >= n) break;
            int off = p[pos++], count = (t & 0x3f) + 1;
            while (count-- && i < 256 && off < 256)
            {
                memcpy(s->pal + 3 * i, old + 3 * off, 3);
                i++;
                off++;
            }
        }
        else
        {
            if (pos + 2 > n) break;
            uint8_t r = t & 0x3f, g = p[pos++] & 0x3f, b = p[pos++] & 0x3f;
            s->pal[3 * i] = (uint8_t)((r << 2) | (r >> 4));
            s->pal[3 * i + 1] = (uint8_t)((g << 2) | (g >> 4));
            s->pal[3 * i + 2] = (uint8_t)((b << 2) | (b >> 4));
            i++;
        }
    }
}

static void decode_video(smk_t *s, const uint8_t *p, size_t n)
{
    bits_t b = { p, n, 0 };
    uint32_t bw = s->width / 4, bh = s->height / 4, blocks = bw * bh, blk = 0;
    uint32_t stride = s->width;
    big_reset(&s->mmap);
    big_reset(&s->mclr);
    big_reset(&s->full);
    big_reset(&s->type);
    while (blk < blocks)
    {
        uint32_t type = big_code(&b, &s->type);
        int run = block_run((type >> 2) & 0x3f);
        switch (type & 3)
        {
        case 0:   /* mono */
            while (run-- > 0 && blk < blocks)
            {
                uint32_t clr = big_code(&b, &s->mclr), map = big_code(&b, &s->mmap);
                uint8_t hi = (uint8_t)(clr >> 8), lo = (uint8_t)clr;
                uint8_t *out = s->image + (blk / bw) * 4 * stride + (blk % bw) * 4;
                for (int i = 0; i < 4; i++)
                {
                    out[0] = (map & 1) ? hi : lo;
                    out[1] = (map & 2) ? hi : lo;
                    out[2] = (map & 4) ? hi : lo;
                    out[3] = (map & 8) ? hi : lo;
                    map >>= 4;
                    out += stride;
                }
                blk++;
            }
            break;
        case 1:   /* full */
        {
            int mode = 0;
            if (s->smk4)
            {
                if (get_bit(&b)) mode = 1;
                else if (get_bit(&b)) mode = 2;
            }
            while (run-- > 0 && blk < blocks)
            {
                uint8_t *out = s->image + (blk / bw) * 4 * stride + (blk % bw) * 4;
                if (mode == 0)
                {
                    for (int i = 0; i < 4; i++)
                    {
                        uint32_t pix = big_code(&b, &s->full);
                        out[2] = (uint8_t)pix;
                        out[3] = (uint8_t)(pix >> 8);
                        pix = big_code(&b, &s->full);
                        out[0] = (uint8_t)pix;
                        out[1] = (uint8_t)(pix >> 8);
                        out += stride;
                    }
                }
                else if (mode == 1)
                {
                    for (int i = 0; i < 2; i++)
                    {
                        uint32_t pix = big_code(&b, &s->full);
                        for (int k = 0; k < 2; k++)
                        {
                            out[0] = out[1] = (uint8_t)pix;
                            out[2] = out[3] = (uint8_t)(pix >> 8);
                            out += stride;
                        }
                    }
                }
                else
                {
                    for (int i = 0; i < 2; i++)
                    {
                        uint32_t pix2 = big_code(&b, &s->full), pix1 = big_code(&b, &s->full);
                        for (int k = 0; k < 2; k++)
                        {
                            out[0] = (uint8_t)pix1;
                            out[1] = (uint8_t)(pix1 >> 8);
                            out[2] = (uint8_t)pix2;
                            out[3] = (uint8_t)(pix2 >> 8);
                            out += stride;
                        }
                    }
                }
                blk++;
            }
            break;
        }
        case 2:   /* skip: the block of the last frame stays */
            while (run-- > 0 && blk < blocks) blk++;
            break;
        case 3:   /* fill */
        {
            uint8_t col = (uint8_t)(type >> 8);
            while (run-- > 0 && blk < blocks)
            {
                uint8_t *out = s->image + (blk / bw) * 4 * stride + (blk % bw) * 4;
                for (int i = 0; i < 4; i++)
                {
                    memset(out, col, 4);
                    out += stride;
                }
                blk++;
            }
            break;
        }
        }
    }
}

/* One compressed (DPCM + Huffman) audio packet. Returns samples written. */
static uint32_t decode_audio(const uint8_t *p, size_t n, int16_t *out, uint32_t max_samples)
{
    if (n < 4) return 0;
    uint32_t unp_size = rd32(p);
    bits_t b = { p + 4, n - 4, 0 };
    if (!get_bit(&b)) return 0;   /* no sound in this packet */
    int stereo = get_bit(&b), sixteen = get_bit(&b);
    byte_tree t[4];
    for (int i = 0; i < (1 << (sixteen + stereo)); i++) read_byte_tree(&b, &t[i]);
    uint32_t count = sixteen ? unp_size / 2 : unp_size;
    if (count > max_samples) count = max_samples;
    uint32_t i = 0;
    if (sixteen)
    {
        int16_t pred[2] = { 0, 0 };
        for (int c = stereo; c >= 0; c--)
        {
            uint32_t v = get_bits(&b, 16);
            pred[c] = (int16_t)(((v & 0xff) << 8) | (v >> 8));
        }
        for (int c = 0; c <= stereo && i < count; c++) out[i++] = pred[c];
        for (; i < count; i++)
        {
            int c = (i & stereo) ? 1 : 0;
            int lo = byte_code(&b, &t[2 * c]), hi = byte_code(&b, &t[2 * c + 1]);
            pred[c] = (int16_t)(pred[c] + (int16_t)(lo | (hi << 8)));
            out[i] = pred[c];
        }
    }
    else
    {
        uint8_t pred[2] = { 0, 0 };
        for (int c = stereo; c >= 0; c--) pred[c] = (uint8_t)get_bits(&b, 8);
        for (int c = 0; c <= stereo && i < count; c++) out[i++] = (int16_t)((pred[c] - 128) << 8);
        for (; i < count; i++)
        {
            int c = (i & stereo) ? 1 : 0;
            pred[c] = (uint8_t)(pred[c] + (int8_t)byte_code(&b, &t[c]));
            out[i] = (int16_t)((pred[c] - 128) << 8);
        }
    }
    return i;
}

/* Reads frame "index" at the current file position into buf */
static int read_frame(smk_t *s, std::vector<uint8_t> &buf)
{
    uint32_t size = s->frame_size[s->frame % s->frames] & ~3u;
    buf.resize(size);
    return fread(buf.data(), 1, size, s->f) == size;
}

/* Splits a frame into palette, audio and video parts */
static void frame_parts(smk_t *s, const uint8_t *p, size_t n, uint8_t ftype,
                        const uint8_t **pal, size_t *pal_n, const uint8_t **aud0, size_t *aud0_n,
                        const uint8_t **vid, size_t *vid_n)
{
    size_t pos = 0;
    *pal = NULL; *pal_n = 0; *aud0 = NULL; *aud0_n = 0;
    if ((ftype & 1) && pos < n)
    {
        size_t len = (size_t)p[pos] * 4;
        if (len == 0 || pos + len > n) len = n - pos;
        *pal = p + pos + 1;
        *pal_n = len - 1;
        pos += len;
    }
    for (int t = 0; t < 7; t++)
    {
        if (!(ftype & (2 << t)) || pos + 4 > n) continue;
        size_t len = rd32(p + pos);
        if (len < 4 || pos + len > n) len = n - pos;
        if (t == 0) { *aud0 = p + pos + 4; *aud0_n = len - 4; }
        pos += len;
    }
    *vid = p + pos;
    *vid_n = n - pos;
}

static void decode_all_audio(smk_t *s)
{
    uint32_t rate = s->audio_rate[0];
    if (!(rate & 0x40000000u) || s->audio_size[0] == 0) return;   /* no track 0 */
    int chans = (rate & 0x10000000u) ? 2 : 1, sixteen = (rate & 0x20000000u) != 0;
    int compressed = (rate & 0x80000000u) != 0;
    if ((rate & 0x0c000000u) != 0)
    {
        LOG_ONCE("Smacker: audio codec %u is not supported\n", (rate >> 26) & 3);
        return;
    }
    std::vector<int16_t> pcm;
    std::vector<uint8_t> buf;
    fseek(s->f, s->first_frame, SEEK_SET);
    for (uint32_t i = 0; i < s->frames; i++)
    {
        s->frame = i;
        if (!read_frame(s, buf)) break;
        const uint8_t *pal, *aud, *vid;
        size_t pal_n, aud_n, vid_n;
        frame_parts(s, buf.data(), buf.size(), s->frame_type[i], &pal, &pal_n, &aud, &aud_n, &vid, &vid_n);
        if (aud == NULL || aud_n == 0) continue;
        if (compressed)
        {
            uint32_t unp = (aud_n >= 4) ? rd32(aud) : 0;
            size_t at = pcm.size();
            uint32_t max = sixteen ? unp / 2 : unp;
            if (max > (1u << 24)) continue;
            pcm.resize(at + max);
            uint32_t got = decode_audio(aud, aud_n, pcm.data() + at, max);
            pcm.resize(at + got);
        }
        else if (sixteen)
        {
            size_t at = pcm.size();
            pcm.resize(at + aud_n / 2);
            memcpy(pcm.data() + at, aud, (aud_n / 2) * 2);
        }
        else
        {
            for (size_t k = 0; k < aud_n; k++) pcm.push_back((int16_t)((aud[k] - 128) << 8));
        }
    }
    s->frame = 0;
    if (pcm.empty()) return;
    s->pcm_bytes = (uint32_t)(pcm.size() * 2);
    s->pcm = (int16_t *)malloc(s->pcm_bytes);
    memcpy(s->pcm, pcm.data(), s->pcm_bytes);
    s->channel = Mixer_Allocate();
    if (s->channel)
    {
        Mixer_SetData(s->channel, s->pcm, s->pcm_bytes, (int)(rate & 0xffffff), 16, chans, NULL);
        Mixer_SetLoops(s->channel, 1);
        Mixer_SetVolume(s->channel, 127);
        Mixer_SetPan(s->channel, 64);
    }
    if (trace()) fprintf(stderr, "Smacker: sound %u Hz, %d ch, %.1f s\n", rate & 0xffffff, chans,
                         (double)pcm.size() / chans / (rate & 0xffffff));
}

/* ------------------------------------------------------------ smackw32 */

static smk_t *video_of(void *smk)
{
    auto it = videos.find(to_guest(smk));
    return (it == videos.end()) ? NULL : it->second;
}

EXTERN_C void *SmackOpen_c(const char *name, uint32_t flags, uint32_t extrabuf)
{
    char path[4096];
    uint8_t h[104];
    if (name == NULL || !CLIB_FindFile(name, path))
    {
        if (trace()) fprintf(stderr, "Smacker: file not found: %s\n", name ? name : "(null)");
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fread(h, 1, 104, f) != 104 || memcmp(h, "SMK", 3) != 0 || (h[3] != '2' && h[3] != '4'))
    {
        fclose(f);
        return NULL;
    }
    smk_t *s = new smk_t();
    s->f = f;
    s->smk4 = (h[3] == '4');
    s->width = rd32(h + 4);
    s->height = rd32(h + 8);
    s->frames = rd32(h + 12);
    int32_t rate = (int32_t)rd32(h + 16);
    s->flags = rd32(h + 20);
    for (int i = 0; i < 7; i++)
    {
        s->audio_size[i] = rd32(h + 24 + 4 * i);
        s->audio_rate[i] = rd32(h + 72 + 4 * i);
    }
    uint32_t tree_size = rd32(h + 52);
    s->ms_per_frame = (rate > 0) ? rate : (rate < 0) ? -rate / 100.0 : 100.0;
    if (s->frames == 0 || s->frames > 1000000 || s->width == 0 || s->width > 4096 || s->height == 0 || s->height > 4096)
    {
        fclose(f);
        delete s;
        return NULL;
    }
    uint32_t total = s->frames + ((s->flags & 1) ? 1 : 0);
    s->frame_size.resize(total);
    s->frame_type.resize(total);
    for (uint32_t i = 0; i < total; i++)
    {
        uint8_t v[4];
        if (fread(v, 1, 4, f) != 4) break;
        s->frame_size[i] = rd32(v);
    }
    if (fread(s->frame_type.data(), 1, total, f) != total) { /* short file: frames stay empty */ }
    std::vector<uint8_t> trees(tree_size + 8, 0);
    if (fread(trees.data(), 1, tree_size, f) != tree_size) { /* short file */ }
    bits_t b = { trees.data(), tree_size, 0 };
    read_big_tree(&b, &s->mmap);
    read_big_tree(&b, &s->mclr);
    read_big_tree(&b, &s->full);
    read_big_tree(&b, &s->type);
    s->first_frame = ftell(f);
    s->image = (uint8_t *)calloc(s->width, s->height);

    decode_all_audio(s);
    fseek(f, s->first_frame, SEEK_SET);
    s->frame = 0;
    s->sound_on = 1;

    uint8_t *g = (uint8_t *)x86_calloc(1, SMK_STRUCT);
    uint32_t display_height = (s->flags & 6) ? s->height * 2 : s->height;
    memcpy(g + SMK_VERSION, h, 4);
    wr32(g + SMK_WIDTH, s->width);
    wr32(g + SMK_HEIGHT, display_height);
    wr32(g + SMK_FRAMES, s->frames);
    wr32(g + SMK_MSPERFRAME, (uint32_t)s->ms_per_frame);
    wr32(g + SMK_TYPE, s->flags);
    wr32(g + SMK_FRAMENUM, 0);
    videos[to_guest(g)] = s;
    if (trace()) fprintf(stderr, "Smacker: open %s: %ux%u (flags %u), %u frames, %.2f ms per frame\n",
                         path, s->width, s->height, s->flags, s->frames, s->ms_per_frame);
    return g;
}

EXTERN_C void SmackClose_c(void *smk)
{
    smk_t *s = video_of(smk);
    if (s == NULL) return;
    if (s->channel)
    {
        Mixer_Stop(s->channel);
        Mixer_Release(s->channel);
    }
    free(s->pcm);
    free(s->image);
    fclose(s->f);
    videos.erase(to_guest(smk));
    delete s;
    x86_free(smk);
}

EXTERN_C void SmackToBuffer_c(void *smk, uint32_t left, uint32_t top, uint32_t pitch, uint32_t destheight, void *buf, uint32_t flags)
{
    smk_t *s = video_of(smk);
    if (s == NULL) return;
    s->dst = (uint8_t *)buf;
    s->dst_left = left;
    s->dst_top = top;
    s->dst_pitch = pitch;
    s->dst_height = destheight;
    s->dst_flags = flags;
}

static void write_buffer(smk_t *s)
{
    if (s->dst == NULL) return;
    uint16_t lut[256];
    int bpp = (s->dst_flags & 0x80000000u) ? 2 : 1;
    int is565 = (s->dst_flags & SMACKBUFFER565) == SMACKBUFFER565;
    for (int i = 0; i < 256; i++)
    {
        uint8_t r = s->pal[3 * i], g = s->pal[3 * i + 1], b = s->pal[3 * i + 2];
        lut[i] = is565 ? (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
                       : (uint16_t)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
    }
    int rows_per_line = (s->flags & 6) ? 2 : 1;
    for (uint32_t y = 0; y < s->height; y++)
    {
        for (int k = 0; k < rows_per_line; k++)
        {
            uint32_t dy = s->dst_top + y * rows_per_line + k;
            if (s->dst_height && dy >= s->dst_height) return;
            uint8_t *row = s->dst + (size_t)dy * s->dst_pitch + s->dst_left * bpp;
            const uint8_t *src = s->image + (size_t)y * s->width;
            int black = (k == 1) && (s->flags & 2) && !(s->flags & 4);
            if (bpp == 2)
            {
                uint16_t *d = (uint16_t *)row;
                if (black) memset(d, 0, s->width * 2);
                else for (uint32_t x = 0; x < s->width; x++) d[x] = lut[src[x]];
            }
            else
            {
                if (black) memset(row, 0, s->width);
                else memcpy(row, src, s->width);
            }
        }
    }
}

EXTERN_C uint32_t SmackDoFrame_c(void *smk)
{
    smk_t *s = video_of(smk);
    std::vector<uint8_t> buf;
    if (s == NULL) return 1;
    uint32_t index = s->frame % (uint32_t)s->frame_size.size();
    if (!read_frame(s, buf)) return 1;
    const uint8_t *pal, *aud, *vid;
    size_t pal_n, aud_n, vid_n;
    frame_parts(s, buf.data(), buf.size(), s->frame_type[index], &pal, &pal_n, &aud, &aud_n, &vid, &vid_n);
    uint8_t *g = (uint8_t *)smk;
    wr32(g + SMK_NEWPALETTE, 0);
    if (pal != NULL)
    {
        decode_palette(s, pal, pal_n);
        memcpy(g + SMK_PALETTE, s->pal, 768);
        wr32(g + SMK_NEWPALETTE, 1);
    }
    decode_video(s, vid, vid_n);
    write_buffer(s);
    if (s->frame == 0 || !s->sound_started)
    {
        s->start_ticks = SDL_GetTicks();
        if (s->channel && s->sound_on && !s->sound_started) Mixer_Start(s->channel);
        s->sound_started = 1;
    }
    /* SmackNextFrame moves on: rewind the file to the start of this frame */
    fseek(s->f, -(long)buf.size(), SEEK_CUR);
    return 0;
}

EXTERN_C void SmackNextFrame_c(void *smk)
{
    smk_t *s = video_of(smk);
    if (s == NULL) return;
    fseek(s->f, s->frame_size[s->frame % s->frame_size.size()] & ~3u, SEEK_CUR);
    s->frame++;
    if (s->frame >= s->frames)
    {
        s->frame = 0;
        fseek(s->f, s->first_frame, SEEK_SET);
    }
    wr32((uint8_t *)smk + SMK_FRAMENUM, s->frame);
}

/* 1: wait (the next frame is not due yet), 0: show the next frame now */
EXTERN_C uint32_t SmackWait_c(void *smk)
{
    smk_t *s = video_of(smk);
    if (s == NULL || !s->sound_started) return 0;
    double due = s->frame * s->ms_per_frame;
    double now;
    if (s->channel && s->sound_on && Mixer_IsPlaying(s->channel)) now = Mixer_Position(s->channel) * 1000.0;
    else now = (double)(SDL_GetTicks() - s->start_ticks);
    return (now < due) ? 1 : 0;
}

EXTERN_C uint32_t SmackSoundOnOff_c(void *smk, uint32_t on)
{
    smk_t *s = video_of(smk);
    if (s == NULL) return 0;
    s->sound_on = on != 0;
    if (s->channel && s->sound_started) Mixer_Pause(s->channel, on ? 0 : 1);
    return 1;
}

EXTERN_C void SmackVolumePan_c(void *smk, uint32_t trackflag, uint32_t volume, uint32_t pan)
{
    smk_t *s = video_of(smk);
    if (s == NULL || s->channel == 0) return;
    int v = (int)((uint64_t)volume * 127 / 32768);
    Mixer_SetVolume(s->channel, v > 127 ? 127 : v);
    Mixer_SetPan(s->channel, (int)(pan * 127 / 65535));
}

/* The SmackToBuffer flags for a DirectDraw surface */
EXTERN_C int DDraw_SurfaceInfo4(void *surface, uint8_t **pixels, int *pitch, int *width, int *height, uint32_t *caps,
                                void **zbuffer, uint32_t *texture, uint8_t *pf);

EXTERN_C uint32_t SmackDDSurfaceType_c(void *lpDDS)
{
    uint8_t pf[32], *pixels;
    int pitch, w, h;
    if (lpDDS == NULL || DDraw_SurfaceInfo4(lpDDS, &pixels, &pitch, &w, &h, NULL, NULL, NULL, pf) != 0) return 0;
    if (rd32(pf + 12) != 16) return 0;
    return (rd32(pf + 20) == 0x7e0) ? SMACKBUFFER565 : SMACKBUFFER555;
}

EXTERN_C uint32_t SmackSoundUseMSS_c(void *dd)
{
    return 1;
}

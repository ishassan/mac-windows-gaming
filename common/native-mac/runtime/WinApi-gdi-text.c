/*
 *  Native port: GDI fonts, device contexts and text, on macOS CoreText.
 *
 *  Fonts (CreateFontA, CreateFontIndirectA) use the installed macOS font
 *  with the same name (Arial, Times New Roman, ...). CoreText finds the
 *  font file and gives the text metrics. FreeType draws the glyphs with
 *  the hints (TrueType instructions) of the font. Without the hints, thin
 *  strokes of small text (the top of "e" in 12-pixel Arial) cover less
 *  than half of a pixel row and are lost.
 *  Smooth (grayscale) edges, as GDI with font smoothing and Wine: only at
 *  the sizes where the font's "gasp" table asks for them (for example
 *  20-pixel Times New Roman); other sizes have hard edges. lfQuality
 *  NONANTIALIASED_QUALITY and ANTIALIASED_QUALITY override the table. An
 *  edge pixel mixes the text color with the pixel below it. Games draw
 *  text on surfaces with a color key and then blit them: where the pixel
 *  below has the key color, the edge mixes with black instead, so that no
 *  key-colored fringe shows (Wine mixes with the key color).
 *  When FreeType cannot open the font file, CoreText draws the glyphs (a
 *  pixel is on when it is at least half covered).
 *  The text metrics and advances are whole pixels, as in GDI.
 *
 *  A device context (DC) is one of:
 *    - the DC of a DirectDraw surface (IDirectDrawSurface::GetDC): text is
 *      drawn into the surface pixels (16- or 32-bit RGB);
 *    - the window DC (GetDC, GetWindowDC, BeginPaint): text is measured,
 *      but not drawn.
 *  Strings are Windows-1252.
 *  Trace with <GAME>_TRACE_GDI=1.
 *  MIT license, see the README.md of the repository.
 */

#include "game-info.h"
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GASP_H
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include "guest.h"
#include "platform.h"

#define WINDOW_HDC   0xfde0u    /* FAKE_HDC of WinApi-user32.c */
#define FONT_FIRST   0xd000u
#define FONT_LAST    0xdffcu
#define DC_FIRST     0xc000u
#define DC_LAST      0xcffcu
#define STOCK_FONT   0xf100u    /* GetStockObject range of WinApi-gdi32.c */

#define TRANSPARENT  1
#define OPAQUE       2

#define TA_UPDATECP  1
#define TA_RIGHT     2
#define TA_CENTER    6
#define TA_BOTTOM    8
#define TA_BASELINE  24

#define DT_CENTER      0x0001
#define DT_RIGHT       0x0002
#define DT_VCENTER     0x0004
#define DT_BOTTOM      0x0008
#define DT_WORDBREAK   0x0010
#define DT_SINGLELINE  0x0020
#define DT_EXPANDTABS  0x0040
#define DT_NOCLIP      0x0100
#define DT_CALCRECT    0x0400
#define DT_NOPREFIX    0x0800

EXTERN_C int DDraw_SurfaceInfo4(void *surface, uint8_t **pixels, int *pitch, int *width, int *height, uint32_t *caps,
                                void **zbuffer, uint32_t *texture, uint8_t *pf);
EXTERN_C int DDraw_SurfaceColorKey(void *surface, uint32_t *low, uint32_t *high);

#define NONANTIALIASED_QUALITY 3
#define ANTIALIASED_QUALITY    4

typedef struct {
    uint8_t *bits;        /* coverage 0..255 (hard edges: 0 or 255); w x h */
    int w, h;             /* bitmap size */
    int left, top;        /* bitmap position from the pen (top: from the cell top) */
    int advance;
} glyph;

typedef struct {
    CTFontRef ct;
    FT_Face ft;           /* NULL: CoreText draws the glyphs */
    int gray;             /* smooth edges (FreeType glyphs only) */
    int height;           /* tmHeight = ascent + descent */
    int ascent, descent, internal_leading, external_leading;
    int ave_width, max_width, weight, italic, underline, strikeout, charset, pitch_family;
    char face[32];
    glyph *glyphs[256];
} gdi_font;

typedef struct {
    void *surface;        /* NULL: window DC */
    uint32_t font;
    uint32_t text_color, bk_color;
    int bk_mode;
    uint32_t align;
    int extra;
    int cur_x, cur_y;
    int org_x, org_y;
} gdi_dc;

static std::map<uint32_t, gdi_font *> fonts;
static std::map<uint32_t, gdi_dc *> dcs;
static uint32_t next_font = FONT_FIRST, next_dc = DC_FIRST;

static int trace(void)
{
    static int value = -1;
    if (value < 0) value = (game_getenv("TRACE_GDI") != NULL);
    return value;
}

/* ------------------------------------------------------------ fonts */

static const uint16_t cp1252_80[32] = {
    0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
    0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178 };

static UniChar unicode_of(uint8_t c)
{
    return (c >= 0x80 && c < 0xa0) ? cp1252_80[c - 0x80] : c;
}

static gdi_font *font_of(uint32_t h)
{
    auto it = fonts.find(h);
    return (it == fonts.end()) ? NULL : it->second;
}

static CTFontRef make_ct_font(const char *face, double size, int bold, int italic)
{
    CFStringRef name = CFStringCreateWithCString(NULL, (face && face[0]) ? face : "Arial", kCFStringEncodingWindowsLatin1);
    CTFontRef f = CTFontCreateWithName(name, size, NULL);
    CFRelease(name);
    CTFontSymbolicTraits traits = (bold ? kCTFontBoldTrait : 0) | (italic ? kCTFontItalicTrait : 0);
    if (traits)
    {
        CTFontRef styled = CTFontCreateCopyWithSymbolicTraits(f, size, NULL, traits, traits);
        if (styled != NULL)
        {
            CFRelease(f);
            f = styled;
        }
    }
    return f;
}

/* The FreeType face of the font file of a CoreText font, at the GDI height */
static FT_Face open_ft_face(CTFontRef ct, int32_t height)
{
    static FT_Library lib;
    char path[4096], ps[256];
    FT_Face face = NULL;
    if (lib == NULL && FT_Init_FreeType(&lib) != 0) return NULL;
    CFURLRef url = (CFURLRef)CTFontCopyAttribute(ct, kCTFontURLAttribute);
    CFStringRef name = CTFontCopyPostScriptName(ct);
    int ok = url != NULL && CFURLGetFileSystemRepresentation(url, true, (UInt8 *)path, sizeof(path)) &&
             name != NULL && CFStringGetCString(name, ps, sizeof(ps), kCFStringEncodingUTF8);
    if (url) CFRelease(url);
    if (name) CFRelease(name);
    if (!ok) return NULL;
    /* a collection file (.ttc) has more faces: take the one with the PostScript name */
    for (FT_Long i = 0, n = 1; i < n; i++)
    {
        if (FT_New_Face(lib, path, i, &face) != 0) return NULL;
        n = face->num_faces;
        const char *fps = FT_Get_Postscript_Name(face);
        if (n == 1 || (fps != NULL && strcmp(fps, ps) == 0)) break;
        FT_Done_Face(face);
        face = NULL;
    }
    if (face == NULL) return NULL;
    /* GDI: a positive height is the cell height, a negative height the em height */
    FT_Size_RequestRec req = { (height > 0) ? FT_SIZE_REQUEST_TYPE_CELL : FT_SIZE_REQUEST_TYPE_NOMINAL, 0,
                               (FT_Long)((height > 0) ? height : -height) << 6, 0, 0 };
    if (!FT_IS_SCALABLE(face) || FT_Request_Size(face, &req) != 0)
    {
        FT_Done_Face(face);
        return NULL;
    }
    return face;
}

static int advance_of(gdi_font *f, uint8_t c)
{
    UniChar u = unicode_of(c);
    CGGlyph g;
    CGSize adv;
    if (f->ft)
    {
        /* the hinted advance, as GDI */
        FT_UInt index = FT_Get_Char_Index(f->ft, u);
        if (index == 0 || FT_Load_Glyph(f->ft, index, f->gray ? FT_LOAD_TARGET_NORMAL : FT_LOAD_TARGET_MONO) != 0) return 0;
        return (int)((f->ft->glyph->advance.x + 32) >> 6);
    }
    if (!CTFontGetGlyphsForCharacters(f->ct, &u, &g, 1)) return 0;
    CTFontGetAdvancesForGlyphs(f->ct, kCTFontOrientationHorizontal, &g, &adv, 1);
    return (int)lround(adv.width);
}

static uint32_t create_font(int32_t height, int32_t width, int32_t weight, int italic, int underline, int strikeout,
                            int charset, int quality, int pitch_family, const char *face)
{
    gdi_font *f = (gdi_font *)calloc(1, sizeof(gdi_font));
    double size;
    if (face) snprintf(f->face, sizeof(f->face), "%s", face);
    if (height == 0) height = -12;
    /* first at size 100, to find the size for the wanted cell height */
    CTFontRef probe = make_ct_font(f->face, 100.0, weight >= 600, italic);
    double a = CTFontGetAscent(probe), d = CTFontGetDescent(probe);
    CFRelease(probe);
    size = (height < 0) ? -height : height * 100.0 / (a + d);
    f->ct = make_ct_font(f->face, size, weight >= 600, italic);
    /* GDI: a positive height is the cell height (ascent + descent) exactly */
    double asc = CTFontGetAscent(f->ct), desc = CTFontGetDescent(f->ct);
    f->height = (height > 0) ? height : (int)lround(asc + desc);
    f->ascent = (int)lround(asc * f->height / (asc + desc));
    f->descent = f->height - f->ascent;
    f->internal_leading = f->height - (int)lround(size);
    if (f->internal_leading < 0) f->internal_leading = 0;
    f->external_leading = (int)lround(CTFontGetLeading(f->ct));
    f->ft = open_ft_face(f->ct, height);
    if (f->ft)
    {
        /* as Wine: no gasp table, or the gasp range of this size has DO_GRAY */
        FT_Int gasp = FT_Get_Gasp(f->ft, f->ft->size->metrics.y_ppem);
        f->gray = (gasp == FT_GASP_NO_TABLE) || (gasp & FT_GASP_DO_GRAY);
        if (quality == NONANTIALIASED_QUALITY) f->gray = 0;
        if (quality == ANTIALIASED_QUALITY) f->gray = 1;
    }
    f->weight = weight ? weight : 400;
    f->italic = italic;
    f->underline = underline;
    f->strikeout = strikeout;
    f->charset = charset;
    f->pitch_family = pitch_family;
    f->ave_width = advance_of(f, 'x');
    for (int c = 32; c < 256; c++)
    {
        int w = advance_of(f, (uint8_t)c);
        if (w > f->max_width) f->max_width = w;
    }
    uint32_t h = next_font;
    while (fonts.count(h)) h = (h >= FONT_LAST) ? FONT_FIRST : h + 4;
    next_font = (h >= FONT_LAST) ? FONT_FIRST : h + 4;
    fonts[h] = f;
    if (trace()) fprintf(stderr, "GDI: CreateFont \"%s\" height %d weight %d -> 0x%x (cell %d, ascent %d, ave %d, %s, %s edges)\n",
                         f->face, height, weight, h, f->height, f->ascent, f->ave_width,
                         f->ft ? FT_Get_Postscript_Name(f->ft) : "CoreText glyphs", f->gray ? "smooth" : "hard");
    return h;
}

static void free_font(gdi_font *f)
{
    for (int i = 0; i < 256; i++)
    {
        if (f->glyphs[i]) free(f->glyphs[i]->bits);
        free(f->glyphs[i]);
    }
    if (f->ft) FT_Done_Face(f->ft);
    CFRelease(f->ct);
    free(f);
}

static glyph *glyph_of(gdi_font *f, uint8_t c)
{
    if (f->glyphs[c]) return f->glyphs[c];
    glyph *g = (glyph *)calloc(1, sizeof(glyph));
    f->glyphs[c] = g;
    g->advance = advance_of(f, c);
    UniChar u = unicode_of(c);
    CGGlyph cg;
    if (f->ft)
    {
        FT_UInt index = FT_Get_Char_Index(f->ft, u);
        if (c < 32 || index == 0 ||
            FT_Load_Glyph(f->ft, index, FT_LOAD_RENDER | (f->gray ? FT_LOAD_TARGET_NORMAL : FT_LOAD_TARGET_MONO)) != 0) return g;
        FT_Bitmap *bm = &f->ft->glyph->bitmap;
        int mono = bm->pixel_mode == FT_PIXEL_MODE_MONO;
        if ((!mono && bm->pixel_mode != FT_PIXEL_MODE_GRAY) || bm->width == 0 || bm->rows == 0) return g;
        g->w = (int)bm->width;
        g->h = (int)bm->rows;
        g->left = f->ft->glyph->bitmap_left;
        g->top = f->ascent - f->ft->glyph->bitmap_top;
        g->bits = (uint8_t *)malloc(g->w * g->h);
        for (int y = 0; y < g->h; y++)
        {
            const uint8_t *row = bm->buffer + y * bm->pitch;
            for (int x = 0; x < g->w; x++)
            {
                if (mono) g->bits[y * g->w + x] = ((row[x >> 3] >> (7 - (x & 7))) & 1) ? 255 : 0;
                else g->bits[y * g->w + x] = (uint8_t)(row[x] * 255 / (bm->num_grays - 1));
            }
        }
        return g;
    }
    if (c < 32 || !CTFontGetGlyphsForCharacters(f->ct, &u, &cg, 1)) return g;

    CGRect box = CTFontGetBoundingRectsForGlyphs(f->ct, kCTFontOrientationHorizontal, &cg, NULL, 1);
    int x0 = (int)floor(box.origin.x) - 1, x1 = (int)ceil(box.origin.x + box.size.width) + 1;
    int y0 = (int)floor(box.origin.y) - 1, y1 = (int)ceil(box.origin.y + box.size.height) + 1;
    g->w = x1 - x0;
    g->h = y1 - y0;
    if (g->w <= 0 || g->h <= 0) { g->w = g->h = 0; return g; }
    g->left = x0;
    g->top = f->ascent - y1;   /* rows from the cell top to the bitmap top */

    uint8_t *cov = (uint8_t *)calloc(g->w * g->h, 1);
    CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
    CGContextRef ctx = CGBitmapContextCreate(cov, g->w, g->h, 8, g->w, gray, kCGImageAlphaNone);
    CGColorSpaceRelease(gray);
    CGContextSetGrayFillColor(ctx, 1.0, 1.0);
    CGContextSetShouldAntialias(ctx, true);
    CGContextSetShouldSmoothFonts(ctx, false);
    CGPoint pos = CGPointMake(-x0, -y0);
    CTFontDrawGlyphs(f->ct, &cg, &pos, 1, ctx);
    CGContextRelease(ctx);
    /* the context has its origin at the bottom: row 0 of the memory is the top */
    g->bits = (uint8_t *)malloc(g->w * g->h);
    for (int i = 0; i < g->w * g->h; i++) g->bits[i] = (cov[i] >= 128) ? 255 : 0;
    free(cov);
    return g;
}

/* ------------------------------------------------------------ device contexts */

static gdi_dc *dc_of(void *hdc)
{
    uint32_t h = to_guest(hdc);
    auto it = dcs.find(h);
    if (it != dcs.end()) return it->second;
    if (h == WINDOW_HDC)
    {
        gdi_dc *d = (gdi_dc *)calloc(1, sizeof(gdi_dc));
        d->bk_mode = OPAQUE;
        d->bk_color = 0xffffff;
        dcs[h] = d;
        return d;
    }
    return NULL;
}

/* For WinApi-ddraw.c: IDirectDrawSurface::GetDC and ReleaseDC */
EXTERN_C uint32_t GDI_CreateSurfaceDC(void *surface)
{
    gdi_dc *d = (gdi_dc *)calloc(1, sizeof(gdi_dc));
    d->surface = surface;
    d->bk_mode = OPAQUE;
    d->bk_color = 0xffffff;
    uint32_t h = next_dc;
    while (dcs.count(h)) h = (h >= DC_LAST) ? DC_FIRST : h + 4;
    next_dc = (h >= DC_LAST) ? DC_FIRST : h + 4;
    dcs[h] = d;
    return h;
}

EXTERN_C void GDI_ReleaseSurfaceDC(uint32_t hdc)
{
    auto it = dcs.find(hdc);
    if (it == dcs.end() || it->second->surface == NULL) return;
    free(it->second);
    dcs.erase(it);
}

EXTERN_C void *GetWindowDC_c(void *hWnd)
{
    return guest_value(WINDOW_HDC);
}

/* ------------------------------------------------------------ drawing */

static int mask_shift(uint32_t m) { int s = 0; if (!m) return 0; while (!(m & 1)) { m >>= 1; s++; } return s; }
static int mask_bits(uint32_t m) { int n = 0; while (m) { n += m & 1; m >>= 1; } return n; }

static uint32_t pixel_of(const uint8_t *pf, uint32_t colorref)
{
    uint32_t rm = rd32(pf + 16), gm = rd32(pf + 20), bm = rd32(pf + 24);
    uint32_t r = colorref & 0xff, g = (colorref >> 8) & 0xff, b = (colorref >> 16) & 0xff;
    uint32_t v = 0;
    v |= ((r >> (8 - mask_bits(rm))) << mask_shift(rm)) & rm;
    v |= ((g >> (8 - mask_bits(gm))) << mask_shift(gm)) & gm;
    v |= ((b >> (8 - mask_bits(bm))) << mask_shift(bm)) & bm;
    return v;
}

typedef struct {
    uint8_t *pixels;
    int pitch, width, height, bpp;
    uint32_t fg, bg;
    int clip_x0, clip_y0, clip_x1, clip_y1;
    uint32_t mask[3];          /* red, green, blue masks of the surface */
    uint8_t fg_rgb[3];         /* the text color, 8 bits per channel */
    int has_key;
    uint32_t key_low, key_high;
} target;

static int open_target(gdi_dc *d, target *t)
{
    uint8_t pf[32];
    if (d->surface == NULL) return 0;
    if (DDraw_SurfaceInfo4(d->surface, &t->pixels, &t->pitch, &t->width, &t->height, NULL, NULL, NULL, pf) != 0) return 0;
    t->bpp = (int)rd32(pf + 12);
    if (t->pixels == NULL || (t->bpp != 16 && t->bpp != 32))
    {
        LOG_ONCE("GDI: text on a %d-bit surface is not drawn\n", t->bpp);
        return 0;
    }
    t->fg = pixel_of(pf, d->text_color);
    t->bg = pixel_of(pf, d->bk_color);
    for (int i = 0; i < 3; i++)
    {
        t->mask[i] = rd32(pf + 16 + 4 * i);
        t->fg_rgb[i] = (uint8_t)(d->text_color >> (8 * i));
    }
    t->has_key = DDraw_SurfaceColorKey(d->surface, &t->key_low, &t->key_high);
    t->clip_x0 = 0;
    t->clip_y0 = 0;
    t->clip_x1 = t->width;
    t->clip_y1 = t->height;
    return 1;
}

static void put(target *t, int x, int y, uint32_t v)
{
    if (x < t->clip_x0 || y < t->clip_y0 || x >= t->clip_x1 || y >= t->clip_y1) return;
    uint8_t *p = t->pixels + y * t->pitch;
    if (t->bpp == 16) ((uint16_t *)p)[x] = (uint16_t)v;
    else ((uint32_t *)p)[x] = v;
}

/* An edge pixel: the text color over the pixel below with coverage a (1..254) */
static void blend(target *t, int x, int y, int a)
{
    if (x < t->clip_x0 || y < t->clip_y0 || x >= t->clip_x1 || y >= t->clip_y1) return;
    uint8_t *p = t->pixels + y * t->pitch;
    uint32_t old = (t->bpp == 16) ? ((uint16_t *)p)[x] : ((uint32_t *)p)[x];
    int keyed = t->has_key && old >= t->key_low && old <= t->key_high;
    uint32_t v = 0;
    for (int i = 0; i < 3; i++)
    {
        uint32_t m = t->mask[i];
        int bits = mask_bits(m), shift = mask_shift(m);
        if (bits == 0) continue;
        uint32_t maxv = (1u << bits) - 1;
        uint32_t below = keyed ? 0 : (((old & m) >> shift) * 255 + maxv / 2) / maxv;
        uint32_t c = (t->fg_rgb[i] * (uint32_t)a + below * (uint32_t)(255 - a) + 127) / 255;
        v |= ((c * maxv + 127) / 255) << shift;
    }
    if (t->bpp == 16) ((uint16_t *)p)[x] = (uint16_t)v;
    else ((uint32_t *)p)[x] = v | (old & ~(t->mask[0] | t->mask[1] | t->mask[2]));
}

static int text_width(gdi_dc *d, gdi_font *f, const uint8_t *s, int n)
{
    int w = 0;
    for (int i = 0; i < n; i++) w += glyph_of(f, s[i])->advance + d->extra;
    return w;
}

/* Draws one line with the cell top-left at x, y (DC coordinates) */
static void draw_line(gdi_dc *d, gdi_font *f, target *t, int x, int y, const uint8_t *s, int n)
{
    x += d->org_x;
    y += d->org_y;
    if (d->bk_mode == OPAQUE)
    {
        int w = text_width(d, f, s, n);
        for (int yy = y; yy < y + f->height; yy++)
            for (int xx = x; xx < x + w; xx++) put(t, xx, yy, t->bg);
    }
    for (int i = 0; i < n; i++)
    {
        glyph *g = glyph_of(f, s[i]);
        for (int gy = 0; gy < g->h; gy++)
        {
            const uint8_t *row = g->bits + gy * g->w;
            for (int gx = 0; gx < g->w; gx++)
            {
                if (row[gx] == 255) put(t, x + g->left + gx, y + g->top + gy, t->fg);
                else if (row[gx]) blend(t, x + g->left + gx, y + g->top + gy, row[gx]);
            }
        }
        if (f->underline)
        {
            for (int xx = 0; xx < g->advance + d->extra; xx++) put(t, x + xx, y + f->ascent + 1, t->fg);
        }
        x += g->advance + d->extra;
    }
}

static gdi_font *selected_font(gdi_dc *d)
{
    gdi_font *f = font_of(d->font);
    if (f == NULL)
    {
        static uint32_t system_font;
        if (system_font == 0) system_font = create_font(16, 0, 700, 0, 0, 0, 0, 0, 0, "Arial");
        f = font_of(system_font);
    }
    return f;
}

/* ------------------------------------------------------------ GDI functions */

EXTERN_C void *CreateFontA_c(int32_t h, int32_t w, int32_t e, int32_t o, int32_t weight, uint32_t i, uint32_t u, uint32_t s,
                             uint32_t cs, uint32_t op, uint32_t cp, uint32_t q, uint32_t pf, const char *face)
{
    return guest_value(create_font(h, w, weight, i != 0, u != 0, s != 0, cs, q, pf, face));
}

EXTERN_C void *CCALL CreateFontIndirectA_c(void *lplf)
{
    const uint8_t *lf = (const uint8_t *)lplf;
    char face[33];
    if (lf == NULL) return NULL;
    memcpy(face, lf + 28, 32);
    face[32] = 0;
    return guest_value(create_font((int32_t)rd32(lf), (int32_t)rd32(lf + 4), (int32_t)rd32(lf + 16),
                                   lf[20], lf[21], lf[22], lf[23], lf[26], lf[27], face));
}

EXTERN_C uint32_t CCALL DeleteObject_c(void *hObject)
{
    uint32_t h = to_guest(hObject);
    auto it = fonts.find(h);
    if (it != fonts.end())
    {
        for (auto &dc : dcs)
        {
            if (dc.second->font == h) dc.second->font = 0;
        }
        free_font(it->second);
        fonts.erase(it);
    }
    return (hObject != NULL) ? 1 : 0;
}

EXTERN_C void *CCALL SelectObject_c(void *hdc, void *hgdiobj)
{
    gdi_dc *d = dc_of(hdc);
    uint32_t h = to_guest(hgdiobj);
    if (d != NULL && (font_of(h) != NULL || h == STOCK_FONT + 4 * 13 || h == STOCK_FONT + 4 * 17))
    {
        /* a font, or a stock font (SYSTEM_FONT 13, DEFAULT_GUI_FONT 17) */
        uint32_t old = d->font ? d->font : STOCK_FONT + 4 * 13;
        d->font = font_of(h) ? h : 0;
        return guest_value(old);
    }
    return guest_value(STOCK_FONT);   /* another object: a stock object as the previous one */
}

EXTERN_C int32_t CCALL SetBkMode_c(void *hdc, int32_t iBkMode)
{
    gdi_dc *d = dc_of(hdc);
    if (d == NULL) return 0;
    int old = d->bk_mode;
    d->bk_mode = iBkMode;
    return old;
}

EXTERN_C uint32_t CCALL SetTextColor_c(void *hdc, uint32_t crColor)
{
    gdi_dc *d = dc_of(hdc);
    if (d == NULL) return 0xffffffffu;
    uint32_t old = d->text_color;
    d->text_color = crColor & 0xffffff;
    return old;
}

EXTERN_C uint32_t SetBkColor_c(void *hdc, uint32_t color)
{
    gdi_dc *d = dc_of(hdc);
    if (d == NULL) return 0xffffffffu;
    uint32_t old = d->bk_color;
    d->bk_color = color & 0xffffff;
    return old;
}

EXTERN_C uint32_t SetTextAlign_c(void *hdc, uint32_t align)
{
    gdi_dc *d = dc_of(hdc);
    if (d == NULL) return 0xffffffffu;
    uint32_t old = d->align;
    d->align = align;
    return old;
}

EXTERN_C uint32_t SetTextCharacterExtra_c(void *hdc, int32_t extra)
{
    gdi_dc *d = dc_of(hdc);
    if (d == NULL) return 0x80000000u;
    int old = d->extra;
    d->extra = extra;
    return (uint32_t)old;
}

EXTERN_C uint32_t MoveToEx_c(void *hdc, int32_t x, int32_t y, void *lppt)
{
    gdi_dc *d = dc_of(hdc);
    if (d == NULL) return 0;
    if (lppt) { wr32(lppt, (uint32_t)d->cur_x); wr32((uint8_t *)lppt + 4, (uint32_t)d->cur_y); }
    d->cur_x = x;
    d->cur_y = y;
    return 1;
}

EXTERN_C uint32_t GetCurrentPositionEx_c(void *hdc, void *lppoint)
{
    gdi_dc *d = dc_of(hdc);
    if (d == NULL || lppoint == NULL) return 0;
    wr32(lppoint, (uint32_t)d->cur_x);
    wr32((uint8_t *)lppoint + 4, (uint32_t)d->cur_y);
    return 1;
}

EXTERN_C uint32_t SetViewportOrgEx_c(void *hdc, int32_t x, int32_t y, void *lppt)
{
    gdi_dc *d = dc_of(hdc);
    if (d == NULL) return 0;
    if (lppt) { wr32(lppt, (uint32_t)d->org_x); wr32((uint8_t *)lppt + 4, (uint32_t)d->org_y); }
    d->org_x = x;
    d->org_y = y;
    return 1;
}

EXTERN_C uint32_t GetClipBox_c(void *hdc, void *lprect)
{
    gdi_dc *d = dc_of(hdc);
    target t;
    int w = 640, h = 480;
    if (d == NULL || lprect == NULL) return 0;   /* ERROR */
    if (open_target(d, &t)) { w = t.width; h = t.height; }
    wr32(lprect, (uint32_t)-d->org_x);
    wr32((uint8_t *)lprect + 4, (uint32_t)-d->org_y);
    wr32((uint8_t *)lprect + 8, (uint32_t)(w - d->org_x));
    wr32((uint8_t *)lprect + 12, (uint32_t)(h - d->org_y));
    return 2;   /* SIMPLEREGION */
}

EXTERN_C uint32_t GetTextMetricsA_c(void *hdc, void *lptm)
{
    gdi_dc *d = dc_of(hdc);
    uint8_t *tm = (uint8_t *)lptm;
    if (d == NULL || tm == NULL) return 0;
    gdi_font *f = selected_font(d);
    memset(tm, 0, 56);
    wr32(tm + 0, (uint32_t)f->height);
    wr32(tm + 4, (uint32_t)f->ascent);
    wr32(tm + 8, (uint32_t)f->descent);
    wr32(tm + 12, (uint32_t)f->internal_leading);
    wr32(tm + 16, (uint32_t)f->external_leading);
    wr32(tm + 20, (uint32_t)f->ave_width);
    wr32(tm + 24, (uint32_t)f->max_width);
    wr32(tm + 28, (uint32_t)f->weight);
    wr32(tm + 32, 0);
    wr32(tm + 36, 96);
    wr32(tm + 40, 96);
    tm[44] = 32;     /* tmFirstChar */
    tm[45] = 255;    /* tmLastChar */
    tm[46] = 31;     /* tmDefaultChar */
    tm[47] = 32;     /* tmBreakChar */
    tm[48] = (uint8_t)f->italic;
    tm[49] = (uint8_t)f->underline;
    tm[50] = (uint8_t)f->strikeout;
    tm[51] = (uint8_t)((f->pitch_family & 0xf0) | 0x06);   /* TMPF_VECTOR | TMPF_TRUETYPE */
    tm[52] = (uint8_t)f->charset;
    return 1;
}

static uint32_t extent(void *hdc, const char *s, int32_t c, void *lpSize)
{
    gdi_dc *d = dc_of(hdc);
    if (d == NULL || lpSize == NULL || c < 0) return 0;
    gdi_font *f = selected_font(d);
    wr32(lpSize, (uint32_t)(s ? text_width(d, f, (const uint8_t *)s, c) : 0));
    wr32((uint8_t *)lpSize + 4, (uint32_t)f->height);
    return 1;
}

EXTERN_C uint32_t GetTextExtentPoint32A_c(void *hdc, const char *lpString, int32_t c, void *lpSize)
{
    return extent(hdc, lpString, c, lpSize);
}

EXTERN_C uint32_t GetTextExtentPointA_c(void *hdc, const char *lpString, int32_t c, void *lpSize)
{
    return extent(hdc, lpString, c, lpSize);
}

EXTERN_C uint32_t GetTextExtentExPointA_c(void *hdc, const char *lpszString, int32_t cchString, int32_t nMaxExtent,
                                          void *lpnFit, void *lpnDx, void *lpSize)
{
    gdi_dc *d = dc_of(hdc);
    if (d == NULL || lpSize == NULL || cchString < 0) return 0;
    gdi_font *f = selected_font(d);
    int w = 0, fit = 0;
    for (int i = 0; i < cchString; i++)
    {
        w += glyph_of(f, (uint8_t)lpszString[i])->advance + d->extra;
        if (lpnDx) wr32((uint8_t *)lpnDx + 4 * i, (uint32_t)w);
        if (w <= nMaxExtent) fit = i + 1;
    }
    if (lpnFit) wr32(lpnFit, (uint32_t)fit);
    wr32(lpSize, (uint32_t)w);
    wr32((uint8_t *)lpSize + 4, (uint32_t)f->height);
    return 1;
}

static void text_out(gdi_dc *d, int x, int y, const uint8_t *s, int n, const int32_t *clip)
{
    gdi_font *f = selected_font(d);
    target t;
    int w = text_width(d, f, s, n);
    if (d->align & TA_UPDATECP) { x = d->cur_x; y = d->cur_y; }
    if ((d->align & TA_CENTER) == TA_CENTER) x -= w / 2;
    else if (d->align & TA_RIGHT) x -= w;
    if ((d->align & TA_BASELINE) == TA_BASELINE) y -= f->ascent;
    else if (d->align & TA_BOTTOM) y -= f->height;
    if (d->align & TA_UPDATECP) d->cur_x += w;
    if (trace()) fprintf(stderr, "GDI: text at %d,%d \"%.*s\" color 0x%06x%s%s\n", x, y, n, (const char *)s, d->text_color,
                         (d->bk_mode == OPAQUE) ? " opaque" : "", d->surface ? "" : " (window DC, not drawn)");
    if (!open_target(d, &t)) return;
    if (clip)
    {
        if (clip[0] + d->org_x > t.clip_x0) t.clip_x0 = clip[0] + d->org_x;
        if (clip[1] + d->org_y > t.clip_y0) t.clip_y0 = clip[1] + d->org_y;
        if (clip[2] + d->org_x < t.clip_x1) t.clip_x1 = clip[2] + d->org_x;
        if (clip[3] + d->org_y < t.clip_y1) t.clip_y1 = clip[3] + d->org_y;
    }
    draw_line(d, f, &t, x, y, s, n);
}

EXTERN_C uint32_t CCALL TextOutA_c(void *hdc, int32_t nXStart, int32_t nYStart, const char *lpString, int32_t cbString)
{
    gdi_dc *d = dc_of(hdc);
    if (d == NULL || lpString == NULL || cbString < 0) return 0;
    text_out(d, nXStart, nYStart, (const uint8_t *)lpString, cbString, NULL);
    return 1;
}

EXTERN_C uint32_t ExtTextOutA_c(void *hdc, int32_t x, int32_t y, uint32_t options, void *lprect, const char *lpString, uint32_t c, void *lpDx)
{
    gdi_dc *d = dc_of(hdc);
    int32_t rc[4];
    if (d == NULL) return 0;
    if (lprect) for (int i = 0; i < 4; i++) rc[i] = (int32_t)rd32((uint8_t *)lprect + 4 * i);
    if ((options & 2) && lprect)   /* ETO_OPAQUE: fill the rectangle */
    {
        target t;
        if (open_target(d, &t))
            for (int yy = rc[1]; yy < rc[3]; yy++)
                for (int xx = rc[0]; xx < rc[2]; xx++) put(&t, xx + d->org_x, yy + d->org_y, t.bg);
    }
    if (lpString && c)
    {
        int mode = d->bk_mode;
        if (options & 2) d->bk_mode = TRANSPARENT;
        text_out(d, x, y, (const uint8_t *)lpString, (int)c, ((options & 4) && lprect) ? rc : NULL);   /* ETO_CLIPPED */
        d->bk_mode = mode;
    }
    return 1;
}

/* DrawTextA: lines split at "\n" (and at word ends with DT_WORDBREAK),
 * aligned in the rectangle. Returns the height of the text. */
EXTERN_C uint32_t DrawTextA_c(void *hdc, const char *lpchText, int32_t cchText, void *lprc, uint32_t format)
{
    gdi_dc *d = dc_of(hdc);
    if (d == NULL || lpchText == NULL || lprc == NULL) return 0;
    gdi_font *f = selected_font(d);
    int32_t rc[4];
    for (int i = 0; i < 4; i++) rc[i] = (int32_t)rd32((uint8_t *)lprc + 4 * i);
    if (cchText < 0) cchText = (int32_t)strlen(lpchText);

    /* the text without "&" prefixes */
    uint8_t *text = (uint8_t *)malloc(cchText + 1);
    int n = 0;
    for (int i = 0; i < cchText; i++)
    {
        uint8_t ch = (uint8_t)lpchText[i];
        if (ch == '&' && !(format & DT_NOPREFIX))
        {
            if (i + 1 < cchText && lpchText[i + 1] == '&') i++;
            else continue;
        }
        if (ch == '\t' && (format & DT_EXPANDTABS)) ch = ' ';
        text[n++] = ch;
    }

    /* split into lines */
    int starts[256], lens[256], lines = 0, maxw = 0;
    int width = rc[2] - rc[0];
    int pos = 0;
    while (pos <= n && lines < 256)
    {
        int end = pos;
        if (format & DT_SINGLELINE) end = n;
        else while (end < n && text[end] != '\n' && text[end] != '\r') end++;
        int len = end - pos;
        if ((format & DT_WORDBREAK) && !(format & DT_SINGLELINE) && text_width(d, f, text + pos, len) > width)
        {
            int fit = 0, last_space = -1, w = 0;
            for (int i = 0; i < len; i++)
            {
                w += glyph_of(f, text[pos + i])->advance + d->extra;
                if (w > width) break;
                fit = i + 1;
                if (text[pos + i] == ' ') last_space = i;
            }
            if (last_space > 0) len = last_space;
            else if (fit > 0) len = fit;
            else len = 1;
            starts[lines] = pos;
            lens[lines] = len;
            lines++;
            pos += len;
            while (pos < n && text[pos] == ' ') pos++;
            continue;
        }
        starts[lines] = pos;
        lens[lines] = len;
        lines++;
        if (end >= n) break;
        pos = end + 1;
        if (text[end] == '\r' && pos < n && text[pos] == '\n') pos++;
    }
    for (int i = 0; i < lines; i++)
    {
        int w = text_width(d, f, text + starts[i], lens[i]);
        if (w > maxw) maxw = w;
    }
    int total = lines * f->height;

    if (format & DT_CALCRECT)
    {
        wr32((uint8_t *)lprc + 8, (uint32_t)(rc[0] + maxw));
        wr32((uint8_t *)lprc + 12, (uint32_t)(rc[1] + total));
        free(text);
        return (uint32_t)total;
    }

    int y = rc[1];
    if (format & DT_SINGLELINE)
    {
        if (format & DT_VCENTER) y = rc[1] + (rc[3] - rc[1] - f->height) / 2;
        else if (format & DT_BOTTOM) y = rc[3] - f->height;
    }
    uint32_t align = d->align;
    d->align = 0;
    for (int i = 0; i < lines; i++)
    {
        int w = text_width(d, f, text + starts[i], lens[i]);
        int x = rc[0];
        if (format & DT_CENTER) x = rc[0] + (width - w) / 2;
        else if (format & DT_RIGHT) x = rc[2] - w;
        text_out(d, x, y, text + starts[i], lens[i], (format & DT_NOCLIP) ? NULL : rc);
        y += f->height;
    }
    d->align = align;
    free(text);
    return (uint32_t)total;
}

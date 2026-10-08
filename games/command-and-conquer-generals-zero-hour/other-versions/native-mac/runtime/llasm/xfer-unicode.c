/*
 *  Generals Zero Hour: load save files with Unicode strings of either width.
 *  MIT license, see the README.md of the repository.
 *
 *  XferLoad::xferUnicodeString (0x602110 in game.dat 1.04) reads a length
 *  byte and then xferUser(buffer, 2 * length): 2 bytes for each character,
 *  as wchar_t has on Windows. GeneralsX, the port of the released source
 *  code, wrote 4 bytes for each character before 2026-10-08 (wchar_t has 4
 *  bytes on macOS), so its saves did not load in this exe ("Error loading
 *  game").
 *
 *  The replacement of the xferUser call (srw/llasm/instruction_replacements.sci)
 *  calls generals_xfer_read_unicode. It finds the width once for each file,
 *  at its first string with characters (the save description), in the same
 *  way as the GeneralsX reader: in the 4-byte format the bytes 2 and 3 are
 *  the upper half of the first character, so they are zero. In the 2-byte
 *  format they are the second character, or for a string of one character
 *  the length byte of the next string (the map label), never zero.
 *  A 4-byte string is changed to 2-byte characters in the game's buffer
 *  (a character above 0xFFFF becomes '?', so the length stays the same).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "llasm_cpu.h"
#include "guest.h"
#include "game-info.h"

EXTERN_C uint32_t fread_c(void *ptr, uint32_t size, uint32_t n, void *f);
EXTERN_C uint32_t fseek_c(void *f, int32_t offset, int32_t origin);
EXTERN_C uint32_t ftell_c(void *f);

/* The width is found again for each file. A file is read from the start
   and its position only grows, so a smaller (or the same) position, or
   another FILE, means a new file. */
static uint32_t last_file;
static int32_t last_position = -1;
static int wide4;

/* GENERALSZH_TRACE_XFERU=1: one line for each string on stderr (FILE,
   position, length, new guess or kept width, the text read in each width).
   GENERALSZH_XFERU=orig (test only): always the original read. */
static void trace(uint32_t file, int32_t position, uint32_t length, int guessed)
{
    static int on = -1, orig;
    if (on < 0)
    {
        const char *t = game_getenv("TRACE_XFERU"), *m = game_getenv("XFERU");
        on = (t != NULL && *t != 0 && *t != '0');
        orig = (m != NULL && strcmp(m, "orig") == 0);
    }
    if (!on) return;
    void *f = from_guest(file);
    unsigned char b[96] = { 0 };
    uint32_t n = length > 24 ? 24 : length;
    uint32_t got = fread_c(b, 1, 4 * n, f);
    fseek_c(f, position, SEEK_SET);
    char t2[25], t4[25];
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t c2 = (2 * i + 1 < got) ? (b[2 * i] | b[2 * i + 1] << 8) : 0;
        uint32_t c4 = (4 * i + 3 < got) ? (b[4 * i] | b[4 * i + 1] << 8 | b[4 * i + 2] << 16 | (uint32_t)b[4 * i + 3] << 24) : 0;
        t2[i] = (c2 >= 32 && c2 < 127) ? (char)c2 : '.';
        t4[i] = (c4 >= 32 && c4 < 127) ? (char)c4 : '.';
    }
    t2[n] = t4[n] = 0;
    fprintf(stderr, "xferu file=%08x pos=%06x len=%u %s width=%d probe=%02x%02x%02x%02x 2:'%s' 4:'%s'\n",
            file, position, length, guessed ? "guess" : "keep", orig ? 2 : (wide4 ? 4 : 2),
            b[0], b[1], b[2], b[3], t2, t4);
}

static int original_only(void)
{
    const char *m = game_getenv("XFERU");
    return m != NULL && strcmp(m, "orig") == 0;
}

/* Returns 1 when the string data is in the buffer, 0 when the caller must
   do the original read (it then fails at the end of the file and throws
   the game's read error). */
EXTERNC uint32_t CCALL generals_xfer_read_unicode(uint32_t self, void *buffer, uint32_t size)
{
    uint32_t file = rd32(from_guest(self + 0x10));   /* XferLoad::m_fileFP */
    void *f = from_guest(file);
    uint32_t length = size / 2;
    if (f == NULL || length == 0 || length > 255) return 0;

    int32_t position = (int32_t)ftell_c(f);
    if (position < 0) return 0;
    int guessed = (file != last_file || position <= last_position);
    if (guessed)
    {
        unsigned char probe[4] = { 0, 0, 0, 0 };
        uint32_t got = fread_c(probe, 1, 4, f);
        fseek_c(f, position, SEEK_SET);
        wide4 = (got == 4 && probe[2] == 0 && probe[3] == 0);
        last_file = file;
    }
    last_position = position;
    trace(file, position, length, guessed);
    if (original_only()) return 0;

    uint16_t *out = (uint16_t *)buffer;
    if (!wide4)
    {
        if (fread_c(out, 2 * length, 1, f) != 1)
        {
            fseek_c(f, position, SEEK_SET);
            return 0;
        }
        return 1;
    }

    uint32_t wide[255];
    if (fread_c(wide, 4 * length, 1, f) != 1)
    {
        fseek_c(f, position, SEEK_SET);
        return 0;
    }
    for (uint32_t i = 0; i < length; i++)
    {
        out[i] = (wide[i] > 0xFFFF) ? (uint16_t)'?' : (uint16_t)wide[i];
    }
    return 1;
}

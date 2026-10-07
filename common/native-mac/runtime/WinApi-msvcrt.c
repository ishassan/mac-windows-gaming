/*
 *  Port change: MSVCRT.dll and MSVCIRT.dll functions for MSVC 6 programs
 *  that import the C runtime (Generals Zero Hour). Commandos and Revenant
 *  have a static C runtime; CLIB.c replaces some of its functions.
 *
 *  Rules for this file:
 *  - Every pointer that the x86 code gets back must be in guest memory:
 *    results of host functions that point into host-only memory (static
 *    buffers of libc: localtime, asctime, strtok) are copied into buffers
 *    of this file (the program image is inside the guest address range).
 *  - Windows wide characters are 16-bit (UTF-16). The wcs* functions work
 *    on uint16_t, not on the 32-bit wchar_t of macOS.
 *  - FILE: the x86 code gets a 32-byte guest FILE (the MSVC layout); the
 *    host FILE * is kept in a table. _iob[0..2] are stdin, stdout, stderr.
 *  - Paths: the same mapping as CreateFileA (CLIB_FindFile: "C:" is the
 *    game folder).
 *
 *  MIT license, see README.md.
 */

#define _FILE_OFFSET_BITS 64
#include "game-info.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <wctype.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <map>
#include <mutex>
#include <vector>
#include <string>
#include <algorithm>
#include "Game-Memory.h"
#include "CLIB.h"
#include "guest.h"
#include "printf_x86.h"
#include "llasm/llasm_cpu.h"

#ifndef EXTERN_C
#define EXTERN_C extern "C"
#endif

EXTERN_C uint32_t CallX86Function(uint32_t address, int nargs, const uint32_t *args);
EXTERN_C uint32_t CallX86FunctionRegs(uint32_t address, int nargs, const uint32_t *args, uint32_t ecx_value, uint32_t ebp_value);
EXTERN_C void *GetCommandLineA_c(void);

typedef uint16_t wch;   /* a Windows wide character */

static char *guest_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *)x86_malloc((unsigned int)n);
    memcpy(d, s, n);
    return d;
}

static int trace_crt(void)
{
    static int t = -1;
    if (t < 0)
    {
        const char *v = game_getenv("TRACE_CRT");
        t = (v != NULL && *v != 0 && *v != '0');
    }
    return t;
}

/* ================================================================ data */

/* The data imports (gen_glue.py "d" lines): the x86 code reads them through
   the import table, so the symbols <name>_asm2c are the variables. */
#define IOB_ENTRIES 20
EXTERN_C { uint8_t _iob_asm2c[32 * IOB_ENTRIES] __attribute__((aligned(16))); }
EXTERN_C { uint32_t _acmdln_asm2c; }
EXTERN_C { uint32_t _pctype_asm2c; }
EXTERN_C { uint32_t __mb_cur_max_asm2c = 1; }
EXTERN_C { uint32_t _adjust_fdiv_asm2c = 0; }

/* MSVC ctype flags */
#define CT_UPPER   0x01
#define CT_LOWER   0x02
#define CT_DIGIT   0x04
#define CT_SPACE   0x08
#define CT_PUNCT   0x10
#define CT_CONTROL 0x20
#define CT_BLANK   0x40
#define CT_HEX     0x80
#define CT_ALPHA   0x100

/* _ctype[0] is for EOF (-1); _pctype points to _ctype + 1 */
static uint16_t ctype_table[257];

static void init_ctype(void)
{
    for (int c = 0; c < 256; c++)
    {
        uint16_t f = 0;
        if (c < 128)
        {
            if (isupper(c)) f |= CT_UPPER | CT_ALPHA;
            if (islower(c)) f |= CT_LOWER | CT_ALPHA;
            if (isdigit(c)) f |= CT_DIGIT;
            if (isspace(c)) f |= CT_SPACE;
            if (ispunct(c)) f |= CT_PUNCT;
            if (iscntrl(c)) f |= CT_CONTROL;
            if (c == ' ' || c == '\t') f |= CT_BLANK;
            if (isxdigit(c)) f |= CT_HEX;
        }
        ctype_table[c + 1] = f;
    }
    _pctype_asm2c = to_guest(&ctype_table[1]);
}

static int ms_ctype(int c)
{
    if (c < -1 || c > 255) return 0;
    return ctype_table[c + 1];
}

/* ================================================================ FILE */

static std::mutex files_lock;
static std::map<uint32_t, FILE *> files;            /* guest FILE -> host */
static std::map<uint32_t, int> file_binary;         /* opened with "b" */

static void init_iob(void)
{
    std::lock_guard<std::mutex> g(files_lock);
    files[to_guest(&_iob_asm2c[0])] = stdin;
    files[to_guest(&_iob_asm2c[32])] = stdout;
    files[to_guest(&_iob_asm2c[64])] = stderr;
    for (int i = 0; i < 3; i++) wr32(&_iob_asm2c[32 * i + 16], i);   /* _file */
}

static FILE *host_file(void *f)
{
    if (f == NULL) return NULL;
    std::lock_guard<std::mutex> g(files_lock);
    auto it = files.find(to_guest(f));
    return (it == files.end()) ? NULL : it->second;
}

static int is_binary(void *f)
{
    std::lock_guard<std::mutex> g(files_lock);
    auto it = file_binary.find(to_guest(f));
    return (it != file_binary.end()) && it->second;
}

/* host path of a Windows path */
static std::string host_path(const char *path)
{
    char buf[4096];
    if (strlen(path) >= sizeof(buf)) return std::string(path);
    CLIB_FindFile(path, buf);
    return std::string(buf);
}

EXTERN_C void *fopen_c(const char *filename, const char *mode)
{
    if (filename == NULL || mode == NULL) return NULL;
    std::string p = host_path(filename);
    char m[8];
    int k = 0;
    int binary = 0;
    for (const char *s = mode; *s && k < 6; s++)
    {
        if (*s == 't') continue;            /* text mode: same bytes on macOS */
        if (*s == 'b') binary = 1;
        if (*s == 'r' || *s == 'w' || *s == 'a' || *s == '+' || *s == 'b') m[k++] = *s;
    }
    m[k] = 0;
    FILE *h = fopen(p.c_str(), m);
    if (trace_crt()) fprintf(stderr, "fopen(%s, %s) -> %s\n", filename, mode, h ? "ok" : "FAILED");
    if (h == NULL) return NULL;
    uint8_t *gf = (uint8_t *)x86_calloc(1, 32);
    std::lock_guard<std::mutex> g(files_lock);
    files[to_guest(gf)] = h;
    file_binary[to_guest(gf)] = binary;
    wr32(gf + 16, (uint32_t)fileno(h));
    return gf;
}

EXTERN_C uint32_t fclose_c(void *f)
{
    FILE *h = host_file(f);
    if (h == NULL) return (uint32_t)-1;
    int r = 0;
    if (h != stdin && h != stdout && h != stderr)
    {
        r = fclose(h);
        std::lock_guard<std::mutex> g(files_lock);
        files.erase(to_guest(f));
        file_binary.erase(to_guest(f));
        x86_free(f);
    }
    return (uint32_t)r;
}

EXTERN_C uint32_t fflush_c(void *f)
{
    if (f == NULL) return (uint32_t)fflush(NULL);
    FILE *h = host_file(f);
    return h ? (uint32_t)fflush(h) : (uint32_t)-1;
}

EXTERN_C uint32_t fgetc_c(void *f)
{
    FILE *h = host_file(f);
    return h ? (uint32_t)fgetc(h) : (uint32_t)-1;
}

EXTERN_C uint32_t fputc_c(int32_t c, void *f)
{
    FILE *h = host_file(f);
    return h ? (uint32_t)fputc(c, h) : (uint32_t)-1;
}

EXTERN_C void *fgets_c(char *s, int32_t n, void *f)
{
    FILE *h = host_file(f);
    if (h == NULL) return NULL;
    char *r = fgets(s, n, h);
    if (r == NULL) return NULL;
    if (!is_binary(f))
    {
        /* text mode: "\r\n" -> "\n" as in the MSVC C runtime */
        size_t len = strlen(s);
        if (len >= 2 && s[len - 2] == '\r' && s[len - 1] == '\n')
        {
            s[len - 2] = '\n';
            s[len - 1] = 0;
        }
    }
    return s;
}

EXTERN_C uint32_t fread_c(void *ptr, uint32_t size, uint32_t n, void *f)
{
    FILE *h = host_file(f);
    return h ? (uint32_t)fread(ptr, size, n, h) : 0;
}

EXTERN_C uint32_t fwrite_c(const void *ptr, uint32_t size, uint32_t n, void *f)
{
    FILE *h = host_file(f);
    return h ? (uint32_t)fwrite(ptr, size, n, h) : 0;
}

EXTERN_C uint32_t fseek_c(void *f, int32_t offset, int32_t origin)
{
    FILE *h = host_file(f);
    return h ? (uint32_t)fseek(h, offset, origin) : (uint32_t)-1;
}

EXTERN_C uint32_t ftell_c(void *f)
{
    FILE *h = host_file(f);
    return h ? (uint32_t)ftell(h) : (uint32_t)-1;
}

EXTERN_C void rewind_c(void *f)
{
    FILE *h = host_file(f);
    if (h) rewind(h);
}

/* wide characters: binary files hold UTF-16; text files hold bytes */
EXTERN_C uint32_t fgetwc_c(void *f)
{
    FILE *h = host_file(f);
    if (h == NULL) return 0xffff;
    if (is_binary(f))
    {
        int a = fgetc(h);
        if (a == EOF) return 0xffff;
        int b = fgetc(h);
        if (b == EOF) return 0xffff;
        return (uint32_t)(a | (b << 8));
    }
    int c = fgetc(h);
    return (c == EOF) ? 0xffff : (uint32_t)(uint8_t)c;
}

EXTERN_C uint32_t fputwc_c(uint32_t c, void *f)
{
    FILE *h = host_file(f);
    if (h == NULL) return 0xffff;
    if (is_binary(f))
    {
        if (fputc(c & 0xff, h) == EOF || fputc((c >> 8) & 0xff, h) == EOF) return 0xffff;
        return c & 0xffff;
    }
    if (fputc((c < 256) ? (int)c : '?', h) == EOF) return 0xffff;
    return c & 0xffff;
}

/* ================================================================ printf */

EXTERN_C uint32_t vsprintf_c(char *buf, const char *format, uint32_t *ap)
{
    return (uint32_t)vsprintf_x86(buf, format, ap);
}

/* MSVC _vsnprintf: -1 when the output does not fit (no terminating 0 then) */
static int ms_vsnprintf(char *buf, uint32_t count, const char *format, uint32_t *ap)
{
    int need = vsnprintf_x86(NULL, 0, format, ap);
    if (need < 0) return -1;
    if ((uint32_t)need >= count)
    {
        if (count > 0)
        {
            std::vector<char> tmp(need + 1);
            vsnprintf_x86(tmp.data(), need + 1, format, ap);
            memcpy(buf, tmp.data(), count);
        }
        return ((uint32_t)need == count) ? need : -1;
    }
    return vsnprintf_x86(buf, count, format, ap);
}

EXTERN_C uint32_t _vsnprintf_c(char *buf, uint32_t count, const char *format, uint32_t *ap)
{
    return (uint32_t)ms_vsnprintf(buf, count, format, ap);
}

EXTERN_C uint32_t _snprintf2_c(char *buf, uint32_t count, const char *format, uint32_t *ap)
{
    return (uint32_t)ms_vsnprintf(buf, count, format, ap);
}

EXTERN_C uint32_t fprintf2_c(void *f, const char *format, uint32_t *ap)
{
    FILE *h = host_file(f);
    if (h == NULL) return (uint32_t)-1;
    int need = vsnprintf_x86(NULL, 0, format, ap);
    if (need < 0) return (uint32_t)-1;
    std::vector<char> tmp(need + 1);
    vsnprintf_x86(tmp.data(), need + 1, format, ap);
    fwrite(tmp.data(), 1, need, h);
    return (uint32_t)need;
}

/* ------------------------------------------------- wide strings */

static size_t wlen(const wch *s) { size_t n = 0; while (s[n]) n++; return n; }

/* UTF-16 -> UTF-8 */
static std::string narrow(const wch *s, size_t n = (size_t)-1)
{
    std::string out;
    if (s == NULL) return out;
    if (n == (size_t)-1) n = wlen(s);
    for (size_t i = 0; i < n; i++)
    {
        uint32_t c = s[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < n && s[i + 1] >= 0xdc00 && s[i + 1] < 0xe000)
        {
            c = 0x10000 + ((c - 0xd800) << 10) + (s[i + 1] - 0xdc00);
            i++;
        }
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) { out += (char)(0xc0 | (c >> 6)); out += (char)(0x80 | (c & 0x3f)); }
        else if (c < 0x10000) { out += (char)(0xe0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3f)); out += (char)(0x80 | (c & 0x3f)); }
        else { out += (char)(0xf0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3f)); out += (char)(0x80 | ((c >> 6) & 0x3f)); out += (char)(0x80 | (c & 0x3f)); }
    }
    return out;
}

/* UTF-8 -> UTF-16 */
static std::vector<wch> widen(const std::string &s)
{
    std::vector<wch> out;
    for (size_t i = 0; i < s.size();)
    {
        uint8_t b = (uint8_t)s[i];
        uint32_t c;
        int len;
        if (b < 0x80) { c = b; len = 1; }
        else if ((b & 0xe0) == 0xc0 && i + 1 < s.size()) { c = ((b & 0x1f) << 6) | (s[i + 1] & 0x3f); len = 2; }
        else if ((b & 0xf0) == 0xe0 && i + 2 < s.size()) { c = ((b & 0x0f) << 12) | ((s[i + 1] & 0x3f) << 6) | (s[i + 2] & 0x3f); len = 3; }
        else if ((b & 0xf8) == 0xf0 && i + 3 < s.size()) { c = ((b & 0x07) << 18) | ((s[i + 1] & 0x3f) << 12) | ((s[i + 2] & 0x3f) << 6) | (s[i + 3] & 0x3f); len = 4; }
        else { c = b; len = 1; }   /* not UTF-8: one byte */
        if (c >= 0x10000)
        {
            c -= 0x10000;
            out.push_back((wch)(0xd800 + (c >> 10)));
            out.push_back((wch)(0xdc00 + (c & 0x3ff)));
        }
        else
        {
            out.push_back((wch)c);
        }
        i += len;
    }
    return out;
}

/*
 * Wide printf: the format and the "%s" and "%c" arguments are wide (MSVC
 * rule: in a wide function, %s is a wide string and %S a narrow string).
 * The format is made narrow (UTF-8), the arguments are copied with the wide
 * strings made narrow, and vsnprintf_x86 formats it. The result is made wide.
 */
static std::vector<wch> wide_format(const wch *wformat, uint32_t *ap)
{
    std::string fmt = narrow(wformat);
    std::vector<uint32_t> args;
    std::vector<char *> temps;
    std::string nfmt;
    size_t ai = 0;

    for (size_t i = 0; i < fmt.size(); i++)
    {
        char c = fmt[i];
        nfmt += c;
        if (c != '%') continue;
        if (i + 1 < fmt.size() && fmt[i + 1] == '%') { nfmt += '%'; i++; continue; }
        /* flags, width, precision, size */
        size_t j = i + 1;
        int is64 = 0, is_h = 0, is_l = 0;
        while (j < fmt.size())
        {
            char d = fmt[j];
            if (d == '*')
            {
                args.push_back(ap[ai++]);
                nfmt += d; j++; continue;
            }
            if (strchr("-+ #0123456789.", d)) { nfmt += d; j++; continue; }
            if (d == 'I' && fmt.compare(j, 3, "I64") == 0) { is64 = 1; nfmt += "I64"; j += 3; continue; }
            if (d == 'h') { is_h = 1; j++; continue; }
            if (d == 'l') { is_l = 1; nfmt += d; j++; continue; }
            if (d == 'L') { nfmt += d; j++; continue; }
            break;
        }
        if (j >= fmt.size()) break;
        char conv = fmt[j];
        i = j;
        switch (conv)
        {
            case 's':
            case 'S':
            {
                uint32_t p = ap[ai++];
                /* %s and %ls: wide; %S and %hs: narrow */
                int wide_arg = (conv == 's' && !is_h) || (conv == 'S' && is_l);
                if (wide_arg && p != 0)
                {
                    std::string n = narrow((const wch *)from_guest(p));
                    char *t = guest_strdup(n.c_str());
                    temps.push_back(t);
                    p = to_guest(t);
                }
                if (is_l && !nfmt.empty() && nfmt.back() == 'l') nfmt.pop_back();
                args.push_back(p);
                nfmt += 's';
                break;
            }
            case 'c':
            case 'C':
            {
                uint32_t v = ap[ai++];
                std::string n = narrow((const wch *)&v, 1);
                char *t = guest_strdup(n.c_str());
                temps.push_back(t);
                if (is_l && !nfmt.empty() && nfmt.back() == 'l') nfmt.pop_back();
                args.push_back(to_guest(t));
                nfmt += 's';
                break;
            }
            case 'e': case 'E': case 'f': case 'g': case 'G': case 'a': case 'A':
                args.push_back(ap[ai++]);
                args.push_back(ap[ai++]);
                nfmt += conv;
                break;
            default:
                args.push_back(ap[ai++]);
                if (is64) args.push_back(ap[ai++]);
                nfmt += conv;
                break;
        }
    }

    int need = vsnprintf_x86(NULL, 0, nfmt.c_str(), args.data());
    std::string out;
    if (need > 0)
    {
        std::vector<char> tmp(need + 1);
        vsnprintf_x86(tmp.data(), need + 1, nfmt.c_str(), args.data());
        out.assign(tmp.data(), need);
    }
    for (char *t : temps) x86_free(t);
    return widen(out);
}

EXTERN_C uint32_t _vsnwprintf_c(wch *buf, uint32_t count, const wch *format, uint32_t *ap)
{
    std::vector<wch> w = wide_format(format, ap);
    size_t n = w.size();
    if (n >= count)
    {
        if (count > 0) memcpy(buf, w.data(), 2 * count);
        return (n == count) ? (uint32_t)n : (uint32_t)-1;
    }
    memcpy(buf, w.data(), 2 * n);
    buf[n] = 0;
    return (uint32_t)n;
}

EXTERN_C uint32_t fwprintf2_c(void *f, const wch *format, uint32_t *ap)
{
    FILE *h = host_file(f);
    if (h == NULL) return (uint32_t)-1;
    std::vector<wch> w = wide_format(format, ap);
    if (is_binary(f))
    {
        fwrite(w.data(), 2, w.size(), h);
    }
    else
    {
        std::string n = narrow(w.data(), w.size());
        fwrite(n.data(), 1, n.size(), h);
    }
    return (uint32_t)w.size();
}

/* ================================================================ scanf */

/*
 * scanf on guest arguments. Each conversion runs through the host sscanf
 * (with %n for the consumed length) into a host value that is then stored
 * through the guest pointer with the MSVC size.
 */
struct scan_src {
    virtual ~scan_src() {}
    virtual int peek() = 0;
    virtual void advance(size_t n) = 0;
    virtual const char *rest() = 0;    /* the remaining input as a string */
};

struct scan_string : scan_src {
    const char *s;
    explicit scan_string(const char *p) : s(p) {}
    int peek() override { return (uint8_t)*s ? (uint8_t)*s : EOF; }
    void advance(size_t n) override { s += n; }
    const char *rest() override { return s; }
};

/* a FILE: reads a line at a time into a buffer (fscanf of the game reads
   numbers and words of text files); unread bytes go back with fseek */
struct scan_file : scan_src {
    FILE *h;
    std::string buf;
    size_t pos = 0;
    explicit scan_file(FILE *f) : h(f) {}
    bool fill()
    {
        if (pos < buf.size()) return true;
        buf.clear();
        pos = 0;
        int c;
        while ((c = fgetc(h)) != EOF)
        {
            buf += (char)c;
            if (c == '\n') break;
        }
        return !buf.empty();
    }
    int peek() override { return fill() ? (uint8_t)buf[pos] : EOF; }
    void advance(size_t n) override { pos += n; }
    const char *rest() override { fill(); return buf.c_str() + pos; }
    ~scan_file() override
    {
        long back = (long)(buf.size() - pos);
        if (back > 0) fseek(h, -back, SEEK_CUR);
    }
};

static int do_scan(scan_src &in, const char *format, uint32_t *ap)
{
    int assigned = 0;
    int any_input = 0;
    size_t ai = 0;

    for (const char *f = format; *f;)
    {
        if (isspace((uint8_t)*f))
        {
            while (isspace((uint8_t)*f)) f++;
            while (in.peek() != EOF && isspace(in.peek())) in.advance(1);
            continue;
        }
        if (*f != '%' || f[1] == '%')
        {
            char want = (*f == '%') ? '%' : *f;
            f += (*f == '%') ? 2 : 1;
            if (in.peek() != (uint8_t)want) break;
            in.advance(1);
            continue;
        }

        /* one conversion: %[*][width][size]type */
        f++;
        int suppress = 0;
        if (*f == '*') { suppress = 1; f++; }
        const char *width_start = f;
        while (isdigit((uint8_t)*f)) f++;
        std::string width(width_start, f - width_start);
        int size_h = 0, size_l = 0, size_ll = 0;
        if (*f == 'h') { size_h = 1; f++; }
        else if (*f == 'l') { size_l = 1; f++; if (*f == 'l') { size_ll = 1; f++; } }
        else if (*f == 'L') { size_l = 1; f++; }
        else if (f[0] == 'I' && f[1] == '6' && f[2] == '4') { size_ll = 1; f += 3; }
        char type = *f;
        /* host spec: no size letters (the sizes are handled here) */
        std::string hs = std::string("%") + (suppress ? "*" : "") + width;
        if (type == '[')
        {
            const char *e = f + 1;
            if (*e == '^') e++;
            if (*e == ']') e++;
            while (*e && *e != ']') e++;
            hs.append(f, (*e ? e + 1 : e) - f);
            f = (*e) ? e + 1 : e;
        }
        else
        {
            hs += type;
            f++;
        }
        hs += "%n";

        if (type != 'n' && type != 'c' && type != '[')
        {
            if (in.peek() == EOF) break;
        }

        const char *src = in.rest();
        int consumed = -1;
        int r = 0;
        if (type == 'n')
        {
            /* not a conversion: the count of bytes read so far is not kept
               across sources; store 0 */
            if (!suppress) wr32(from_guest(ap[ai++]), 0);
            continue;
        }
        else if (strchr("di", type))
        {
            long long v = 0;
            std::string h2 = hs; h2.insert(h2.size() - 3, "ll");
            r = suppress ? sscanf(src, hs.c_str(), &consumed) : sscanf(src, h2.c_str(), &v, &consumed);
            if (r >= 1 && consumed >= 0 && !suppress)
            {
                void *p = from_guest(ap[ai++]);
                if (size_h) wr16(p, (uint16_t)v);
                else if (size_ll) { wr32(p, (uint32_t)v); wr32((uint8_t *)p + 4, (uint32_t)((unsigned long long)v >> 32)); }
                else wr32(p, (uint32_t)v);
            }
        }
        else if (strchr("uoxX", type))
        {
            unsigned long long v = 0;
            std::string h2 = hs; h2.insert(h2.size() - 3, "ll");
            r = suppress ? sscanf(src, hs.c_str(), &consumed) : sscanf(src, h2.c_str(), &v, &consumed);
            if (r >= 1 && consumed >= 0 && !suppress)
            {
                void *p = from_guest(ap[ai++]);
                if (size_h) wr16(p, (uint16_t)v);
                else if (size_ll) { wr32(p, (uint32_t)v); wr32((uint8_t *)p + 4, (uint32_t)(v >> 32)); }
                else wr32(p, (uint32_t)v);
            }
        }
        else if (strchr("eEfgGaA", type))
        {
            double v = 0;
            std::string h2 = hs; h2.insert(h2.size() - 3, "l");
            r = suppress ? sscanf(src, hs.c_str(), &consumed) : sscanf(src, h2.c_str(), &v, &consumed);
            if (r >= 1 && consumed >= 0 && !suppress)
            {
                void *p = from_guest(ap[ai++]);
                if (size_l) memcpy(p, &v, 8);
                else { float fv = (float)v; memcpy(p, &fv, 4); }
            }
        }
        else if (type == 's' || type == 'c' || type == '[')
        {
            if (suppress)
            {
                r = sscanf(src, hs.c_str(), &consumed);
            }
            else
            {
                char *p = (char *)from_guest(ap[ai++]);
                r = sscanf(src, hs.c_str(), p, &consumed);
            }
        }
        else if (type == 'p')
        {
            unsigned int v = 0;
            r = suppress ? sscanf(src, "%*x%n", &consumed) : sscanf(src, "%x%n", &v, &consumed);
            if (r >= 1 && !suppress) wr32(from_guest(ap[ai++]), v);
        }
        else
        {
            fprintf(stderr, "scanf: unsupported conversion %%%c in \"%s\"\n", type, format);
            break;
        }

        if (consumed < 0) break;      /* the conversion failed */
        any_input = 1;
        in.advance(consumed);
        if (!suppress) assigned++;
    }
    if (assigned == 0 && !any_input && in.peek() == EOF) return -1;
    return assigned;
}

int32_t CCALL sscanf2_c(const char *str, const char *format, uint32_t *ap)
{
    scan_string s(str);
    return (uint32_t)do_scan(s, format, ap);
}

EXTERN_C uint32_t fscanf2_c(void *f, const char *format, uint32_t *ap)
{
    FILE *h = host_file(f);
    if (h == NULL) return (uint32_t)-1;
    scan_file s(h);
    return (uint32_t)do_scan(s, format, ap);
}

/* swscanf: the input and the format are made narrow (UTF-8); %s, %c and %[
   (wide destinations) are made wide again */
EXTERN_C uint32_t swscanf2_c(const wch *str, const wch *format, uint32_t *ap)
{
    std::string in = narrow(str);
    std::string fmt = narrow(format);
    /* count the arguments and find the wide string destinations */
    std::vector<uint32_t> args;
    std::vector<std::pair<size_t, char *>> wide_dest;    /* arg index, temp */
    size_t ai = 0;
    for (size_t i = 0; i < fmt.size(); i++)
    {
        if (fmt[i] != '%') continue;
        if (i + 1 < fmt.size() && fmt[i + 1] == '%') { i++; continue; }
        size_t j = i + 1;
        int suppress = 0, narrow_dest = 0;
        if (j < fmt.size() && fmt[j] == '*') { suppress = 1; j++; }
        while (j < fmt.size() && isdigit((uint8_t)fmt[j])) j++;
        if (j < fmt.size() && fmt[j] == 'h') { narrow_dest = 1; j++; }
        while (j < fmt.size() && strchr("lLI64", fmt[j])) j++;
        if (j >= fmt.size()) break;
        char t = fmt[j];
        i = j;
        if (suppress) continue;
        if (t == 's' || t == 'c' || t == '[' || t == 'S' || t == 'C')
        {
            int wide = (t == 's' || t == 'c' || t == '[') && !narrow_dest;
            if (wide)
            {
                char *tmp = (char *)x86_calloc(1, (unsigned int)(in.size() + 2));
                wide_dest.push_back(std::make_pair(ai, tmp));
                args.push_back(to_guest(tmp));
            }
            else
            {
                args.push_back(ap[ai]);
            }
            if (t == 'S') fmt[j] = 's';
            if (t == 'C') fmt[j] = 'c';
        }
        else
        {
            args.push_back(ap[ai]);
        }
        ai++;
    }
    scan_string s(in.c_str());
    int r = do_scan(s, fmt.c_str(), args.data());
    for (auto &d : wide_dest)
    {
        std::vector<wch> w = widen(std::string(d.second));
        wch *dst = (wch *)from_guest(ap[d.first]);
        memcpy(dst, w.data(), 2 * w.size());
        dst[w.size()] = 0;
        x86_free(d.second);
    }
    return (uint32_t)r;
}

/* ================================================================ strings */

static char *G2H(uint32_t v) { return (char *)from_guest(v); }

EXTERN_C void *strchr_c(char *s, int32_t c) { return strchr(s, c); }
EXTERN_C void *strrchr_c(char *s, int32_t c) { return strrchr(s, c); }
EXTERN_C void *strstr_c(char *s1, const char *s2) { return strstr(s1, s2); }
EXTERN_C void *strpbrk_c(char *s1, const char *s2) { return strpbrk(s1, s2); }
EXTERN_C uint32_t strspn_c(const char *s1, const char *s2) { return (uint32_t)strspn(s1, s2); }
EXTERN_C uint32_t strcspn_c(const char *s1, const char *s2) { return (uint32_t)strcspn(s1, s2); }
EXTERN_C uint32_t _strnicmp_c(const char *s1, const char *s2, uint32_t n) { return (uint32_t)strncasecmp(s1, s2, n); }
EXTERN_C void *_strdup_c(const char *s) { return s ? guest_strdup(s) : NULL; }
EXTERN_C void *_strlwr_c(char *s) { for (char *p = s; *p; p++) *p = (char)tolower((uint8_t)*p); return s; }
EXTERN_C void *_strupr_c(char *s) { for (char *p = s; *p; p++) *p = (char)toupper((uint8_t)*p); return s; }
EXTERN_C uint32_t _mbscmp_c(const char *s1, const char *s2) { return (uint32_t)strcmp(s1, s2); }
EXTERN_C uint32_t _mbslen_c(const char *s) { return (uint32_t)strlen(s); }
EXTERN_C uint32_t _mbsnccnt_c(const char *s, uint32_t n) { size_t l = strnlen(s, n); return (uint32_t)l; }

static thread_local char *strtok_next;

EXTERN_C void *strtok_c(char *s, const char *delim)
{
    if (s == NULL) s = strtok_next;
    if (s == NULL) return NULL;
    s += strspn(s, delim);
    if (*s == 0) { strtok_next = NULL; return NULL; }
    char *e = s + strcspn(s, delim);
    if (*e) { *e = 0; strtok_next = e + 1; }
    else strtok_next = NULL;
    return s;
}

EXTERN_C uint32_t strtol_c(const char *s, uint32_t *endptr, int32_t base)
{
    char *e;
    long v = strtol(s, &e, base);
    if (v > INT32_MAX) v = INT32_MAX;
    if (v < INT32_MIN) v = INT32_MIN;
    if (endptr) wr32(endptr, to_guest(e));
    return (uint32_t)(int32_t)v;
}

EXTERN_C uint32_t strtoul_c(const char *s, uint32_t *endptr, int32_t base)
{
    char *e;
    unsigned long v = strtoul(s, &e, base);
    if (v > UINT32_MAX) v = UINT32_MAX;
    if (endptr) wr32(endptr, to_guest(e));
    return (uint32_t)v;
}

EXTERN_C uint32_t atoi_c(const char *s) { return (uint32_t)atoi(s); }

/* atof: the result goes on the x87 stack */
EXTERN_C void atof_c(_cpu *cpu, const char *s)
{
    cpu->_st_top = (cpu->_st_top - 1) & 7;
    cpu->_st[cpu->_st_top] = atof(s);
}

EXTERN_C void *_itoa_c(int32_t value, char *str, int32_t radix)
{
    char tmp[40];
    uint32_t v;
    int neg = 0, i = 0;
    if (radix == 10 && value < 0) { neg = 1; v = (uint32_t)(-(int64_t)value); }
    else v = (uint32_t)value;
    if (radix < 2 || radix > 36) radix = 10;
    do { int d = v % radix; tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= radix; } while (v);
    char *p = str;
    if (neg) *p++ = '-';
    while (i) *p++ = tmp[--i];
    *p = 0;
    return str;
}

EXTERN_C uint32_t tolower_c(int32_t c) { return (c >= 'A' && c <= 'Z') ? (uint32_t)(c + 32) : (uint32_t)c; }
EXTERN_C uint32_t isalnum_c(int32_t c) { return ms_ctype(c) & (CT_ALPHA | CT_DIGIT); }
EXTERN_C uint32_t isalpha_c(int32_t c) { return ms_ctype(c) & CT_ALPHA; }
EXTERN_C uint32_t isdigit_c(int32_t c) { return ms_ctype(c) & CT_DIGIT; }
EXTERN_C uint32_t isspace_c(int32_t c) { return ms_ctype(c) & CT_SPACE; }
EXTERN_C uint32_t _isctype_c(int32_t c, int32_t mask) { return ms_ctype(c) & mask; }

EXTERN_C void *memmove_c(void *d, const void *s, uint32_t n) { return memmove(d, s, n); }

EXTERN_C void *realloc_c(void *p, uint32_t size)
{
    if (p == NULL) return x86_malloc(size);
    if (size == 0) { x86_free(p); return NULL; }
    return x86_realloc(p, size);
}

/* ------------------------------------------------- wide strings */

EXTERN_C uint32_t wcslen_c(const wch *s) { return (uint32_t)wlen(s); }

EXTERN_C void *wcscpy_c(wch *d, const wch *s)
{
    size_t i = 0;
    do { d[i] = s[i]; } while (s[i++]);
    return d;
}

EXTERN_C void *wcsncpy_c(wch *d, const wch *s, uint32_t n)
{
    uint32_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

EXTERN_C void *wcscat_c(wch *d, const wch *s)
{
    wcscpy_c(d + wlen(d), s);
    return d;
}

EXTERN_C uint32_t wcscmp_c(const wch *a, const wch *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (uint32_t)((*a > *b) - (*a < *b));
}

static wch wlower(wch c) { return (c < 0x80) ? (wch)tolower(c) : (wch)towlower(c); }

EXTERN_C uint32_t _wcsicmp_c(const wch *a, const wch *b)
{
    while (*a && wlower(*a) == wlower(*b)) { a++; b++; }
    wch x = wlower(*a), y = wlower(*b);
    return (uint32_t)((x > y) - (x < y));
}

EXTERN_C void *wcschr_c(wch *s, uint32_t c)
{
    for (;; s++)
    {
        if (*s == (wch)c) return s;
        if (*s == 0) return NULL;
    }
}

EXTERN_C void *wcsrchr_c(wch *s, uint32_t c)
{
    wch *last = NULL;
    for (;; s++)
    {
        if (*s == (wch)c) last = s;
        if (*s == 0) return last;
    }
}

EXTERN_C void *wcsstr_c(wch *s1, const wch *s2)
{
    size_t n = wlen(s2);
    if (n == 0) return s1;
    for (; *s1; s1++)
    {
        if (memcmp(s1, s2, 2 * n) == 0) return s1;
    }
    return NULL;
}

EXTERN_C uint32_t wcsspn_c(const wch *s, const wch *set)
{
    uint32_t n = 0;
    for (; s[n]; n++)
    {
        const wch *p = set;
        while (*p && *p != s[n]) p++;
        if (*p == 0) break;
    }
    return n;
}

EXTERN_C uint32_t wcscspn_c(const wch *s, const wch *set)
{
    uint32_t n = 0;
    for (; s[n]; n++)
    {
        const wch *p = set;
        while (*p && *p != s[n]) p++;
        if (*p) break;
    }
    return n;
}

EXTERN_C uint32_t _wtoi_c(const wch *s)
{
    return (uint32_t)atoi(narrow(s).c_str());
}

EXTERN_C uint32_t iswalnum_c(uint32_t c) { c &= 0xffff; return (c < 0x80) ? (uint32_t)(isalnum(c) != 0) : (uint32_t)(iswalnum(c) != 0); }
EXTERN_C uint32_t iswalpha_c(uint32_t c) { c &= 0xffff; return (c < 0x80) ? (uint32_t)(isalpha(c) != 0) : (uint32_t)(iswalpha(c) != 0); }
EXTERN_C uint32_t iswascii_c(uint32_t c) { return (c & 0xffff) < 0x80; }
EXTERN_C uint32_t iswdigit_c(uint32_t c) { c &= 0xffff; return c >= '0' && c <= '9'; }
EXTERN_C uint32_t iswspace_c(uint32_t c) { c &= 0xffff; return (c < 0x80) ? (uint32_t)(isspace(c) != 0) : (uint32_t)(iswspace(c) != 0); }

/* ================================================================ files (low level) */

#define MS_O_WRONLY 0x0001
#define MS_O_RDWR   0x0002
#define MS_O_APPEND 0x0008
#define MS_O_CREAT  0x0100
#define MS_O_TRUNC  0x0200
#define MS_O_EXCL   0x0400

EXTERN_C uint32_t _open2_c(const char *path, uint32_t oflag, uint32_t *ap)
{
    int flags = (oflag & MS_O_RDWR) ? O_RDWR : (oflag & MS_O_WRONLY) ? O_WRONLY : O_RDONLY;
    if (oflag & MS_O_APPEND) flags |= O_APPEND;
    if (oflag & MS_O_CREAT) flags |= O_CREAT;
    if (oflag & MS_O_TRUNC) flags |= O_TRUNC;
    if (oflag & MS_O_EXCL) flags |= O_EXCL;
    std::string p = host_path(path);
    int fd = open(p.c_str(), flags, 0644);
    if (trace_crt()) fprintf(stderr, "_open(%s, 0x%x) -> %d\n", path, oflag, fd);
    return (uint32_t)fd;
}

EXTERN_C uint32_t _close_c(int32_t fd) { return (uint32_t)close(fd); }
EXTERN_C uint32_t _read_c(int32_t fd, void *buf, uint32_t n) { return (uint32_t)read(fd, buf, n); }
EXTERN_C uint32_t _write_c(int32_t fd, const void *buf, uint32_t n) { return (uint32_t)write(fd, buf, n); }
EXTERN_C uint32_t _lseek_c(int32_t fd, int32_t off, int32_t origin) { return (uint32_t)lseek(fd, off, origin); }

/* MSVC 6 struct _stat (36 bytes) */
static void fill_ms_stat(uint8_t *b, const struct stat *st)
{
    memset(b, 0, 36);
    uint16_t mode = 0x0100 | 0x0040;                      /* read, exec */
    if (st->st_mode & S_IWUSR) mode |= 0x0080;
    if (S_ISDIR(st->st_mode)) mode |= 0x4000;
    else mode |= 0x8000;
    wr32(b + 0, 2);                                       /* drive C: */
    wr16(b + 6, mode);
    wr16(b + 8, 1);
    wr32(b + 16, 2);
    wr32(b + 20, (uint32_t)st->st_size);
    wr32(b + 24, (uint32_t)st->st_atime);
    wr32(b + 28, (uint32_t)st->st_mtime);
    wr32(b + 32, (uint32_t)st->st_ctime);
}

EXTERN_C uint32_t _stat_c(const char *path, void *buf)
{
    struct stat st;
    std::string p = host_path(path);
    if (stat(p.c_str(), &st) != 0) return (uint32_t)-1;
    fill_ms_stat((uint8_t *)buf, &st);
    return 0;
}

EXTERN_C uint32_t _fstat_c(int32_t fd, void *buf)
{
    struct stat st;
    if (fstat(fd, &st) != 0) return (uint32_t)-1;
    fill_ms_stat((uint8_t *)buf, &st);
    return 0;
}

EXTERN_C uint32_t _access_c(const char *path, int32_t mode)
{
    std::string p = host_path(path);
    int m = F_OK;
    if (mode & 2) m |= W_OK;
    if (mode & 4) m |= R_OK;
    int r = access(p.c_str(), m);
    if (trace_crt()) fprintf(stderr, "_access(%s, %d) -> %d\n", path, mode, r);
    return (uint32_t)r;
}

EXTERN_C uint32_t _chmod_c(const char *path, int32_t mode) { return 0; }

EXTERN_C uint32_t _mkdir_c(const char *path)
{
    std::string p = host_path(path);
    return (uint32_t)mkdir(p.c_str(), 0755);
}

EXTERN_C uint32_t remove_c(const char *path)
{
    std::string p = host_path(path);
    return (uint32_t)remove(p.c_str());
}

EXTERN_C uint32_t rename_c(const char *a, const char *b)
{
    std::string pa = host_path(a), pb = host_path(b);
    return (uint32_t)rename(pa.c_str(), pb.c_str());
}

/* the game folder is drive C: (see GetCurrentDirectoryA) */
EXTERN_C void *_getcwd_c(char *buf, int32_t size)
{
    const char *cwd = "C:\\";
    if (buf == NULL)
    {
        return guest_strdup(cwd);
    }
    if (size < 4) return NULL;
    strcpy(buf, cwd);
    return buf;
}

EXTERN_C void _splitpath_c(const char *path, char *drive, char *dir, char *fname, char *ext)
{
    const char *p = path;
    if (p[0] && p[1] == ':')
    {
        if (drive) { drive[0] = p[0]; drive[1] = ':'; drive[2] = 0; }
        p += 2;
    }
    else if (drive) drive[0] = 0;
    const char *last_slash = NULL, *last_dot = NULL;
    for (const char *q = p; *q; q++)
    {
        if (*q == '\\' || *q == '/') last_slash = q;
        else if (*q == '.') last_dot = q;
    }
    const char *name = last_slash ? last_slash + 1 : p;
    if (dir)
    {
        size_t n = name - p;
        memcpy(dir, p, n);
        dir[n] = 0;
    }
    if (last_dot == NULL || last_dot < name) last_dot = name + strlen(name);
    if (fname)
    {
        size_t n = last_dot - name;
        memcpy(fname, name, n);
        fname[n] = 0;
    }
    if (ext) strcpy(ext, last_dot);
}

EXTERN_C uint32_t _spawnl2_c(int32_t mode, const char *cmdname, uint32_t *ap)
{
    fprintf(stderr, "_spawnl(%s): starting other programs is not supported\n", cmdname ? cmdname : "");
    return (uint32_t)-1;
}

/* ================================================================ time */

/* MSVC struct tm: 9 ints, the same order as the C standard */
static void to_ms_tm(uint8_t *d, const struct tm *t)
{
    int v[9] = { t->tm_sec, t->tm_min, t->tm_hour, t->tm_mday, t->tm_mon, t->tm_year, t->tm_wday, t->tm_yday, t->tm_isdst };
    for (int i = 0; i < 9; i++) wr32(d + 4 * i, (uint32_t)v[i]);
}

static void from_ms_tm(struct tm *t, const uint8_t *s)
{
    memset(t, 0, sizeof(*t));
    t->tm_sec = (int)rd32(s + 0); t->tm_min = (int)rd32(s + 4); t->tm_hour = (int)rd32(s + 8);
    t->tm_mday = (int)rd32(s + 12); t->tm_mon = (int)rd32(s + 16); t->tm_year = (int)rd32(s + 20);
    t->tm_wday = (int)rd32(s + 24); t->tm_yday = (int)rd32(s + 28); t->tm_isdst = (int)rd32(s + 32);
}

EXTERN_C uint32_t time_c(uint32_t *t)
{
    uint32_t now = (uint32_t)time(NULL);
    if (t) wr32(t, now);
    return now;
}

static thread_local uint8_t *tm_buffer;

EXTERN_C void *localtime_c(const uint32_t *timer)
{
    if (timer == NULL) return NULL;
    time_t t = (time_t)(int32_t)rd32(timer);
    struct tm host;
    if (localtime_r(&t, &host) == NULL) return NULL;
    if (tm_buffer == NULL) tm_buffer = (uint8_t *)x86_calloc(1, 36);
    to_ms_tm(tm_buffer, &host);
    return tm_buffer;
}

static thread_local char *asctime_buffer;

EXTERN_C void *asctime_c(const void *tmp)
{
    struct tm t;
    from_ms_tm(&t, (const uint8_t *)tmp);
    char buf[64];
    asctime_r(&t, buf);
    if (asctime_buffer == NULL) asctime_buffer = (char *)x86_calloc(1, 64);
    strncpy(asctime_buffer, buf, 63);
    return asctime_buffer;
}

EXTERN_C uint32_t strftime_c(char *s, uint32_t max, const char *format, const void *tmp)
{
    struct tm t;
    from_ms_tm(&t, (const uint8_t *)tmp);
    /* MSVC "%#x" style flags: drop the '#' */
    std::string f;
    for (const char *p = format; *p; p++)
    {
        f += *p;
        if (p[0] == '%' && p[1] == '#') p++;
    }
    return (uint32_t)strftime(s, max, f.c_str(), &t);
}

/* MSVC: CLOCKS_PER_SEC is 1000 */
EXTERN_C uint32_t clock_c(void)
{
    static struct timeval start;
    static int started;
    struct timeval now;
    gettimeofday(&now, NULL);
    if (!started) { start = now; started = 1; }
    return (uint32_t)((now.tv_sec - start.tv_sec) * 1000 + (now.tv_usec - start.tv_usec) / 1000);
}

/* ================================================================ math, FPU */

static double make_double(uint32_t lo, uint32_t hi)
{
    uint64_t u = ((uint64_t)hi << 32) | lo;
    double d;
    memcpy(&d, &u, 8);
    return d;
}

EXTERN_C uint32_t _finite_c(uint32_t lo, uint32_t hi) { return isfinite(make_double(lo, hi)) ? 1 : 0; }
EXTERN_C uint32_t _isnan_c(uint32_t lo, uint32_t hi) { return isnan(make_double(lo, hi)) ? 1 : 0; }

/* The x87 control word is not emulated (the translated code computes in
   64-bit doubles); _controlfp only keeps the value that the game sets. */
static uint32_t fp_control = 0x0009001f;

EXTERN_C uint32_t _controlfp_c(uint32_t newval, uint32_t mask)
{
    fp_control = (fp_control & ~mask) | (newval & mask);
    return fp_control;
}

EXTERN_C uint32_t _statusfp_c(void) { return 0; }
EXTERN_C void _fpreset_c(void) {}

/* ================================================================ sort */

EXTERN_C void qsort_c(void *base, uint32_t num, uint32_t width, uint32_t compare)
{
    if (num < 2 || width == 0) return;
    uint8_t *b = (uint8_t *)base;
    std::vector<uint32_t> idx(num);
    for (uint32_t i = 0; i < num; i++) idx[i] = i;
    /* the elements stay in place while the compare function runs */
    std::stable_sort(idx.begin(), idx.end(), [&](uint32_t x, uint32_t y) {
        uint32_t args[2] = { to_guest(b + (size_t)x * width), to_guest(b + (size_t)y * width) };
        return (int32_t)CallX86Function(compare, 2, args) < 0;
    });
    std::vector<uint8_t> copy(b, b + (size_t)num * width);
    for (uint32_t i = 0; i < num; i++)
    {
        memcpy(b + (size_t)i * width, copy.data() + (size_t)idx[i] * width, width);
    }
}

EXTERN_C void *bsearch_c(const void *key, const void *base, uint32_t num, uint32_t width, uint32_t compare)
{
    uint32_t lo = 0, hi = num;
    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2;
        const uint8_t *e = (const uint8_t *)base + (size_t)mid * width;
        uint32_t args[2] = { to_guest(key), to_guest(e) };
        int32_t r = (int32_t)CallX86Function(compare, 2, args);
        if (r == 0) return (void *)e;
        if (r < 0) hi = mid;
        else lo = mid + 1;
    }
    return NULL;
}

/* ================================================================ locale */

EXTERN_C void *setlocale_c(int32_t category, const char *locale)
{
    static char *c_locale;
    if (c_locale == NULL) c_locale = guest_strdup("C");
    return c_locale;
}

/* ================================================================ start-up and exit */

static uint32_t fmode_value, commode_value;
static uint32_t *fmode_ptr, *commode_ptr;
static std::vector<uint32_t> onexit_funcs;
static std::mutex onexit_lock;
static int crt_started;

static void crt_start(void)
{
    if (crt_started) return;
    crt_started = 1;
    init_ctype();
    init_iob();
}

EXTERN_C void __set_app_type_c(uint32_t type) { crt_start(); }
EXTERN_C void __setusermatherr_c(uint32_t handler) {}

EXTERN_C void *__p__fmode_c(void)
{
    if (fmode_ptr == NULL) { fmode_ptr = (uint32_t *)x86_calloc(1, 4); *fmode_ptr = fmode_value; }
    return fmode_ptr;
}

EXTERN_C void *__p__commode_c(void)
{
    if (commode_ptr == NULL) { commode_ptr = (uint32_t *)x86_calloc(1, 4); *commode_ptr = commode_value; }
    return commode_ptr;
}

static thread_local uint32_t *errno_ptr;

EXTERN_C void *_errno_c(void)
{
    if (errno_ptr == NULL) errno_ptr = (uint32_t *)x86_calloc(1, 4);
    *errno_ptr = (uint32_t)errno;
    return errno_ptr;
}

EXTERN_C uint32_t __getmainargs_c(uint32_t *argc, uint32_t *argv, uint32_t *env, uint32_t doWildCard, void *startupinfo)
{
    crt_start();
    const char *cmdline = (const char *)GetCommandLineA_c();
    _acmdln_asm2c = to_guest(cmdline);

    /* argv: split the command line at spaces (quotes keep spaces) */
    std::vector<std::string> args;
    std::string cur;
    int quoted = 0, have = 0;
    for (const char *p = cmdline; *p; p++)
    {
        if (*p == '"') { quoted = !quoted; have = 1; continue; }
        if (*p == ' ' && !quoted)
        {
            if (have) { args.push_back(cur); cur.clear(); have = 0; }
            continue;
        }
        cur += *p;
        have = 1;
    }
    if (have) args.push_back(cur);

    uint32_t *av = (uint32_t *)x86_calloc((unsigned int)args.size() + 1, 4);
    for (size_t i = 0; i < args.size(); i++) av[i] = to_guest(guest_strdup(args[i].c_str()));
    uint32_t *ev = (uint32_t *)x86_calloc(1, 4);
    if (argc) wr32(argc, (uint32_t)args.size());
    if (argv) wr32(argv, to_guest(av));
    if (env) wr32(env, to_guest(ev));
    return 0;
}

/* _initterm: call each non-NULL function pointer in [begin, end) */
EXTERN_C void _initterm_c(uint32_t *begin, uint32_t *end)
{
    crt_start();
    if (_acmdln_asm2c == 0) _acmdln_asm2c = to_guest(GetCommandLineA_c());
    for (uint32_t *p = begin; p < end; p++)
    {
        uint32_t f = rd32(p);
        if (f != 0) CallX86Function(f, 0, NULL);
    }
}

EXTERN_C uint32_t _onexit_c(uint32_t func)
{
    std::lock_guard<std::mutex> g(onexit_lock);
    onexit_funcs.push_back(func);
    return func;
}

EXTERN_C uint32_t __dllonexit_c(uint32_t func, void *pbegin, void *pend)
{
    return _onexit_c(func);
}

static void run_onexit(void)
{
    std::vector<uint32_t> funcs;
    {
        std::lock_guard<std::mutex> g(onexit_lock);
        funcs.swap(onexit_funcs);
    }
    for (auto it = funcs.rbegin(); it != funcs.rend(); ++it)
    {
        CallX86Function(*it, 0, NULL);
    }
}

EXTERN_C void exit_c(int32_t status)
{
    run_onexit();
    fflush(NULL);
    exit(status);
}

EXTERN_C void _exit_c(int32_t status)
{
    fflush(NULL);
    _exit(status);
}

/* ================================================================ C++: exception, type_info */

/*
 * MSVCIRT class "exception": {vftable, const char *_m_what, int _m_doFree}.
 * Its vftable has the scalar deleting destructor and what(); both are glue
 * procedures (imports.spec, convention "t"), so the x86 code can call them.
 */
EXTERN_C void exception_sdtor_asm2c(void);
EXTERN_C void exception_what_asm2c(void);

static uint32_t exception_vftable[2];
static char *unknown_exception_text;

static uint32_t exception_vftable_guest(void)
{
    if (exception_vftable[0] == 0)
    {
        exception_vftable[0] = to_guest((const void *)&exception_sdtor_asm2c);
        exception_vftable[1] = to_guest((const void *)&exception_what_asm2c);
        unknown_exception_text = guest_strdup("Unknown exception");
    }
    return to_guest(exception_vftable);
}

EXTERN_C void *exception_ctor_c(uint8_t *self)
{
    wr32(self, exception_vftable_guest());
    wr32(self + 4, 0);
    wr32(self + 8, 0);
    return self;
}

EXTERN_C void *exception_copy_ctor_c(uint8_t *self, const uint8_t *other)
{
    wr32(self, exception_vftable_guest());
    uint32_t what = rd32(other + 4);
    if (rd32(other + 8) && what)
    {
        wr32(self + 4, to_guest(guest_strdup((const char *)from_guest(what))));
        wr32(self + 8, 1);
    }
    else
    {
        wr32(self + 4, what);
        wr32(self + 8, 0);
    }
    return self;
}

EXTERN_C void exception_dtor_c(uint8_t *self)
{
    wr32(self, exception_vftable_guest());
    if (rd32(self + 8) && rd32(self + 4)) x86_free(from_guest(rd32(self + 4)));
    wr32(self + 4, 0);
    wr32(self + 8, 0);
}

EXTERN_C void *exception_sdtor_c(uint8_t *self, uint32_t flags)
{
    exception_dtor_c(self);
    if (flags & 1) x86_free(self);
    return self;
}

EXTERN_C void *exception_what_c(uint8_t *self)
{
    uint32_t what = rd32(self + 4);
    exception_vftable_guest();
    return what ? from_guest(what) : unknown_exception_text;
}

EXTERN_C void type_info_dtor_c(void *self) {}

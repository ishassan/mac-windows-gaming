/*
 *  Commandos port: CPU helpers that the SR support files do not have.
 *  MIT license, see README.md.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "llasm_cpu.h"

/* x87 "fldpi": push pi on the FPU stack (the stack is in cpu->_st, as doubles) */
EXTERNC void CCALL x87_fldpi_void(CPU)
{
    cpu->_st_top = (cpu->_st_top - 1) & 7;
    cpu->_st[cpu->_st_top] = 3.14159265358979323846;
}

/* Called by code that SRW could not translate (SSE2-only paths). */
EXTERNC void CCALL x86_unsupported_instruction(uint32_t address)
{
    extern void x86_print_stack_trace(unsigned int words);
    fprintf(stderr, "Fatal: untranslated x86 instruction at 0x%x was executed\n", address);
    x86_print_stack_trace(256);
    abort();
}

/* "int N": only int 3 (debug break) exists in the game code. */
EXTERNC void CCALL X86_InterruptProcedure(const uint8_t IntNum, void *regs)
{
    fprintf(stderr, "Fatal: x86 interrupt %u\n", IntNum);
    abort();
}

/* C runtime math helpers (_CIxxx: argument and result on the FPU stack) */
#include <math.h>

#define ST0 (cpu->_st[cpu->_st_top])

EXTERNC void CCALL x87_fatan_void(CPU) { ST0 = atan(ST0); }

/* ceil(double): cdecl, argument on the x86 stack, result in st0 */
static double make_double(uint32_t low, uint32_t high)
{
    union { uint64_t u; double d; } v;
    v.u = ((uint64_t)high << 32) | low;
    return v.d;
}

EXTERNC void CCALL x87_fld_ceil(CPU, uint32_t low, uint32_t high)
{
    cpu->_st_top = (cpu->_st_top - 1) & 7;
    ST0 = ceil(make_double(low, high));
}

/* The SR glue for _check_security_cookie refers to this name. */
EXTERNC uint32_t security_cookie_ = 0xbb40e64e;

/*
 * Reads from absolute addresses that SRW did not relocate. The only ones in
 * the game are C runtime reads of the PE header at ImageBase (0x400000):
 * _ValidateImageBase and _FindPESection. The translated code and data are
 * not at their original addresses, so the header given here has one
 * read-only section that covers every address. Then
 * _IsNonwritableInCurrentImage is true for all code pointers, and the C
 * runtime calls its floating point set-up (_FPinit) as on Windows.
 */
static uint8_t fake_pe_header[0x200];

static void init_fake_pe_header(void)
{
    uint8_t *h = fake_pe_header;
    if (h[0] == 'M') return;
    h[0] = 'M'; h[1] = 'Z';
    h[0x3c] = 0x80;                         /* e_lfanew */
    h[0x80] = 'P'; h[0x81] = 'E';           /* signature */
    h[0x84] = 0x4c; h[0x85] = 0x01;         /* Machine = i386 */
    h[0x86] = 1;                            /* NumberOfSections */
    h[0x94] = 0xe0;                         /* SizeOfOptionalHeader */
    h[0x98] = 0x0b; h[0x99] = 0x01;         /* Magic = PE32 */
    /* section header at 0x98 + 0xe0 = 0x178 */
    memcpy(h + 0x178, ".all", 4);
    h[0x178 + 8] = 0xff; h[0x178 + 9] = 0xff; h[0x178 + 10] = 0xff; h[0x178 + 11] = 0xff; /* VirtualSize */
    /* VirtualAddress = 0 */
    h[0x178 + 36] = 0x20; h[0x178 + 39] = 0x60;  /* Characteristics = code | read | execute */
}

EXTERNC uint32_t CCALL X86_ReadMemProcedure(const uint32_t Address, const uint32_t MemSize)
{
    uint32_t offset, value;

    init_fake_pe_header();
    offset = Address - 0x400000;
    if (offset + MemSize <= sizeof(fake_pe_header))
    {
        value = 0;
        memcpy(&value, fake_pe_header + offset, MemSize);
        return value;
    }
    fprintf(stderr, "Fatal: read from absolute address 0x%x\n", Address);
    abort();
}

EXTERNC void CCALL X86_WriteMemProcedure(const uint32_t Address, const uint32_t MemSize, const uint32_t _eax)
{
    fprintf(stderr, "Fatal: write to absolute address 0x%x\n", Address);
    abort();
}

/* Native version of the CRT _cfltcvt_l (printf %e %f %g %a). The output
 * matches the MSVC CRT: three exponent digits, and for %g the trailing
 * zeros stay (printf removes them later with _cropzeros). */
EXTERNC uint32_t CCALL cfltcvt_c(const void *arg, char *buffer, uint32_t size, int32_t format, int32_t precision, int32_t caps)
{
    double value;
    char fmt[8], tmp[512], *e;
    int n;

    if (buffer == NULL || size == 0) return 22;   /* EINVAL */
    memcpy(&value, arg, sizeof(value));

    switch (format)
    {
        case 'e': case 'E': snprintf(fmt, sizeof(fmt), "%%.*%c", caps ? 'E' : 'e'); break;
        case 'f':           snprintf(fmt, sizeof(fmt), "%%.*f"); break;
        case 'a': case 'A': snprintf(fmt, sizeof(fmt), "%%.*%c", caps ? 'A' : 'a'); break;
        default:            snprintf(fmt, sizeof(fmt), "%%#.*%c", caps ? 'G' : 'g'); break;
    }
    if (precision < 0) precision = 6;
    n = snprintf(tmp, sizeof(tmp), fmt, precision, value);
    if (n < 0 || n >= (int)sizeof(tmp)) return 34;   /* ERANGE */

    /* MSVC writes the exponent with at least three digits */
    if (format != 'f' && format != 'a' && format != 'A')
    {
        e = strpbrk(tmp, "eE");
        if (e != NULL && (e[1] == '+' || e[1] == '-') && strlen(e + 2) == 2)
        {
            memmove(e + 3, e + 2, 3);
            e[2] = '0';
        }
    }

    if (strlen(tmp) + 1 > size) return 34;
    strcpy(buffer, tmp);
    return 0;
}

/* x87 instructions that the llasm support files do not have. The FPU
 * stack is cpu->_st (doubles), the condition bits are cpu->_st_sw_cond. */
#define X87_ST(i) cpu->_st[(cpu->_st_top + (i)) & 7]
#define X87_C0 0x0100
#define X87_C1 0x0200
#define X87_C2 0x0400
#define X87_C3 0x4000

/* fxam: classify st0 */
EXTERNC void CCALL x87_fxam_void(CPU)
{
    double v = X87_ST(0);
    uint32_t c;
    switch (fpclassify(v))
    {
        case FP_NAN:       c = X87_C0; break;
        case FP_INFINITE:  c = X87_C2 | X87_C0; break;
        case FP_ZERO:      c = X87_C3; break;
        case FP_SUBNORMAL: c = X87_C3 | X87_C2; break;
        default:           c = X87_C2; break;
    }
    if (signbit(v)) c |= X87_C1;
    cpu->_st_sw_cond = c;
}

/* fprem: st0 = remainder of st0 / st1 (quotient truncated), always complete */
EXTERNC void CCALL x87_fprem_void(CPU)
{
    double a = X87_ST(0), b = X87_ST(1);
    double r = fmod(a, b);
    uint32_t c = 0;
    if (b != 0 && isfinite(a))
    {
        double q = trunc((a - r) / b);
        uint64_t qi = (uint64_t)fabs(fmod(q, 8.0));
        if (qi & 1) c |= X87_C1;
        if (qi & 2) c |= X87_C3;
        if (qi & 4) c |= X87_C0;
    }
    X87_ST(0) = r;
    cpu->_st_sw_cond = c;    /* C2 = 0: reduction is complete */
}

/* fscale: st0 = st0 * 2^trunc(st1) */
EXTERNC void CCALL x87_fscale_void(CPU)
{
    X87_ST(0) = ldexp(X87_ST(0), (int)trunc(X87_ST(1)));
}

/* fpatan: st1 = atan2(st1, st0), pop */
EXTERNC void CCALL x87_fpatan_void(CPU)
{
    X87_ST(1) = atan2(X87_ST(1), X87_ST(0));
    cpu->_st_top = (cpu->_st_top + 1) & 7;
}

/* fnstenv / fldenv: the 28-byte FPU environment (32-bit protected mode).
 * Only the control word, the status word and the tag word are kept. */
EXTERNC void CCALL x87_fnstenv_void(CPU, uint8_t *env)
{
    uint32_t v;
    memset(env, 0, 28);
    v = cpu->_st_cw & 0xffff;
    memcpy(env, &v, 4);
    v = (cpu->_st_sw_cond & 0x4700) | ((cpu->_st_top & 7) << 11);
    memcpy(env + 4, &v, 4);
    v = 0xffff;
    memcpy(env + 8, &v, 4);
    cpu->_st_cw |= 0x3f;    /* fnstenv masks all FPU exceptions */
}

EXTERNC void CCALL x87_fldenv_void(CPU, const uint8_t *env)
{
    uint32_t cw, sw;
    memcpy(&cw, env, 4);
    memcpy(&sw, env + 4, 4);
    cpu->_st_cw = cw & 0xffff;
    cpu->_st_sw_cond = sw & 0x4700;
    cpu->_st_top = (sw >> 11) & 7;
}


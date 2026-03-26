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
    fprintf(stderr, "Fatal: untranslated x86 instruction at 0x%x was executed\n", address);
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

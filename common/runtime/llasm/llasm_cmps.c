//part of static recompiler -- do not edit

/**
 *
 *  Copyright (C) 2021-2026 Roman Pauer
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is furnished to do
 *  so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 */

#include "llasm_cpu.h"

EXTERNC uint32_t CCALL x86_repe_cmpsb(CPU)
{
    int32_t dir;
    uint32_t srcptr1, srcptr2, counter, srcvalue1, srcvalue2;

    dir = (eflags & DF)?-1:1;

    counter = ecx;
    srcptr1 = esi;
    srcptr2 = edi;

    do {
        srcvalue1 = *((uint8_t *)REG2PTR(srcptr1));
        srcvalue2 = *((uint8_t *)REG2PTR(srcptr2));
        srcptr1 += dir;
        srcptr2 += dir;

        counter--;

        if (srcvalue1 != srcvalue2) break;
    } while (counter != 0);

    ecx = counter;
    esi = srcptr1;
    edi = srcptr2;

    return srcvalue1 | (srcvalue2 << 8);
}

EXTERNC uint32_t CCALL x86_repne_cmpsb(CPU)
{
    int32_t dir;
    uint32_t srcptr1, srcptr2, counter, srcvalue1, srcvalue2;

    dir = (eflags & DF)?-1:1;

    counter = ecx;
    srcptr1 = esi;
    srcptr2 = edi;

    do {
        srcvalue1 = *((uint8_t *)REG2PTR(srcptr1));
        srcvalue2 = *((uint8_t *)REG2PTR(srcptr2));
        srcptr1 += dir;
        srcptr2 += dir;

        counter--;

        if (srcvalue1 == srcvalue2) break;
    } while (counter != 0);

    ecx = counter;
    esi = srcptr1;
    edi = srcptr2;

    return srcvalue1 | (srcvalue2 << 8);
}


/* Port change: repe/repne cmpsd. The flags are those of "cmp [esi], [edi]"
 * for the last compared dwords; they are written to eflags here. */
static void cmpsd_flags(CPU, uint32_t a, uint32_t b)
{
    uint32_t r = a - b, f = 0, p;
    if (a < b) f |= CF;
    if (r == 0) f |= ZF;
    if (r & 0x80000000) f |= SF;
    if ((a ^ b) & (a ^ r) & 0x80000000) f |= OF;
    if ((a ^ b ^ r) & 0x10) f |= AF;
    p = r & 0xff; p ^= p >> 4; p ^= p >> 2; p ^= p >> 1;
    if (!(p & 1)) f |= PF;
    eflags = (eflags & ~(CF | PF | AF | ZF | SF | OF)) | f;
}

static void rep_cmpsd(CPU, int repne)
{
    int32_t dir = (eflags & DF) ? -4 : 4;
    uint32_t counter = ecx, s1 = esi, s2 = edi, v1, v2;

    do {
        v1 = *((uint32_t *)REG2PTR(s1));
        v2 = *((uint32_t *)REG2PTR(s2));
        s1 += dir;
        s2 += dir;
        counter--;
        if (repne ? (v1 == v2) : (v1 != v2)) break;
    } while (counter != 0);

    ecx = counter;
    esi = s1;
    edi = s2;
    cmpsd_flags(cpu, v1, v2);
}

EXTERNC void CCALL x86_repe_cmpsd(CPU) { rep_cmpsd(cpu, 0); }
EXTERNC void CCALL x86_repne_cmpsd(CPU) { rep_cmpsd(cpu, 1); }

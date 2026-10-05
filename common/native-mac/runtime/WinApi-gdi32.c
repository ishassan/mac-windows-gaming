/**
 *
 *  Copyright (C) 2019-2026 Roman Pauer
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

#if defined(DEBUG_GDI32)
#include <inttypes.h>
#endif
#include "WinApi-gdi32.h"
#include "WinApi.h"
#include <stdio.h>
#include <stdlib.h>
#include "guest.h"


#define eprintf(...) fprintf(stderr,__VA_ARGS__)

/* Port change: the game draws its own graphics on DirectDraw surfaces.
 * These GDI calls get harmless results: a fake handle for each new object
 * and success for each drawing call. Nothing is drawn. Fonts, device
 * contexts and text are in WinApi-gdi-text.c. */

static uint32_t next_gdi_handle = 0xe000;

/* Handles are small guest values. A "funcp" return converts a host pointer
 * to a guest value, so the value goes through guest_value(). */
static void *new_gdi_handle(void)
{
    next_gdi_handle += 4;
    if (next_gdi_handle >= 0xf000) next_gdi_handle = 0xe004;
    return guest_value(next_gdi_handle);
}


uint32_t CCALL BitBlt_c(void *hdcDest, int32_t nXDest, int32_t nYDest, int32_t nWidth, int32_t nHeight, void *hdcSrc, int32_t nXSrc, int32_t nYSrc, uint32_t dwRop)
{
    LOG_ONCE("BitBlt: GDI drawing is not done\n");
    return 1;
}

void * CCALL CreateCompatibleDC_c(void *hdc)
{
    return new_gdi_handle();
}


void * CCALL CreatePolygonRgn_c(void *lppt, int32_t cPoints, int32_t fnPolyFillMode)
{
    return new_gdi_handle();
}

void * CCALL CreateSolidBrush_c(uint32_t crColor)
{
    return new_gdi_handle();
}

uint32_t CCALL DeleteDC_c(void *hdc)
{
    return 1;
}


uint32_t CCALL FillRgn_c(void *hdc, void *hrgn, void *hbr)
{
    LOG_ONCE("FillRgn: GDI drawing is not done\n");
    return 1;
}

void * CCALL GetStockObject_c(int32_t fnObject)
{
    return guest_value(0xf100 + 4 * (fnObject & 0x1f));
}

int32_t CCALL OffsetRgn_c(void *hrgn, int32_t nXOffset, int32_t nYOffset)
{
    return 2;   /* SIMPLEREGION */
}





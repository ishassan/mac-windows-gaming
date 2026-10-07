/*
 *  Port change: C++ exceptions and SEH frames of MSVC 6 programs that use
 *  MSVCRT.dll (_CxxThrowException, __CxxFrameHandler, _except_handler3).
 *
 *  With a static C runtime (Commandos, Revenant) these functions are part
 *  of the exe and are translated with it. Programs that import them from
 *  MSVCRT.dll need them here. The tables that they read (FuncInfo, unwind
 *  map, try blocks, catch types) are the MSVC x86 tables in the exe.
 *
 *  How a C++ exception runs:
 *  1. "throw" calls _CxxThrowException, which raises the exception
 *     0xE06D7363 (RaiseException_c in WinApi-kernel32-crt.c).
 *  2. RaiseException_c calls the handler of each SEH frame (fs:[0] chain).
 *     For a C++ function that is __CxxFrameHandler, with eax = FuncInfo.
 *  3. When a try block of the frame has a matching catch: the frames above
 *     it are unwound (RtlUnwind_c: their destructors run), then the frame
 *     itself back to the start state of the try block, the catch object is
 *     built, and the catch block runs (it returns the address to continue
 *     at). Then the exception object is destroyed, and the x86 code
 *     continues at that address with the esp and ebp of the frame
 *     (x86_continue_at in llasm/asm-llasm.c).
 *
 *  The frame of a C++ EH function: EstablisherFrame = the registration
 *  {next, handler, state} at ebp - 12; the saved esp is at ebp - 16.
 *  Funclets (catch blocks, unwind actions) run with ebp = frame + 12.
 *
 *  MIT license, see README.md.
 */

#include "game-info.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Game-Memory.h"
#include "guest.h"

#ifndef EXTERN_C
#define EXTERN_C extern "C"
#endif

EXTERN_C uint32_t CallX86Function(uint32_t address, int nargs, const uint32_t *args);
EXTERN_C uint32_t CallX86FunctionRegs(uint32_t address, int nargs, const uint32_t *args, uint32_t ecx_value, uint32_t ebp_value);
EXTERN_C void x86_continue_at(uint32_t new_esp, uint32_t new_ebp, uint32_t eip) __attribute__((noreturn));
EXTERN_C void RaiseException_c(uint32_t dwExceptionCode, uint32_t dwExceptionFlags, uint32_t nNumberOfArguments, void *lpArguments);
EXTERN_C void RtlUnwind_c(uint32_t TargetFrame, uint32_t TargetIp, void *ExceptionRecord, uint32_t ReturnValue);
extern thread_local uint32_t current_SEH_frame;

#define CXX_EXCEPTION       0xE06D7363u
#define CXX_MAGIC_VC6       0x19930520u
#define CXX_MAGIC_LAST      0x19930522u

#define EXCEPTION_NONCONTINUABLE 0x01
#define EXCEPTION_UNWINDING      0x02
#define EXCEPTION_EXIT_UNWIND    0x04

#define ExceptionContinueExecution 0
#define ExceptionContinueSearch    1

/* ExceptionRecord: code, flags, record, address, nparams, info[15] */
#define REC_CODE(r)   rd32((uint8_t *)(r) + 0)
#define REC_FLAGS(r)  rd32((uint8_t *)(r) + 4)
#define REC_NPARAMS(r) rd32((uint8_t *)(r) + 16)
#define REC_INFO(r, i) rd32((uint8_t *)(r) + 20 + 4 * (i))

static uint8_t *G(uint32_t v) { return (uint8_t *)from_guest(v); }
static uint32_t R(uint32_t addr, uint32_t off) { return rd32(G(addr + off)); }

static int trace_eh(void)
{
    static int t = -1;
    if (t < 0)
    {
        const char *v = game_getenv("TRACE_EH");
        t = (v != NULL && *v != 0 && *v != '0');
    }
    return t;
}

/* Name of a TypeDescriptor (".?AVINIException@@"), or "" */
static const char *type_name(uint32_t td)
{
    return td ? (const char *)G(td + 8) : "";
}

/* The exception that a catch block of this thread handles now ("throw;") */
static thread_local uint32_t current_object, current_throwinfo;

/* ------------------------------------------------------------- throw */

EXTERN_C void _CxxThrowException_c(uint32_t pExceptionObject, uint32_t pThrowInfo)
{
    uint32_t args[3];

    if (pExceptionObject == 0 && pThrowInfo == 0)
    {
        /* "throw;": throw the exception of the current catch block again */
        pExceptionObject = current_object;
        pThrowInfo = current_throwinfo;
        if (pThrowInfo == 0)
        {
            fprintf(stderr, "Fatal: \"throw;\" outside a catch block\n");
            abort();
        }
    }

    if (trace_eh())
    {
        uint32_t cta = pThrowInfo ? R(pThrowInfo, 12) : 0;
        uint32_t ct0 = (cta && R(cta, 0) > 0) ? R(cta, 4) : 0;
        fprintf(stderr, "eh: throw %s (object 0x%x) from %s\n",
                ct0 ? type_name(R(ct0, 4)) : "?", pExceptionObject, guest_caller());
    }

    args[0] = CXX_MAGIC_VC6;
    args[1] = pExceptionObject;
    args[2] = pThrowInfo;
    RaiseException_c(CXX_EXCEPTION, EXCEPTION_NONCONTINUABLE, 3, args);

    /* RaiseException_c does not return for an exception that no frame takes */
    fprintf(stderr, "Fatal: C++ exception was not caught\n");
    abort();
}

/* ------------------------------------------------- __CxxFrameHandler */

/* FuncInfo: magic, maxState, pUnwindMap, nTryBlocks, pTryBlockMap, ... */
#define FI_MAXSTATE(fi)  ((int32_t)R(fi, 4))
#define FI_UNWINDMAP(fi) R(fi, 8)
#define FI_NTRY(fi)      R(fi, 12)
#define FI_TRYMAP(fi)    R(fi, 16)

static int32_t frame_state(uint32_t frame) { return (int32_t)R(frame, 8); }
static void set_frame_state(uint32_t frame, int32_t s) { wr32(G(frame + 8), (uint32_t)s); }

/* Run the unwind actions (destructors) of the frame from its current state
   down to target_state. */
static void unwind_to_state(uint32_t frame, uint32_t funcinfo, int32_t target_state)
{
    int32_t state = frame_state(frame);
    int32_t max_state = FI_MAXSTATE(funcinfo);
    uint32_t map = FI_UNWINDMAP(funcinfo);

    while (state != target_state)
    {
        if (state < 0 || state >= max_state)
        {
            fprintf(stderr, "Fatal: bad EH state %d (frame 0x%x, FuncInfo 0x%x)\n", state, frame, funcinfo);
            abort();
        }
        int32_t next = (int32_t)R(map, 8 * state);
        uint32_t action = R(map, 8 * state + 4);
        /* the state is set before the action, so that an exception inside a
           destructor does not run the action again */
        set_frame_state(frame, next);
        if (action != 0)
        {
            CallX86FunctionRegs(action, 0, NULL, 0, frame + 12);
        }
        state = next;
    }
    set_frame_state(frame, state);
}

/* this pointer of the object as the catch type (PMD: mdisp, pdisp, vdisp) */
static uint32_t adjust_pointer(uint32_t obj, uint32_t pmd)
{
    int32_t mdisp = (int32_t)R(pmd, 0);
    int32_t pdisp = (int32_t)R(pmd, 4);
    int32_t vdisp = (int32_t)R(pmd, 8);
    uint32_t p = obj + mdisp;
    if (pdisp >= 0)
    {
        uint32_t vbtable = R(obj, pdisp);
        p += (int32_t)R(vbtable, vdisp) + pdisp;
    }
    return p;
}

/* HandlerType: adjectives, pType, dispCatchObj, addressOfHandler.
   CatchableType: properties, pType, thisDisplacement (12 bytes),
   sizeOrOffset, copyFunction. ThrowInfo: attributes, pmfnUnwind,
   pForwardCompat, pCatchableTypeArray. */
static int type_match(uint32_t handler, uint32_t ct, uint32_t throwinfo)
{
    uint32_t adjectives = R(handler, 0);
    uint32_t htype = R(handler, 4);
    uint32_t ctype = R(ct, 4);

    if (htype == 0 || type_name(htype)[0] == 0) return 1;   /* catch (...) */
    if (htype != ctype && strcmp(type_name(htype), type_name(ctype)) != 0) return 0;
    if ((R(ct, 0) & 2) && !(adjectives & 8)) return 0;      /* by reference only */
    if ((R(throwinfo, 0) & 1) && !(adjectives & 1)) return 0; /* const */
    if ((R(throwinfo, 0) & 2) && !(adjectives & 2)) return 0; /* volatile */
    return 1;
}

/* Copy the exception object into the catch parameter */
static void build_catch_object(uint32_t obj, uint32_t frame, uint32_t handler, uint32_t ct)
{
    uint32_t adjectives = R(handler, 0);
    uint32_t htype = R(handler, 4);
    int32_t disp = (int32_t)R(handler, 8);

    if (htype == 0 || type_name(htype)[0] == 0 || disp == 0) return;

    uint32_t dst = frame + 12 + disp;
    uint32_t properties = R(ct, 0);
    uint32_t size = R(ct, 20);
    uint32_t copy = R(ct, 24);

    if (adjectives & 8)
    {
        wr32(G(dst), adjust_pointer(obj, ct + 8));          /* by reference */
    }
    else if (properties & 1)
    {
        memmove(G(dst), G(obj), size);                       /* simple type */
        if (size == 4 && rd32(G(dst)) != 0)
        {
            wr32(G(dst), adjust_pointer(rd32(G(dst)), ct + 8));
        }
    }
    else if (copy == 0)
    {
        memmove(G(dst), G(adjust_pointer(obj, ct + 8)), size);
    }
    else
    {
        uint32_t args[2] = { adjust_pointer(obj, ct + 8), 1 };
        CallX86FunctionRegs(copy, (properties & 4) ? 2 : 1, args, dst, 0);
    }
}

static void __attribute__((noreturn)) catch_it(uint32_t record, uint32_t frame, uint32_t funcinfo,
                                                uint32_t tryblock, uint32_t handler, uint32_t ct,
                                                uint32_t obj, uint32_t throwinfo)
{
    if (ct != 0) build_catch_object(obj, frame, handler, ct);

    /* destructors of the frames above this one, then of this frame */
    RtlUnwind_c(frame, 0, G(record), 0);
    unwind_to_state(frame, funcinfo, (int32_t)R(tryblock, 0));
    set_frame_state(frame, (int32_t)R(tryblock, 4) + 1);

    if (trace_eh())
    {
        fprintf(stderr, "eh: catch %s in frame 0x%x (handler 0x%x)\n",
                ct ? type_name(R(ct, 4)) : "(...)", frame, R(handler, 12));
    }

    uint32_t prev_object = current_object, prev_throwinfo = current_throwinfo;
    current_object = obj;
    current_throwinfo = throwinfo;
    uint32_t cont = CallX86FunctionRegs(R(handler, 12), 0, NULL, 0, frame + 12);
    current_object = prev_object;
    current_throwinfo = prev_throwinfo;

    /* the catch block is done: destroy the exception object */
    if (throwinfo != 0 && R(throwinfo, 4) != 0 && obj != 0)
    {
        CallX86FunctionRegs(R(throwinfo, 4), 0, NULL, obj, 0);
    }

    current_SEH_frame = frame;
    x86_continue_at(R(frame, -4), frame + 12, cont);
}

EXTERN_C uint32_t __CxxFrameHandler_c(uint32_t funcinfo, uint32_t record, uint32_t frame, uint32_t context, uint32_t dispatcher)
{
    uint32_t magic = R(funcinfo, 0);

    if (magic < CXX_MAGIC_VC6 || magic > CXX_MAGIC_LAST)
    {
        fprintf(stderr, "Fatal: bad FuncInfo magic 0x%x at 0x%x\n", magic, funcinfo);
        abort();
    }

    if (REC_FLAGS(G(record)) & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
    {
        if (FI_MAXSTATE(funcinfo) != 0 && frame_state(frame) >= 0)
        {
            unwind_to_state(frame, funcinfo, -1);
        }
        return ExceptionContinueSearch;
    }

    if (FI_NTRY(funcinfo) == 0) return ExceptionContinueSearch;

    int32_t state = frame_state(frame);
    uint32_t code = REC_CODE(G(record));
    int cxx = (code == CXX_EXCEPTION && REC_NPARAMS(G(record)) == 3 &&
               REC_INFO(G(record), 0) >= CXX_MAGIC_VC6 && REC_INFO(G(record), 0) <= CXX_MAGIC_LAST);
    uint32_t obj = cxx ? REC_INFO(G(record), 1) : 0;
    uint32_t throwinfo = cxx ? REC_INFO(G(record), 2) : 0;
    uint32_t trymap = FI_TRYMAP(funcinfo);

    for (uint32_t t = 0; t < FI_NTRY(funcinfo); t++)
    {
        uint32_t tb = trymap + 20 * t;
        if (state < (int32_t)R(tb, 0) || state > (int32_t)R(tb, 4)) continue;

        uint32_t ncatch = R(tb, 12), handlers = R(tb, 16);
        for (uint32_t h = 0; h < ncatch; h++)
        {
            uint32_t handler = handlers + 16 * h;
            if (!cxx)
            {
                /* other exceptions: only catch (...) */
                uint32_t htype = R(handler, 4);
                if (htype == 0 || type_name(htype)[0] == 0)
                {
                    catch_it(record, frame, funcinfo, tb, handler, 0, 0, 0);
                }
                continue;
            }
            uint32_t cta = R(throwinfo, 12);
            uint32_t n = R(cta, 0);
            for (uint32_t c = 0; c < n; c++)
            {
                uint32_t ct = R(cta, 4 + 4 * c);
                if (type_match(handler, ct, throwinfo))
                {
                    catch_it(record, frame, funcinfo, tb, handler, ct, obj, throwinfo);
                }
            }
        }
    }
    return ExceptionContinueSearch;
}

/* ------------------------------------------------- _except_handler3 */

/*
 * SEH frame of MSVC 6 (__try/__except/__finally): the registration
 * {next, handler, scopetable, trylevel} is at ebp - 16; the
 * EXCEPTION_POINTERS pointer is at ebp - 20 and the saved esp at ebp - 24.
 * Scope table entry: {enclosing level, filter (NULL: __finally), handler}.
 * Filters and handlers run with ebp = frame + 16.
 */
static void local_unwind2(uint32_t frame, int32_t stop)
{
    uint32_t scopetable = R(frame, 8);
    for (;;)
    {
        int32_t level = (int32_t)R(frame, 12);
        if (level == -1 || level == stop) break;
        uint32_t entry = scopetable + 12 * level;
        wr32(G(frame + 12), R(entry, 0));
        if (R(entry, 4) == 0)
        {
            CallX86FunctionRegs(R(entry, 8), 0, NULL, 0, frame + 16);   /* __finally */
        }
    }
}

EXTERN_C uint32_t _except_handler3_c(uint32_t record, uint32_t frame, uint32_t context, uint32_t dispatcher)
{
    if (REC_FLAGS(G(record)) & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
    {
        local_unwind2(frame, -1);
        return ExceptionContinueSearch;
    }

    uint8_t *ep = (uint8_t *)x86_malloc(8);
    wr32(ep, record);
    wr32(ep + 4, context);
    wr32(G(frame - 4), to_guest(ep));

    uint32_t scopetable = R(frame, 8);
    int32_t level = (int32_t)R(frame, 12);
    while (level != -1)
    {
        uint32_t entry = scopetable + 12 * level;
        uint32_t filter = R(entry, 4);
        if (filter != 0)
        {
            int32_t r = (int32_t)CallX86FunctionRegs(filter, 0, NULL, 0, frame + 16);
            if (r < 0) return ExceptionContinueExecution;
            if (r > 0)
            {
                if (trace_eh())
                {
                    fprintf(stderr, "eh: __except 0x%x takes exception 0x%x (frame 0x%x)\n",
                            R(entry, 8), REC_CODE(G(record)), frame);
                }
                RtlUnwind_c(frame, 0, G(record), 0);
                local_unwind2(frame, level);
                wr32(G(frame + 12), R(entry, 0));
                current_SEH_frame = frame;
                x86_continue_at(R(frame, -8), frame + 16, R(entry, 8));
            }
        }
        level = (int32_t)R(entry, 0);
    }
    return ExceptionContinueSearch;
}

/* The filter of the C runtime start-up: let the exception go on */
EXTERN_C uint32_t _XcptFilter_c(uint32_t xcptnum, void *pxcptinfoptrs)
{
    return 0;   /* EXCEPTION_CONTINUE_SEARCH */
}

/* ------------------------------------------------- C++ runtime */

EXTERN_C void cxx_terminate_c(void)
{
    fprintf(stderr, "Fatal: terminate() called\n");
    x86_print_stack_trace(64);
    abort();
}

static uint32_t se_translator;

EXTERN_C uint32_t cxx_set_se_translator_c(uint32_t func)
{
    /* no hardware exceptions are raised here, so the translator is never called */
    uint32_t old = se_translator;
    se_translator = func;
    return old;
}

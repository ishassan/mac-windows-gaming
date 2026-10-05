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

#include "../Game-Memory.h"
#include "llasm_cpu.h"
#include <stdlib.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
// C11
static _Thread_local _cpu *thread_cpu = NULL;
#elif defined(__cplusplus) && __cplusplus >= 201103L
// C++11
static thread_local _cpu *thread_cpu = NULL;
#elif defined(__SUNPRO_C) || defined(__SUNPRO_CC) || defined(__IBMC__) || defined(__IBMCPP__) || defined(__ibmxl__) || defined(__GNUC__) || defined(__llvm__) || (defined(__INTEL_COMPILER) && defined(__linux__))
static __thread _cpu *thread_cpu = NULL;
#elif defined(_MSC_VER) || (defined(__INTEL_COMPILER) && defined(_WIN32)) || defined(__BORLANDC__) || defined(__DMC__)
static __declspec(thread) _cpu *thread_cpu = NULL;
#else
#error thread local variables are not supported
#endif

#ifdef __cplusplus
extern "C" {
#endif

uint32_t X86_InterruptFlag;

#ifdef __cplusplus
}
#endif

#ifdef PTROFS_64BIT
extern uint64_t pointer_offset;
#endif

EXTERNC _cpu *x86_initialize_cpu(void)
{
    _cpu *cpu;
    void *stack_bottom;

    cpu = thread_cpu;
    if (cpu != NULL) return cpu;

    stack_bottom = x86_malloc(1024 * 1024 - 8);
    if (stack_bottom == NULL) exit(2);

    cpu = (_cpu *)((1024 * 1022 + (uintptr_t)stack_bottom) & ~(uintptr_t)127);
    cpu->stack_bottom = stack_bottom;
    cpu->stack_top = (void *)(1024 * 1020 + (uintptr_t)stack_bottom);

    cpu->_st_top = 0;
    cpu->_st_sw_cond = 0;
    cpu->_st_cw = 0x037f;

#ifdef PTROFS_64BIT
    cpu->_pointer_offset = pointer_offset;
#else
    cpu->_reserved1 = 0;
#endif

    esp = PTR2REG(cpu->stack_top);
    eflags = 0x3202;
    X86_InterruptFlag = 1;

    thread_cpu = cpu;
    return cpu;
}

EXTERNC void x86_deinitialize_cpu(void)
{
    _cpu *cpu;
    void *stack_bottom;

    cpu = thread_cpu;
    if (cpu == NULL) return;

    if (cpu->stack_bottom != NULL)
    {
        stack_bottom = cpu->stack_bottom;
        cpu->stack_bottom = NULL;
        x86_free(stack_bottom);
    }

    thread_cpu = NULL;
}


/* Port change: the dword at guest [esp + 4 * n] of the current thread.
 * Inside an import function, n = 0 gives the return address of the caller. */
EXTERNC uint32_t x86_stack_dword(unsigned int n)
{
    _cpu *cpu = thread_cpu;
    if (cpu == NULL) return 0;
    return *(uint32_t *)REG2PTR(esp + 4 * n);
}

/* Port change: guest register n of the current thread (for lldb).
 * 0 eax, 1 ecx, 2 edx, 3 ebx, 4 esp, 5 ebp, 6 esi, 7 edi. */
EXTERNC uint32_t x86_reg(unsigned int n)
{
    _cpu *cpu = thread_cpu;
    if (cpu == NULL) return 0;
    switch (n)
    {
        case 0: return eax;
        case 1: return ecx;
        case 2: return edx;
        case 3: return ebx;
        case 4: return esp;
        case 5: return ebp;
        case 6: return esi;
        case 7: return edi;
        default: return 0;
    }
}

/* Port change: the dword at guest address addr (for lldb). */
EXTERNC uint32_t x86_peek(uint32_t addr)
{
    _cpu *cpu = thread_cpu;
    return *(uint32_t *)REG2PTR(addr);
}

/* Port change: host pointer for guest address addr (for lldb). */
EXTERNC void *x86_ptr(uint32_t addr)
{
    _cpu *cpu = thread_cpu;
    return REG2PTR(addr);
}

#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>

/* Port change: the recompiled code at a guest return address. The result
 * is the symbol name if it is exported, else the unslid host address as
 * text: "nm build/<program>" gives the name (for example loc_676894). */
EXTERNC const char *x86_code_name(uint32_t value)
{
    static char buf[32];
    Dl_info info;
    void *p = (void *)(uintptr_t)(pointer_offset + value);
    if (dladdr(p, &info) && info.dli_sname != NULL && (uintptr_t)info.dli_saddr == (uintptr_t)p) return info.dli_sname;
    snprintf(buf, sizeof(buf), "host 0x%llx", (unsigned long long)((uintptr_t)p - _dyld_get_image_vmaddr_slide(0)));
    return buf;
}

/* Port change: print the guest stack words that point to recompiled
 * code (a rough call trace). tools/trace.py turns it into names. */
EXTERNC void x86_print_stack_trace(unsigned int words)
{
    _cpu *cpu = thread_cpu;
    uintptr_t slide = _dyld_get_image_vmaddr_slide(0);
    unsigned int i;
    if (cpu == NULL) return;
    fprintf(stderr, "guest trace:");
    for (i = 0; i < words; i++)
    {
        uint32_t *w = (uint32_t *)REG2PTR(esp + 4 * i);
        if ((void *)w >= cpu->stack_top) break;
        fprintf(stderr, " 0x%llx", (unsigned long long)((uintptr_t)REG2PTR(*w) - slide));
    }
    fprintf(stderr, "\n");
}

#include <signal.h>
#include <sys/ucontext.h>
#include <unistd.h>

/* Port change: on a crash, print the guest registers and a rough guest
 * call trace (see tools/trace.py), then stop. */
static void crash_handler(int sig, siginfo_t *info, void *context)
{
    _cpu *cpu = thread_cpu;
    fprintf(stderr, "crash: signal %d, address %p\n", sig, info->si_addr);
    {
        ucontext_t *uc = (ucontext_t *)context;
        uintptr_t slide = _dyld_get_image_vmaddr_slide(0);
        fprintf(stderr, "host pc 0x%llx lr 0x%llx (unslid)\n",
                (unsigned long long)(uc->uc_mcontext->__ss.__pc - slide),
                (unsigned long long)(uc->uc_mcontext->__ss.__lr - slide));
    }
    if (cpu != NULL)
    {
        fprintf(stderr, "guest regs: eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x ebp=%08x esp=%08x\n",
                eax, ebx, ecx, edx, esi, edi, ebp, esp);
        x86_print_stack_trace(512);
    }
    fflush(stderr);
    _exit(128 + sig);
}

EXTERNC void x86_install_crash_handler(void)
{
    struct sigaction sa;
    static uint8_t alt_stack[65536];
    stack_t ss;

    ss.ss_sp = alt_stack;
    ss.ss_size = sizeof(alt_stack);
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
}

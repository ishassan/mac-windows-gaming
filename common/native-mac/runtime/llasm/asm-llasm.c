/*
 *  Port change: calls from C into the translated x86 code.
 *
 *  Based on asm-llasm.c from M-HT/SR (Septerra Core port), MIT license.
 */

#include "llasm_cpu.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
extern _cpu *x86_initialize_cpu(void);
extern void x86_deinitialize_cpu(void);
extern void CCALL c_EntryPoint_(CPU);
extern void CCALL c_RunWndProc_c2asm(CPU);
extern void CCALL c_CallFunction_c2asm(CPU);
extern void CCALL c_run_thread_c2asm(CPU);
extern void CCALL c_ContinueAt_c2asm(CPU);
#ifdef __cplusplus
}
#endif

static void push32(CPU, uint32_t value)
{
    esp -= 4;
    *((uint32_t *)REG2PTR(esp)) = value;
}

/*
 * Port change: entries from C into the x86 code, for exceptions.
 *
 * The x86 code runs with tail calls, so all x86 code that one C call starts
 * runs at the same host stack depth. When an exception handler (C++ catch or
 * SEH __except) continues the x86 code at a frame that an outer entry
 * started, x86_continue_at goes back to that entry with longjmp (the host
 * frames of the inner entries end), and the entry continues the x86 code
 * there (c_ContinueAt_c2asm). The x86 code then returns to the entry as if
 * the first call had returned.
 */
typedef struct x86_entry {
    jmp_buf jb;
    uint32_t entry_esp;      /* x86 esp when the entry started */
    struct x86_entry *outer;
} x86_entry;

static thread_local x86_entry *current_entry = NULL;

#define ENTRY_BEGIN(cpu) \
    x86_entry entry_; \
    entry_.entry_esp = esp; \
    entry_.outer = current_entry; \
    current_entry = &entry_; \
    if (setjmp(entry_.jb) != 0) \
    { \
        c_ContinueAt_c2asm(cpu); \
    } \
    else

#define ENTRY_END() current_entry = entry_.outer

/* Continue the x86 code at eip with these esp and ebp (never returns). */
EXTERNC void CCALL x86_continue_at(uint32_t new_esp, uint32_t new_ebp, uint32_t eip)
{
    _cpu *cpu = x86_initialize_cpu();
    x86_entry *e = current_entry;

    while (e != NULL && e->entry_esp < new_esp)
    {
        e = e->outer;
    }
    if (e == NULL)
    {
        fprintf(stderr, "Fatal: no C entry for the x86 frame at esp 0x%x (continue at 0x%x)\n", new_esp, eip);
        abort();
    }
    current_entry = e;
    /* c_ContinueAt_c2asm pops the return address that its C wrapper pushes,
       then pops eip and jumps there */
    esp = new_esp;
    ebp = new_ebp;
    push32(cpu, eip);
    longjmp(e->jb, 1);
}

/* WinMainCRTStartup of the original program: C runtime start-up, then WinMain */
EXTERNC int CCALL EntryPoint_asm(void)
{
    _cpu *cpu;
    int retval;

    cpu = x86_initialize_cpu();
    {
        ENTRY_BEGIN(cpu)
        {
            c_EntryPoint_(cpu);
        }
        ENTRY_END();
    }
    retval = eax;
    x86_deinitialize_cpu();
    return retval;
}

EXTERNC uint32_t CCALL RunWndProc_asm(void *hwnd, uint32_t uMsg, uint32_t wParam, uint32_t lParam, uint32_t (*WndProc)(void *, uint32_t, uint32_t, uint32_t))
{
    _cpu *cpu;
    uint32_t saved_esp;

    cpu = x86_initialize_cpu();
    saved_esp = esp;
    push32(cpu, PTR2REG(WndProc));
    push32(cpu, lParam);
    push32(cpu, wParam);
    push32(cpu, uMsg);
    push32(cpu, PTR2REG(hwnd));
    // stdcall (4 parameters)
    {
        ENTRY_BEGIN(cpu)
        {
            c_RunWndProc_c2asm(cpu);
        }
        ENTRY_END();
    }
    esp = saved_esp;
    return eax;
}

/* Call an x86 function (cdecl or stdcall) with up to 8 dword arguments. */
EXTERNC uint32_t CCALL CallX86Function(uint32_t address, int nargs, const uint32_t *args)
{
    _cpu *cpu;
    uint32_t saved_esp;
    int i;

    cpu = x86_initialize_cpu();
    saved_esp = esp;
    for (i = nargs - 1; i >= 0; i--)
    {
        push32(cpu, args[i]);
    }
    push32(cpu, address);   // CallFunction_c2asm removes it
    {
        ENTRY_BEGIN(cpu)
        {
            c_CallFunction_c2asm(cpu);
        }
        ENTRY_END();
    }
    esp = saved_esp;
    return eax;
}

/* The same with ecx (thiscall) and ebp set: exception funclets run with
   ebp = the frame of their function. ebp is restored after the call. */
EXTERNC uint32_t CCALL CallX86FunctionRegs(uint32_t address, int nargs, const uint32_t *args, uint32_t ecx_value, uint32_t ebp_value)
{
    _cpu *cpu;
    uint32_t saved_esp, saved_ebp;
    int i;

    cpu = x86_initialize_cpu();
    saved_esp = esp;
    saved_ebp = ebp;
    for (i = nargs - 1; i >= 0; i--)
    {
        push32(cpu, args[i]);
    }
    push32(cpu, address);
    ecx = ecx_value;
    ebp = ebp_value;
    {
        ENTRY_BEGIN(cpu)
        {
            c_CallFunction_c2asm(cpu);
        }
        ENTRY_END();
    }
    esp = saved_esp;
    ebp = saved_ebp;
    return eax;
}

EXTERNC void CCALL run_thread_asm(void *arglist, void(*start_address)(void *))
{
    _cpu *cpu;

    cpu = x86_initialize_cpu();
    push32(cpu, PTR2REG(start_address));
    push32(cpu, PTR2REG(arglist));
    {
        ENTRY_BEGIN(cpu)
        {
            c_run_thread_c2asm(cpu);
        }
        ENTRY_END();
    }
    x86_deinitialize_cpu();
}

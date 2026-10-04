/*
 *  Commandos port: calls from C into the translated x86 code.
 *
 *  Based on asm-llasm.c from M-HT/SR (Septerra Core port), MIT license.
 */

#include "llasm_cpu.h"

#ifdef __cplusplus
extern "C" {
#endif
extern _cpu *x86_initialize_cpu(void);
extern void x86_deinitialize_cpu(void);
extern void CCALL c_EntryPoint_(CPU);
extern void CCALL c_RunWndProc_c2asm(CPU);
extern void CCALL c_CallFunction_c2asm(CPU);
extern void CCALL c_run_thread_c2asm(CPU);
#ifdef __cplusplus
}
#endif

static void push32(CPU, uint32_t value)
{
    esp -= 4;
    *((uint32_t *)REG2PTR(esp)) = value;
}

/* WinMainCRTStartup of the original program: C runtime start-up, then WinMain */
EXTERNC int CCALL EntryPoint_asm(void)
{
    _cpu *cpu;
    int retval;

    cpu = x86_initialize_cpu();
    c_EntryPoint_(cpu);
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
    c_RunWndProc_c2asm(cpu);
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
    c_CallFunction_c2asm(cpu);
    esp = saved_esp;
    return eax;
}

EXTERNC void CCALL run_thread_asm(void *arglist, void(*start_address)(void *))
{
    _cpu *cpu;

    cpu = x86_initialize_cpu();
    push32(cpu, PTR2REG(start_address));
    push32(cpu, PTR2REG(arglist));
    c_run_thread_c2asm(cpu);
    x86_deinitialize_cpu();
}

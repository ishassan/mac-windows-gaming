/*
 *  Port change: helpers for the 32-bit guest address space.
 *  Guest (x86) pointers are 32-bit offsets from pointer_offset.
 *  MIT license, see README.md.
 */
#ifndef NATIVE_GUEST_H
#define NATIVE_GUEST_H

#ifndef EXTERN_C
#define EXTERN_C extern "C"
#endif

#include <stdint.h>
#include <stdio.h>

extern uint64_t pointer_offset;

static inline uint32_t to_guest(const void *p)
{
    return (p == NULL) ? 0 : (uint32_t)((uintptr_t)p - pointer_offset);
}

static inline void *from_guest(uint32_t v)
{
    return (v == 0) ? NULL : (void *)(uintptr_t)(pointer_offset + v);
}

/* A guest value returned through a "funcp" glue function */
static inline void *guest_value(uint32_t v)
{
    return (void *)(uintptr_t)(pointer_offset + v);
}

static inline uint32_t rd32(const void *p) { uint32_t v; __builtin_memcpy(&v, p, 4); return v; }
static inline void wr32(void *p, uint32_t v) { __builtin_memcpy(p, &v, 4); }
static inline void wr16(void *p, uint16_t v) { __builtin_memcpy(p, &v, 2); }

#ifdef __cplusplus
extern "C"
#endif
uint32_t x86_stack_dword(unsigned int n);
#ifdef __cplusplus
extern "C"
#endif
const char *x86_code_name(uint32_t value);

#ifdef __cplusplus
extern "C"
#endif
void x86_print_stack_trace(unsigned int words);

/* Name of the x86 code that called the current import function */
static inline const char *guest_caller(void)
{
    const char *name = x86_code_name(x86_stack_dword(0));
    return name ? name : "?";
}

/* Log once per call site */
#define LOG_ONCE(...) do { static int logged_; if (!logged_) { logged_ = 1; fprintf(stderr, __VA_ARGS__); } } while (0)

#endif

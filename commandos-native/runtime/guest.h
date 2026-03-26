/*
 *  Commandos port: helpers for the 32-bit guest address space.
 *  Guest (x86) pointers are 32-bit offsets from pointer_offset.
 *  MIT license, see README.md.
 */
#ifndef COMMANDOS_GUEST_H
#define COMMANDOS_GUEST_H

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

/* Log once per call site */
#define LOG_ONCE(...) do { static int logged_; if (!logged_) { logged_ = 1; fprintf(stderr, __VA_ARGS__); } } while (0)

#endif

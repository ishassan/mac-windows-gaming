#include <stdint.h>
#include <string.h>
extern uint8_t guest_mem[];
static inline void *from_guest(uint32_t a) { return a ? guest_mem + a : 0; }
static inline uint32_t rd32(void *p) { uint32_t v; memcpy(&v, p, 4); return v; }

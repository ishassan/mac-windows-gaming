// ops on stdin: "open <slot> <path>", "close <slot>", "read <slot>", "seek <slot> <pos>"
// A FILE address is 0x100 * (slot + 1); "close" then "open" of the same slot reuses the address.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
uint8_t guest_mem[0x10000];
static FILE *host[16];
extern "C" uint32_t generals_xfer_read_unicode(uint32_t self, void *buffer, uint32_t size);
static FILE *h(void *f) { return host[((uint8_t *)f - guest_mem) / 0x100 - 1]; }
extern "C" uint32_t fread_c(void *p, uint32_t s, uint32_t n, void *f) { return fread(p, s, n, h(f)); }
extern "C" uint32_t fseek_c(void *f, int32_t o, int32_t w) { return fseek(h(f), o, w); }
extern "C" uint32_t ftell_c(void *f) { return ftell(h(f)); }
int main()
{
    char op[16], path[512]; int slot;
    while (scanf("%15s %d", op, &slot) == 2)
    {
        uint32_t self = 0x8000 + slot * 0x40, file = 0x100 * (slot + 1);
        if (!strcmp(op, "open")) { scanf("%511s", path); host[slot] = fopen(path, "rb"); memcpy(guest_mem + self + 0x10, &file, 4); }
        else if (!strcmp(op, "close")) { fclose(host[slot]); host[slot] = 0; }
        else if (!strcmp(op, "seek")) { long p; scanf("%ld", &p); fseek(host[slot], p, SEEK_SET); }
        else if (!strcmp(op, "read"))
        {
            uint8_t len = 0; fread(&len, 1, 1, host[slot]);
            uint16_t buf[1024];
            if (!generals_xfer_read_unicode(self, buf, 2 * len))
                if (len && fread(buf, 2 * len, 1, host[slot]) != 1) { printf("READ ERROR\n"); continue; }
            for (int i = 0; i < len; i++) putchar(buf[i] < 128 ? buf[i] : '?');
            printf("@%ld\n", ftell(host[slot]));
        }
    }
}

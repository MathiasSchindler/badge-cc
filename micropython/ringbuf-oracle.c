#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *buf;
    uint16_t size, iget, iput;
} ringbuf_t;

#define ROUTINES(P) \
    int P##get16(ringbuf_t *); \
    int P##peek16(ringbuf_t *); \
    int P##put16(ringbuf_t *, uint16_t); \
    int P##get_bytes(ringbuf_t *, uint8_t *, size_t); \
    int P##put_bytes(ringbuf_t *, const uint8_t *, size_t)
ROUTINES(ringbuf_);
ROUTINES(gcc_);

void *memcpy(void *dst, const void *src, size_t count) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (count--)
        *d++ = *s++;
    return dst;
}

int main(void) {
    uint8_t owned[19], control[19];
    for (unsigned i = 0; i < 19; i++)
        owned[i] = control[i] = (uint8_t)(i + 1);
    ringbuf_t a = {owned, 19, 0, 0}, b = {control, 19, 0, 0};
    uint32_t random = 1;
    unsigned coverage = 0;
    for (unsigned k = 0; k < 2048; k++) {
        uint8_t x[24], y[24];
        random = random * 1664525u + 1013904223u;
        size_t count = (random >> 8) % sizeof(x);
        for (size_t j = 0; j < sizeof(x); j++)
            x[j] = y[j] = (uint8_t)(random + j * 7);
        unsigned op = (random >> 16) % 5;
        int first, second;
        switch (op) {
        case 0: first = ringbuf_put16(&a, random); second = gcc_put16(&b, random); break;
        case 1: first = ringbuf_get16(&a); second = gcc_get16(&b); break;
        case 2: first = ringbuf_peek16(&a); second = gcc_peek16(&b); break;
        case 3: first = ringbuf_put_bytes(&a, x, count); second = gcc_put_bytes(&b, y, count); break;
        default: first = ringbuf_get_bytes(&a, x, count); second = gcc_get_bytes(&b, y, count); break;
        }
        coverage |= 1u << op;
        if (first < 0) coverage |= 1u << (5 - first);
        if (a.iget < 2 && k > 50) coverage |= 1u << 8;
        if (first != second || a.iget != b.iget || a.iput != b.iput)
            return 1;
        for (unsigned i = 0; i < sizeof(owned); i++)
            if (owned[i] != control[i])
                return 2;
        for (unsigned i = 0; i < sizeof(x); i++)
            if (x[i] != y[i])
                return 3;
    }
    return (coverage & 0x1dfu) != 0x1dfu;
}

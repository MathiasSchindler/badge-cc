#include <stdint.h>

extern float sqrtf(float);

static float from_bits(uint32_t bits) {
    union { uint32_t bits; float value; } repr = { .bits = bits };
    return repr.value;
}

static uint32_t to_bits(float value) {
    union { uint32_t bits; float value; } repr = { .value = value };
    return repr.bits;
}

int main(void) {
    *(volatile uint32_t *)0xe000ed88 |= 0xf00000;
    __asm__ volatile ("dsb\nisb" ::: "memory");
    for (unsigned n = 0; n <= 4096; ++n)
        if (to_bits(sqrtf((float)(n * n))) != to_bits((float)n))
            return 1;
    static const uint32_t cases[][2] = {
        {0x80000000, 0x80000000}, {0x40000000, 0x3fb504f3},
        {0x00000001, 0x1a3504f3}, {0x00800000, 0x20000000},
        {0x7f7fffff, 0x5f7fffff}, {0x7f800000, 0x7f800000},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(*cases); ++i)
        if (to_bits(sqrtf(from_bits(cases[i][0]))) != cases[i][1])
            return 2;
    if ((to_bits(sqrtf(-1.0f)) & 0x7fffffff) <= 0x7f800000)
        return 3;
    return 0;
}

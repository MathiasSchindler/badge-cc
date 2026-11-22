#include <stdint.h>

uint32_t micro_kernel(uint32_t x) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < 17; ++i) {
        x = x * 1664525u + 1013904223u;
        sum += x ^ (x >> 16);
    }
    return sum;
}

#include <stdint.h>

uint32_t micro_kernel(uint32_t seed) {
    static const uint8_t ring[16] = {
        71, 9, 204, 31, 122, 45, 6, 173, 88, 55, 239, 16, 103, 2, 147, 63
    };
    uint8_t dest[16];
    uint32_t read = (seed >> 4u) & 15u, write = (seed >> 8u) & 15u;
    uint32_t count = seed & 15u;
    for (uint32_t i = 0; i < count; ++i)
        dest[(write + i) & 15u] = ring[(read + i) & 15u];
    uint32_t sum = 0;
    for (uint32_t i = 0; i < count; ++i)
        sum = (sum * 31u) + dest[(write + i) & 15u];
    return sum;
}

#include <stdint.h>

uint32_t micro_kernel(uint32_t seed) {
    static const uint8_t weights[16] = {
        71, 9, 204, 31, 122, 45, 6, 173, 88, 55, 239, 16, 103, 2, 147, 63
    };
    uint32_t sum = 0;
    uint32_t index = seed & 15u;
    uint32_t stride = ((seed >> 4) & 7u) * 2u + 1u;
    for (uint32_t i = 1; i <= 16; ++i) {
        sum += i * weights[index];
        index = (index + stride) & 15u;
    }
    return sum;
}

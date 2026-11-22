#include <stdint.h>

uint32_t micro_kernel(uint32_t x) {
    uint32_t score = 0;
    for (uint32_t i = 0; i < 8; ++i, x >>= 4) {
        switch (x & 15u) {
        case 0: score += 11; break;
        case 3: score ^= 19; break;
        case 7: score <<= 1; break;
        case 12: score += i; break;
        default: score -= 3; break;
        }
    }
    return score;
}

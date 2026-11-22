#include <stdint.h>

static uint32_t mix(uint32_t x, uint32_t salt) {
    x ^= salt;
    x ^= x << 13;
    x ^= x >> 17;
    return x ^ (x << 5);
}

uint32_t micro_kernel(uint32_t seed) {
    uint32_t first = mix(seed, 41u);
    uint32_t second = mix(seed >> 3, 73u);
    return (first + second) ^ mix(first, second);
}

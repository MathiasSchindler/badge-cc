#include "suite.h"

void compiler_arithmetic(uint32_t seed, compiler_result_t *out) {
    out->cases = 256u;
    for (uint32_t i = 0u; i < out->cases; ++i) {
        uint32_t x = seed ^ (i * 0x9e3779b9u);
        uint32_t divisor = i + 1u;
        int32_t signed_x = (int32_t)(x & 0x7fffffffu) - 1073741824;
        int32_t signed_divisor = (int32_t)divisor;
        uint64_t wide = ((uint64_t)x << 32u) | (x ^ 0xabcdef01u);
        out->values[0] += x * divisor + (0u - x);
        out->values[1] ^= (x >> (i & 31u)) | (x << ((32u - i) & 31u));
        out->values[2] += x / divisor + x % divisor;
        out->values[3] += (uint32_t)(signed_x / signed_divisor);
        out->values[4] += (uint32_t)(signed_x % signed_divisor);
        out->values[5] ^= (uint32_t)((wide + x) >> 32u) ^ (uint32_t)wide;
    }
}

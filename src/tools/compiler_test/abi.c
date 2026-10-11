#include "suite.h"

void compiler_abi(uint32_t seed, compiler_result_t *out) {
    uint32_t (*volatile callback)(uint32_t, uint32_t) = compiler_mix;
    compiler_tuple_t tuple = {seed, 65530u, 250u};
    uint64_t wide = ((uint64_t)seed << 32u) | 0xfedcba98u;
    out->cases = 64u;
    for (uint32_t i = 0u; i < out->cases; ++i) {
        tuple = compiler_tuple(tuple, i + 1u);
        wide = compiler_wide(i, seed, 2u, 3u, 4u, 5u, wide);
        out->values[0] ^= callback(tuple.first, i);
    }
    out->values[1] = tuple.first;
    out->values[2] = tuple.second;
    out->values[3] = tuple.third;
    out->values[4] = (uint32_t)wide;
    out->values[5] = (uint32_t)(wide >> 32u);
}

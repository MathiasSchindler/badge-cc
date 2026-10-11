#include "suite.h"

compiler_tuple_t compiler_tuple(compiler_tuple_t value, uint32_t delta) {
    value.first += delta;
    value.second = (uint16_t)(value.second ^ (uint16_t)delta);
    value.third = (uint8_t)(value.third + 3u);
    return value;
}

uint64_t compiler_wide(uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                       uint32_t e, uint32_t f, uint64_t wide) {
    return wide + (uint64_t)a * b + c + d + e + f;
}

uint32_t compiler_mix(uint32_t value, uint32_t salt) {
    return (value ^ salt) * 16777619u;
}

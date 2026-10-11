#include "kernels.h"

uint32_t compiler_kernel_arithmetic(uint32_t seed, const uint32_t *source, uint32_t *dest) {
    (void)source;
    (void)dest;
    uint32_t value = seed;
    for (uint32_t i = 0u; i < COMPILER_KERNEL_WORK; ++i) {
        value = (value + i) * 1664525u + 1013904223u;
        value ^= value >> 13u;
    }
    return value;
}

uint32_t compiler_kernel_branch(uint32_t seed, const uint32_t *source, uint32_t *dest) {
    (void)source;
    (void)dest;
    uint32_t a = seed, b = seed ^ 0x9e3779b9u;
    for (uint32_t i = 0u; i < COMPILER_KERNEL_WORK; ++i) {
        if (((a ^ b) & 1u) != 0u) {
            a += b ^ i;
            b -= a;
        } else {
            a ^= b + i;
            b += a >> 3u;
        }
    }
    return a ^ b;
}

uint32_t compiler_kernel_call(uint32_t seed, const uint32_t *source, uint32_t *dest) {
    (void)source;
    (void)dest;
    uint32_t value = seed;
    for (uint32_t i = 0u; i < COMPILER_KERNEL_WORK; ++i) value = compiler_mix(value, i);
    return value;
}

uint32_t compiler_kernel_unsigned_divrem(uint32_t seed, const uint32_t *source, uint32_t *dest) {
    (void)dest;
    uint32_t value = 0u, divisor = (seed & 127u) + 3u;
    for (uint32_t i = 0u; i < COMPILER_KERNEL_WORK; ++i)
        value += (source[i] / divisor) ^ (source[i] % divisor);
    return value;
}

uint32_t compiler_kernel_signed_divrem(uint32_t seed, const uint32_t *source, uint32_t *dest) {
    (void)dest;
    uint32_t value = 0u;
    int32_t divisor = (int32_t)(seed & 127u) + 3;
    if ((seed & 0x80000000u) != 0u) divisor = -divisor;
    for (uint32_t i = 0u; i < COMPILER_KERNEL_WORK; ++i) {
        int32_t dividend = (int32_t)(source[i] & 0x7fffffffu) - 1073741824;
        value += (uint32_t)(dividend / divisor) ^ (uint32_t)(dividend % divisor);
    }
    return value;
}

uint32_t compiler_kernel_memory(uint32_t seed, const uint32_t *source, uint32_t *dest) {
    for (uint32_t i = 0u; i < COMPILER_KERNEL_WORK; ++i) dest[i] = source[i] + seed + i;
    return dest[COMPILER_KERNEL_WORK - 1u];
}

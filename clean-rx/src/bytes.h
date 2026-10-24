#ifndef CLEAN_RX_BYTES_H
#define CLEAN_RX_BYTES_H

#include "types.h"

/* Copy requires nonoverlapping ranges; zero-sized ranges are not accessed. */
static inline void clean_bytes_copy(uint8_t *destination, const uint8_t *source, uint32_t count) {
    for (uint32_t i = 0u; i < count; ++i) destination[i] = source[i];
}

static inline void clean_bytes_zero(uint8_t *destination, uint32_t count) {
    for (uint32_t i = 0u; i < count; ++i) destination[i] = 0u;
}

static inline int clean_bytes_equal(const uint8_t *left, const uint8_t *right, uint32_t count) {
    for (uint32_t i = 0u; i < count; ++i) if (left[i] != right[i]) return 0;
    return 1;
}

/* Little-endian words use four byte accesses, including unaligned ranges. */
static inline uint32_t clean_bytes_load_u32(const uint8_t *source) {
    const volatile uint8_t *bytes = source;
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8u |
           (uint32_t)bytes[2] << 16u | (uint32_t)bytes[3] << 24u;
}

static inline void clean_bytes_store_u32(uint8_t *destination, uint32_t value) {
    volatile uint8_t *bytes = destination;
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
}

#endif

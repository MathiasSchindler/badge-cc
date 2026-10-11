#ifndef UNIVERSE26_BYTES_H
#define UNIVERSE26_BYTES_H

#include <stdint.h>

/* Copy requires nonoverlapping ranges; zero-sized ranges are not accessed. */
#if defined(__arm__) && defined(BADGER_FAST_BYTES)
void badger_bytes_copy(uint8_t *destination, const uint8_t *source, uint32_t count);
void badger_bytes_zero(uint8_t *destination, uint32_t count);
int badger_bytes_equal(const uint8_t *left, const uint8_t *right, uint32_t count);
void badger_pixels_fill(uint16_t *destination, uint16_t color, uint32_t count);
/* Little-endian words use four byte accesses, including unaligned ranges. */
uint32_t badger_bytes_load_u32(const uint8_t *source);
void badger_bytes_store_u32(uint8_t *destination, uint32_t value);
#else
/* The owned frontend supports static functions but not inline declarations. */
#if defined(__GNUC__)
#define BADGER_BYTES_LOCAL static inline
#else
#define BADGER_BYTES_LOCAL static
#endif
BADGER_BYTES_LOCAL void badger_bytes_copy(uint8_t *destination, const uint8_t *source, uint32_t count) {
    for (uint32_t i = 0u; i < count; ++i) destination[i] = source[i];
}
BADGER_BYTES_LOCAL void badger_bytes_zero(uint8_t *destination, uint32_t count) {
    for (uint32_t i = 0u; i < count; ++i) destination[i] = 0u;
}
BADGER_BYTES_LOCAL int badger_bytes_equal(const uint8_t *left, const uint8_t *right, uint32_t count) {
    for (uint32_t i = 0u; i < count; ++i) if (left[i] != right[i]) return 0;
    return 1;
}
BADGER_BYTES_LOCAL void badger_pixels_fill(uint16_t *destination, uint16_t color, uint32_t count) {
    for (uint32_t i = 0u; i < count; ++i) destination[i] = color;
}
BADGER_BYTES_LOCAL uint32_t badger_bytes_load_u32(const uint8_t *source) {
    const volatile uint8_t *bytes = source;
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8u |
           (uint32_t)bytes[2] << 16u | (uint32_t)bytes[3] << 24u;
}
BADGER_BYTES_LOCAL void badger_bytes_store_u32(uint8_t *destination, uint32_t value) {
    volatile uint8_t *bytes = destination;
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
}
#undef BADGER_BYTES_LOCAL
#endif

#endif

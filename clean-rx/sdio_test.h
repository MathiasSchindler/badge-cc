#ifndef CLEAN_RX_SDIO_TEST_H
#define CLEAN_RX_SDIO_TEST_H

#include "../src/shared/stdint.h"

#define CLEAN_SDIO_TEST_PACING_TICKS 500000u
#define CLEAN_SDIO_TEST_ACK_OFFSET 36u
#define CLEAN_SDIO_TEST_WINDOW_OFFSET 40u
#define CLEAN_SDIO_TEST_PATTERN_OFFSET 44u

#ifndef CLEAN_SDIO_SELF_TEST_WINDOW
#define CLEAN_SDIO_SELF_TEST_WINDOW 0u
#endif
#if CLEAN_SDIO_SELF_TEST_WINDOW > 16u
#error "SDIO test window must be 0..16"
#endif

static inline int clean_sdio_test_window_open(uint32_t posted, uint32_t consumed,
                                              uint32_t window) {
    return window == 0u || posted - consumed < window;
}

static inline uint32_t clean_sdio_test_length(uint32_t sequence) {
    return 8u + (sequence * 37u) % 497u;
}

static inline uint32_t clean_sdio_test_pattern_length(uint32_t sequence, uint32_t pattern) {
    return pattern == 1u ? ((sequence & 1u) ? 465u : 502u) : clean_sdio_test_length(sequence);
}

static inline uint8_t clean_sdio_test_byte(uint32_t sequence, uint32_t offset) {
    uint32_t value = sequence * 0x9e3779b9u + offset * 0x01010101u;
    value ^= value >> 16u;
    value *= 0x85ebca6bu;
    return (uint8_t)(value ^ (value >> 13u));
}

#endif

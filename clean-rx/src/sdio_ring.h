#ifndef CLEAN_RX_SDIO_RING_H
#define CLEAN_RX_SDIO_RING_H

#include "../../src/shared/stdint.h"

/* The ring is 8-KiB aligned; STATUS0 reports the current descriptor's low bits. */
static inline int clean_sdio_available(uint32_t producer, uint32_t status) {
    uint32_t offset = status & 0x1fffu;
    if (producer >= 32u || (offset & ~0x1f0u) != 0u) return -1;
    return ((producer + 1u) & 31u) != (offset >> 4u);
}

static inline uint32_t clean_sdio_flags(uint32_t index) {
    return 0xc0000000u | (index == 31u ? 0x10000000u : 0u);
}

/* Low/high halves are saturating posted/dropped packet counters. */
static inline uint32_t clean_sdio_count(uint32_t counts, uint8_t dropped) {
    uint32_t shift = dropped ? 16u : 0u;
    return ((counts >> shift) & 0xffffu) == 0xffffu ? counts : counts + (1u << shift);
}

#endif

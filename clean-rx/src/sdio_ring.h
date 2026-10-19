#ifndef CLEAN_RX_SDIO_RING_H
#define CLEAN_RX_SDIO_RING_H

#include "../../src/shared/stdint.h"

#define CLEAN_SDIO_DMA_CONTROL 0x00060801u
#define CLEAN_SDIO_WINDOW 1u
#define CLEAN_SDIO_ACK_OFFSET 448u
#define CLEAN_SDIO_POSTED_OFFSET 452u
#define CLEAN_SDIO_DRAIN_US 500000u
#define CLEAN_SDIO_DRAIN_POLLS 1000000u

static inline int clean_sdio_drain_state(uint32_t captured, uint32_t published,
        uint32_t capacity, uint32_t started, uint32_t now, uint32_t polls) {
    uint32_t pending = captured-published;
    if (!capacity || capacity > 16u || pending > capacity) return -1;
    if (!pending) return 0;
    if (now-started >= CLEAN_SDIO_DRAIN_US || polls >= CLEAN_SDIO_DRAIN_POLLS) return -2;
    return 1;
}

static inline int clean_sdio_credit_available(uint32_t posted, uint32_t consumed,
                                               uint32_t window) {
    if (window == 0u) return 1;
    uint32_t pending = posted - consumed;
    if (window > 16u || pending > window) return -1;
    return pending < window;
}

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

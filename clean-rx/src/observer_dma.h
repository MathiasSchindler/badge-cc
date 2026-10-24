#ifndef CLEAN_OBSERVER_DMA_H
#define CLEAN_OBSERVER_DMA_H
#include "rx_contract.h"
#include "mac.h"

#define CLEAN_OBSERVER_DMA_ADDR 0x66000u
#define CLEAN_OBSERVER_DMA_MAGIC 0x314f4d44u
#define CLEAN_OBSERVER_DMA_SLOTS 2u
#define CLEAN_OBSERVER_DMA_PREFIX 768u
#define CLEAN_OBSERVER_DMA_HEADER 128u
#define CLEAN_OBSERVER_DMA_RECORD (32u+CLEAN_OBSERVER_DMA_PREFIX)
#define CLEAN_OBSERVER_DMA_BYTES (CLEAN_OBSERVER_DMA_HEADER+CLEAN_OBSERVER_DMA_SLOTS*CLEAN_OBSERVER_DMA_RECORD)

_Static_assert(CLEAN_D11_BUFFER_BYTES >= CLEAN_OBSERVER_DMA_PREFIX &&
    CLEAN_D11_BUFFER_BYTES <= 0x10000u, "DMA window offset must fit its trace field");
_Static_assert(CLEAN_TX_BUFFER_ADDR+CLEAN_TX_STORAGE_BYTES <= CLEAN_OBSERVER_DMA_ADDR &&
    CLEAN_OBSERVER_DMA_ADDR+CLEAN_OBSERVER_DMA_BYTES <= 0x80000u,
    "DMA diagnostic must not overlap packet storage or exceed SRAM");

static inline void clean_observer_dma_init(volatile uint32_t *trace) {
    for (uint32_t i = 0u; i < CLEAN_OBSERVER_DMA_BYTES/4u; ++i) trace[i] = 0u;
    trace[0] = CLEAN_OBSERVER_DMA_MAGIC;
    trace[1] = 5u;
}

/* Caller owns a completed descriptor or has verified DMA shutdown. */
static inline void clean_observer_dma_record(volatile uint32_t *trace,
    const volatile uint8_t *buffer, const uint8_t nonce[14], const uint32_t context[7]) {
    volatile uint8_t *header = (volatile uint8_t *)trace;
    if (!trace[4]) {
        for (uint32_t i = 0u; i < 14u; ++i) header[32u+i] = nonce[i];
        for (uint32_t i = 0u; i < 7u; ++i) trace[12u+i] = context[i];
        for (uint32_t i = 0u; i < 24u; ++i) header[80u+i] = buffer[i];
    }
    ++trace[4];
    uint32_t nonzero = 0u, length = (uint32_t)buffer[0] | ((uint32_t)buffer[1]<<8u);
    for (uint32_t i = 0u; i < CLEAN_D11_BUFFER_BYTES; ++i)
        if (buffer[i]) ++nonzero;
    if (nonzero > trace[5]) trace[5] = nonzero;
    if (length > trace[6]) trace[6] = length;
    uint32_t found = 0u;
    uint32_t position = 0u;
    for (uint32_t i = 0u; i+14u <= CLEAN_D11_BUFFER_BYTES; ++i) {
        uint32_t j = 0u;
        while (j < 14u && buffer[i+j] == nonce[j]) ++j;
        if (j == 14u) { found = 1u; position = i; break; }
    }
    if (!found) { ++trace[7]; return; }
    ++trace[3];
    uint32_t aggregate = position >= 26u && buffer[position-26u] == 0x88u;
    uint32_t ordinal = aggregate ? (buffer[position-4u]>>4u)&1u : 0u;
    if (aggregate) {
        ++trace[19];
        ++trace[26u+ordinal];
        uint32_t size = (nonce[11] == 1u) != (ordinal != 0u) ? 170u : 298u;
        uint32_t begin = position-26u;
        if ((nonce[11] == 1u || nonce[11] == 2u) &&
            begin+size <= CLEAN_D11_BUFFER_BYTES) {
            uint8_t body[298];
            for (uint32_t i = 0u; i < size; ++i) body[i] = buffer[begin+i];
            uint32_t fcs = (uint32_t)body[size-4u] | ((uint32_t)body[size-3u]<<8u) |
                ((uint32_t)body[size-2u]<<16u) | ((uint32_t)body[size-1u]<<24u);
            if (clean_mac_fcs(body,size-4u) == fcs) ++trace[28u+ordinal];
        }
    }
    uint32_t count = trace[2];
    if (count == CLEAN_OBSERVER_DMA_SLOTS) {
        /* Keep an initial example; prefer a second body in the other slot. */
        uint32_t flags = trace[CLEAN_OBSERVER_DMA_HEADER/4u+6u] |
            trace[(CLEAN_OBSERVER_DMA_HEADER+CLEAN_OBSERVER_DMA_RECORD)/4u+6u];
        if (!aggregate || (flags&0x20000000u) || (!ordinal && (flags&0x40000000u))) return;
        count = 1u;
    }
    volatile uint32_t *record = trace+CLEAN_OBSERVER_DMA_HEADER/4u+
        count*(CLEAN_OBSERVER_DMA_RECORD/4u);
    for (uint32_t i = 0u; i < 7u; ++i) record[i] = context[i];
    if (aggregate) record[6] |= 0x40000000u;
    if (ordinal) record[6] |= 0x20000000u;
    uint32_t start = position >= 64u ? position-64u : 0u;
    if (start+CLEAN_OBSERVER_DMA_PREFIX > CLEAN_D11_BUFFER_BYTES)
        start = CLEAN_D11_BUFFER_BYTES-CLEAN_OBSERVER_DMA_PREFIX;
    record[7] = (start<<16u) | length;
    volatile uint8_t *bytes = (volatile uint8_t *)(record+8u);
    for (uint32_t i = 0u; i < CLEAN_OBSERVER_DMA_PREFIX; ++i) bytes[i] = buffer[start+i];
    trace[2] = count+1u;
}
#endif

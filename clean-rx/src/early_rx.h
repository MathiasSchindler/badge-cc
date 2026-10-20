#ifndef CLEAN_RX_EARLY_RX_H
#define CLEAN_RX_EARLY_RX_H

#include "ht20.h"
#include "ht_observer.h"

static inline int clean_observer_early_length(const uint16_t *header) {
    if (!header[0] || header[10] == 1u) return 0;
    if (header[10] != 2u || header[0] > CLEAN_OBSERVER_RECORDS || header[2] < 50u ||
        header[2] > CLEAN_OBSERVER_EARLY_MAX+6u ||
        (header[3]&0xc100u) != 0x100u) return -1;
    return header[2]-6u;
}

static inline int clean_observer_early_decode(const uint16_t *header,
        const uint16_t *prefix, volatile uint8_t *raw) {
    int length = clean_observer_early_length(header);
    if (length <= 0) return length;
    uint8_t sig[6], expected[6];
    for (uint32_t i = 0u; i < 6u; ++i)
        sig[i] = (uint8_t)(header[7u+i/2u] >> (8u*(i&1u)));
    uint32_t psdu = (uint32_t)sig[1] | ((uint32_t)sig[2]<<8u);
    if (psdu <= 4u+(((uint32_t)length+3u)&~3u) ||
        !clean_ht20_ampdu_sig(expected,psdu,0u) || prefix[10] != header[1])
        return -1;
    for (uint32_t i = 0u; i < 6u; ++i)
        if (sig[i] != expected[i] ||
            sig[i] != (uint8_t)(prefix[6u+i/2u] >> (8u*(i&1u)))) return -1;
    if ((raw[8u]&12u) != 8u || !clean_mac_valid(raw+8u,(uint32_t)length))
        return -2;
    raw[0] = raw[1] = 0u;
    for (uint32_t i = 0u; i < 6u; ++i) raw[2u+i] = sig[i];
    return length;
}

#endif

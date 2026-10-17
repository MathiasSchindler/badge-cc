#ifndef CLEAN_RX_MAC_SOAK_H
#define CLEAN_RX_MAC_SOAK_H

#include "mac.h"
#include "phy_rates.h"

#define CLEAN_SOAK_MAGIC 0x314b534du
#define CLEAN_SOAK_DETAIL 520u
#define CLEAN_SOAK_MAX_FRAMES 100000u

typedef struct {
    uint32_t magic, target, tx_acked, tx_attempts, tx_timeouts, deferrals;
    uint32_t received, duplicates, corrupt, out_of_order, elapsed_us, failure;
} clean_soak_status_t;

static inline uint32_t clean_soak_size(uint32_t index) {
    return index % 3u == 0u ? 32u : index % 3u == 1u ? 1500u : CLEAN_MSDU_BYTES;
}

static inline void clean_soak_address(uint8_t address[6], const uint8_t nonce[14],
                                      uint8_t role) {
    address[0] = 2u;
    for (uint32_t i = 0u; i < 4u; ++i) address[i + 1u] = nonce[i];
    address[5] = role;
}

static inline uint8_t clean_soak_pattern(const uint8_t nonce[14], uint32_t index,
                                         uint32_t at) {
    return (uint8_t)(nonce[at % 14u] ^ (index >> (8u * (at & 3u))) ^ (at * 37u));
}

static inline uint32_t clean_soak_frame(uint8_t *frame, const uint8_t nonce[14],
                                        uint8_t role, uint32_t index) {
    uint8_t own[6], peer[6];
    clean_soak_address(own, nonce, role);
    clean_soak_address(peer, nonce, (uint8_t)(3u - role));
    uint32_t payload = clean_soak_size(index), length = payload + 28u;
    if (!clean_ofdm_plcp_rate(frame, length, 0u)) return 0u;
    uint8_t *m = frame + 6u;
    for (uint32_t i = 0u; i < 24u; ++i) m[i] = 0u;
    m[0] = 8u; m[1] = 1u;
    m[2] = (uint8_t)(10u + clean_ofdm_airtime_us(14u, 0u));
    for (uint32_t i = 0u; i < 6u; ++i) {
        m[4u + i] = m[16u + i] = peer[i];
        m[10u + i] = own[i];
    }
    uint16_t sequence = (uint16_t)(index << 4u);
    m[22] = (uint8_t)sequence; m[23] = (uint8_t)(sequence >> 8u);
    for (uint32_t i = 0u; i < payload; ++i)
        m[24u + i] = clean_soak_pattern(nonce, index, i);
    for (uint32_t i = 0u; i < 4u; ++i) {
        m[24u + i] = (uint8_t)(CLEAN_SOAK_MAGIC >> (8u * i));
        m[28u + i] = (uint8_t)(index >> (8u * i));
    }
    uint32_t fcs = clean_mac_fcs(m, length - 4u);
    for (uint32_t i = 0u; i < 4u; ++i) m[length - 4u + i] = (uint8_t)(fcs >> (8u * i));
    return length + 6u;
}

/* Bound-peer data reaching this function has already passed the actual MAC
 * duplicate/reassembly adapter. A repeated application index is a failure. */
static inline int clean_soak_receive(clean_soak_status_t *s, const uint8_t *m,
                                     uint32_t length, const uint8_t nonce[14],
                                     uint8_t role) {
    uint8_t own[6], peer[6];
    clean_soak_address(own, nonce, role);
    clean_soak_address(peer, nonce, (uint8_t)(3u - role));
    if (length < 28u) return 0;
    for (uint32_t i = 0u; i < 6u; ++i)
        if (m[4u + i] != own[i] || m[10u + i] != peer[i]) return 0;
    if (m[0] != 8u) return 0;
    if (length < 60u || !clean_mac_valid(m, length) || (m[1] & ~8u) != 1u)
        goto corrupt;
    uint32_t magic = 0u, index = 0u;
    for (uint32_t i = 0u; i < 4u; ++i) {
        magic |= (uint32_t)m[24u + i] << (8u * i);
        index |= (uint32_t)m[28u + i] << (8u * i);
    }
    if (magic != CLEAN_SOAK_MAGIC || index >= s->target ||
        length != clean_soak_size(index) + 28u ||
        ((uint16_t)m[22] | ((uint16_t)m[23] << 8u)) != (uint16_t)(index << 4u))
        goto corrupt;
    for (uint32_t i = 8u; i < length - 28u; ++i)
        if (m[24u + i] != clean_soak_pattern(nonce, index, i)) goto corrupt;
    if (index != s->received) {
        if (index < s->received) ++s->duplicates;
        else ++s->out_of_order;
        s->failure = 2u;
        return -1;
    }
    ++s->received;
    return 1;
corrupt:
    ++s->corrupt;
    s->failure = 1u;
    return -1;
}

#endif

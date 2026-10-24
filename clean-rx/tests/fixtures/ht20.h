#ifndef CLEAN_RX_FIXTURE_HT20_H
#define CLEAN_RX_FIXTURE_HT20_H

#include "../../src/ht_command.h"

static inline int clean_ht_ampdu_ba_match(const uint8_t *m, uint32_t length,
                                          const uint8_t nonce[14]) {
    if (!m || !nonce || length != 32u || m[0] != 0x94u || m[1] ||
        m[2] || m[3] || m[16] != 4u || m[17] || m[18] || m[19] ||
        !m[20] || (m[20] & ~7u)) return 0;
    for (uint32_t i = 21u; i < 28u; ++i) if (m[i]) return 0;
    for (uint32_t i = 0u; i < 6u; ++i) {
        uint8_t own = i ? nonce[i-1u] : 2u;
        uint8_t peer = (uint8_t)(own ^ (i == 5u ? 0x80u : 0u));
        if (m[4u+i] != peer || m[10u+i] != own) return 0;
    }
    uint32_t fcs = clean_mac_fcs(m, 28u);
    for (uint32_t i = 0u; i < 4u; ++i)
        if (m[28u+i] != (uint8_t)(fcs >> (8u*i))) return 0;
    return 1;
}

static inline int clean_ht_ampdu_probe_sequence(const uint8_t *m, uint32_t length,
                                                const uint8_t nonce[14]) {
    if (length != CLEAN_HT_AMPDU_PROBE_MPDU || (m[22] & 15u)) return -1;
    uint32_t sequence = ((uint32_t)m[22] | ((uint32_t)m[23] << 8u)) >> 4u;
    uint8_t expected[80];
    if (!clean_ht_ampdu_probe_mpdu(expected,nonce,sequence)) return -1;
    for (uint32_t i = 0u; i < sizeof(expected); ++i)
        if (m[i] != expected[i]) return -1;
    return (int)sequence;
}

static inline uint32_t clean_ht_ampdu_delivery_mask(const uint8_t *payloads,
        uint32_t bytes, uint32_t reported, const uint8_t nonce[14]) {
    if (!payloads || bytes < 3u * CLEAN_HT_AMPDU_PROBE_MPDU || (reported & ~7u))
        return 0u;
    uint32_t mask = 0u;
    for (uint32_t sequence = 0u; sequence < 3u; ++sequence)
        if ((reported & (1u << sequence)) &&
            clean_ht_ampdu_probe_sequence(payloads + CLEAN_HT_AMPDU_PROBE_MPDU * sequence,
                CLEAN_HT_AMPDU_PROBE_MPDU, nonce) == (int)sequence)
            mask |= 1u << sequence;
    return mask;
}

static inline uint32_t clean_ht_ampdu_probe_trace_mask(const clean_ampdu_probe_trace_t *trace,
                                                       const uint8_t nonce[14]) {
    if (trace->magic != CLEAN_AMPDU_PROBE_MAGIC || !trace->count ||
        trace->count > CLEAN_HT_AMPDU_PROBE_COUNT ||
        ((trace->phy & 3u) != 2u &&
         !(CLEAN_RX_AMPDU_INTERFACE >= 3 &&
           ((trace->status1 & 0x100u) ||
            (CLEAN_RX_AMPDU_HANDOFF == 2 && trace->outcome == 2u)) &&
           (trace->status2 & 0x200u))) ||
        trace->length != CLEAN_PLCP_BYTES + CLEAN_HT_AMPDU_PROBE_MPDU) return 0u;
    uint32_t mask = 0u;
    for (uint32_t i = 0u; i < trace->count; ++i) {
        const uint8_t *raw = trace->raw + i * CLEAN_HT_AMPDU_PROBE_SLOT_BYTES;
        if (raw[2u] != nonce[13] ||
            !clean_ht_ampdu_probe_sig_valid(raw+2u,CLEAN_HT_AMPDU_PROBE_BYTES)) continue;
        int sequence = clean_ht_ampdu_probe_sequence(raw+8u,CLEAN_HT_AMPDU_PROBE_MPDU,nonce);
        if (sequence >= 0) mask |= 1u << (uint32_t)sequence;
    }
    return mask;
}

static inline int clean_ht_data_frame(uint8_t *packet, const uint8_t nonce[14]) {
    uint32_t length = clean_ht_data_length(nonce);
    if (!clean_ht_data_size_valid(length) || nonce[13] >= 8u) return 0;
    int aggregate = length == CLEAN_AMSDU_MPDU_BYTES;
    for (uint32_t i = 0u; i < CLEAN_HT_DATA_STORAGE; ++i) packet[i] = 0u;
    packet[0] = aggregate ? CLEAN_HT_AMSDU_VERSION : CLEAN_HT_DATA_VERSION;
    packet[2] = nonce[13];
    packet[4] = (uint8_t)length; packet[5] = (uint8_t)(length >> 8u);
    if (!clean_ht20_sig(packet + CLEAN_HT_DATA_HEADER, length, nonce[13])) return 0;
    uint8_t *m = packet + CLEAN_HT_DATA_HEADER + 6u;
    for (uint32_t i = 0u; i < length - 4u; ++i)
        m[i] = aggregate ? clean_ht_amsdu_byte(i, nonce) : clean_ht_data_byte(i, nonce);
    uint32_t fcs = clean_mac_fcs(m, length - 4u);
    for (uint32_t i = 0u; i < 4u; ++i) m[length - 4u + i] = (uint8_t)(fcs >> (8u*i));
    return 1;
}

static inline int clean_ht_data_match(const volatile uint8_t *m, uint32_t length,
                                       const uint8_t nonce[14]) {
    if (!clean_ht_data_size_valid(length) || nonce[13] >= 8u ||
        length != clean_ht_data_length(nonce) || !clean_mac_valid(m, length)) return 0;
    if (length == CLEAN_AMSDU_MPDU_BYTES) {
        clean_amsdu_t aggregate;
        clean_amsdu_subframe_t subframe;
        if (!clean_amsdu_open(&aggregate, m + 26u, CLEAN_AMSDU_BYTES) ||
            !clean_amsdu_next(&aggregate, &subframe) || subframe.length != 2304u ||
            !clean_amsdu_next(&aggregate, &subframe) || subframe.length != 1505u ||
            clean_amsdu_next(&aggregate, &subframe)) return 0;
    }
    for (uint32_t i = 0u; i < length - 4u; ++i)
        if (m[i] != (length == CLEAN_AMSDU_MPDU_BYTES ?
                    clean_ht_amsdu_byte(i, nonce) : clean_ht_data_byte(i, nonce))) return 0;
    return 1;
}

static inline int clean_ht_ack_frame(uint8_t *packet, const uint8_t nonce[14]) {
    if (!CLEAN_RX_HT_RESPONSE || clean_ht_data_length(nonce) != CLEAN_HT_ACK_MPDU ||
        !clean_ht_data_frame(packet, nonce)) return 0;
    packet[0] = CLEAN_HT_ACK_VERSION;
    uint8_t *m = packet + CLEAN_HT_DATA_HEADER + 6u;
    for (uint32_t i = 0u; i < CLEAN_HT_ACK_MPDU - 4u; ++i)
        m[i] = clean_ht_ack_byte(i, nonce);
    uint32_t fcs = clean_mac_fcs(m, CLEAN_HT_ACK_MPDU - 4u);
    for (uint32_t i = 0u; i < 4u; ++i)
        m[CLEAN_HT_ACK_MPDU - 4u + i] = (uint8_t)(fcs >> (8u*i));
    return 1;
}

static inline int clean_ht_ack_match(const volatile uint8_t *m, uint32_t length,
                                     const uint8_t nonce[14]) {
    if (!CLEAN_RX_HT_RESPONSE || length != CLEAN_HT_ACK_MPDU ||
        length != clean_ht_data_length(nonce) || nonce[13] >= 8u ||
        !clean_mac_valid(m, length)) return 0;
    for (uint32_t i = 0u; i < length - 4u; ++i)
        if (m[i] != clean_ht_ack_byte(i, nonce)) return 0;
    return 1;
}

static inline int clean_ht_clocked_frame(uint8_t *packet, const uint8_t nonce[14]) {
    if (!clean_ht_ack_frame(packet, nonce)) return 0;
    packet[0] = CLEAN_HT_CLOCKED_VERSION;
    return 1;
}

static inline int clean_ht_ack_evidence(const clean_ack_rx_t *rx, const uint8_t nonce[14]) {
    const uint8_t *m = (const uint8_t *)rx->mpdu;
    const uint8_t *plcp = (const uint8_t *)rx->plcp;
    int rate = clean_ofdm_rate_index(plcp[0]);
    if (rx->magic != CLEAN_ACK_RX_MAGIC || rx->count != 1u ||
        nonce[13] >= 8u || clean_ht_data_length(nonce) != CLEAN_HT_ACK_MPDU ||
        (rx->phy & 3u) != 1u || rate < 0 ||
        rate + 4 != clean_ht_ack_rate(0x150u, nonce[13]) ||
        !clean_ofdm_plcp_valid(plcp, 14u) ||
        m[0] != 0xd4u || m[1] || !clean_mac_valid(m, 14u)) return 0;
    for (uint32_t i = 0u; i < 6u; ++i)
        if (m[4u+i] != clean_ht_ack_byte(10u+i, nonce)) return 0;
    return 1;
}

static inline int clean_ht_data_trace_valid(const clean_ht_rx_t *rx, const uint8_t nonce[14]) {
    uint32_t length = clean_ht_data_length(nonce);
    return length <= sizeof(rx->mpdu) && rx->magic == CLEAN_HT_RX_MAGIC && rx->captures &&
        rx->rxe_length == length + 6u && (rx->phy[0] & 3u) == 2u &&
        rx->sig[0] == nonce[13] && clean_ht20_rx_sig_valid(rx->sig, length) &&
        clean_ht_data_match(rx->mpdu, length, nonce);
}

static inline int clean_ht_probe_rx_valid(const clean_ht_rx_t *rx, const uint8_t nonce[14]) {
    return rx->magic == CLEAN_HT_RX_MAGIC && rx->captures &&
        rx->rxe_length == CLEAN_HT_PROBE_BYTES && (rx->phy[0] & 3u) == 2u &&
        clean_ht20_rx_sig_valid(rx->sig, CLEAN_HT_PROBE_MPDU) && rx->sig[0] == nonce[13] &&
        clean_ht_probe_match(rx->mpdu, CLEAN_HT_PROBE_MPDU, nonce);
}

#endif

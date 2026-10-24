#ifndef CLEAN_RX_FIXTURE_UNICAST_H
#define CLEAN_RX_FIXTURE_UNICAST_H

#include "../../src/unicast.h"

static inline int clean_ack_rx_valid(const clean_ack_rx_t *rx, const uint8_t own[6],
        const clean_unicast_status_t *tx, uint16_t basic, uint32_t rate,
        uint32_t short_preamble) {
    if (rx->magic != CLEAN_ACK_RX_MAGIC || tx->magic != CLEAN_UNICAST_MAGIC ||
        tx->result != CLEAN_UNICAST_ACKED || !tx->attempts || !tx->ack_received) return 0;
    uint32_t received = (uint32_t)rx->time_lo | ((uint32_t)rx->time_hi << 16u);
    if (received - tx->started_us > tx->finished_us - tx->started_us) return 0;
    int response = clean_ack_rate(basic, rate);
    if (response < 0 || (rx->phy & 3u) != (response >= 4 ? 1u : 0u) ||
        !!(rx->phy & 0x80u) != (short_preamble && response > 0 && response < 4)) return 0;
    uint32_t signal = response >= 4 ? clean_ofdm_rates[response - 4].signal :
        clean_cck_rates[response].signal;
    if ((rx->plcp[0] & (response >= 4 ? 15u : 255u)) != signal) return 0;
    uint8_t ack[14];
    for (uint32_t i = 0u; i < 7u; ++i) {
        ack[2u*i] = (uint8_t)rx->mpdu[i];
        ack[2u*i+1u] = (uint8_t)(rx->mpdu[i] >> 8u);
    }
    if (ack[0] != 0xd4u || ack[1] || ack[2] || ack[3] || !clean_mac_valid(ack, sizeof(ack)))
        return 0;
    for (uint32_t i = 0u; i < 6u; ++i)
        if (ack[4u+i] != own[i]) return 0;
    return 1;
}

#endif

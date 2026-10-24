#ifndef CLEAN_RX_HT_PAYLOAD_H
#define CLEAN_RX_HT_PAYLOAD_H

#include "firmware_profile.h"
#include "ht20.h"
#include "unicast.h"

#if CLEAN_RX_DIAGNOSTIC
static inline uint8_t clean_ht_amsdu_byte(uint32_t at, const uint8_t nonce[14]);
static inline uint8_t clean_ht_ack_byte(uint32_t at, const uint8_t nonce[14]);
static inline uint32_t clean_ht_data_length(const uint8_t nonce[14]);
#endif

static inline int clean_ht_payload_command_valid(uint32_t cmd, uint32_t enabled,
        uint32_t clocked, const volatile uint8_t *packet) {
    if (!enabled || (clocked && (!CLEAN_RX_DIAGNOSTIC || !CLEAN_RX_HT_RESPONSE))) return 0;
    uint32_t length = clean_tx_length(cmd);
    if (length < 34u || length > CLEAN_HT_DATA_MAX_MPDU + 6u ||
        cmd != (CLEAN_HT_DATA_CMD | clean_tx_length_bits(length) | (1u << 19u))) return 0;
    int aggregate = CLEAN_RX_DIAGNOSTIC && CLEAN_RX_HT_RX && packet[0] == CLEAN_HT_AMSDU_VERSION;
    int ack = CLEAN_RX_DIAGNOSTIC && CLEAN_RX_HT_RESPONSE &&
        packet[0] == (clocked ? CLEAN_HT_CLOCKED_VERSION : CLEAN_HT_ACK_VERSION);
    if (clocked && !ack) return 0;
    if ((!aggregate && !ack && packet[0] != CLEAN_HT_DATA_VERSION) ||
        !clean_ht_data_size_valid(length - 6u) ||
        (ack && length - 6u != CLEAN_HT_ACK_MPDU) ||
        (aggregate ? length - 6u != CLEAN_AMSDU_MPDU_BYTES : length - 6u > CLEAN_MTU_MAX_BYTES) ||
        packet[1] || packet[2] >= 8u ||
        packet[3] || packet[6] || packet[7] ||
        ((uint32_t)packet[4] | ((uint32_t)packet[5] << 8u)) != length - 6u) return 0;
    const volatile uint8_t *sig = packet + CLEAN_HT_DATA_HEADER;
    const volatile uint8_t *m = sig + 6u;
    if (sig[0] != packet[2] || !clean_ht20_sig_valid(sig, length - 6u) ||
        !clean_mac_valid(m, length - 6u)) return 0;
#if CLEAN_RX_DIAGNOSTIC
    if (aggregate || ack) {
        uint8_t nonce[14];
        for (uint32_t i = 0u; i < 14u; ++i) nonce[i] = m[(ack ? 26u : 40u)+i];
        if (clean_ht_data_length(nonce) != length - 6u || nonce[13] != sig[0])
            return 0;
        for (uint32_t i = 0u; i < length - 10u; ++i)
            if (m[i] != (ack ? clean_ht_ack_byte(i, nonce) :
                              clean_ht_amsdu_byte(i, nonce))) return 0;
        return 1;
    }
#endif
    if (m[0] != 8u || m[1] ||
        m[2] || m[3] || (m[10] & 3u) != 2u || (m[22] & 15u)) return 0;
    for (uint32_t i = 0u; i < 6u; ++i)
        if (m[4u+i] != 255u || m[16u+i] != 255u) return 0;
    return 1;
}

#endif

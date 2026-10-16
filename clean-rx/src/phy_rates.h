#ifndef CLEAN_RX_PHY_RATES_H
#define CLEAN_RX_PHY_RATES_H

#include "../../src/shared/stdint.h"

#define CLEAN_OFDM_RATES 8u

typedef struct {
    uint8_t mbps;
    uint8_t signal;
    uint16_t phyctl1;
} clean_ofdm_rate_t;

static const clean_ofdm_rate_t clean_ofdm_rates[CLEAN_OFDM_RATES] = {
    {6u, 0xbu, 0x0002u}, {9u, 0xfu, 0x0202u},
    {12u, 0xau, 0x0802u}, {18u, 0xeu, 0x0a02u},
    {24u, 0x9u, 0x1002u}, {36u, 0xdu, 0x1202u},
    {48u, 0x8u, 0x1902u}, {54u, 0xcu, 0x1a02u}
};

static inline int clean_ofdm_rate_index(uint8_t signal) {
    for (uint32_t i = 0u; i < CLEAN_OFDM_RATES; ++i)
        if (clean_ofdm_rates[i].signal == (signal & 15u)) return (int)i;
    return -1;
}

static inline uint32_t clean_ofdm_airtime_us(uint32_t mpdu_length, uint32_t rate_index) {
    if (rate_index >= CLEAN_OFDM_RATES || mpdu_length < 14u || mpdu_length > 4095u)
        return 0u;
    uint32_t bits_per_symbol = 4u * clean_ofdm_rates[rate_index].mbps;
    uint32_t symbols = (16u + 8u * mpdu_length + 6u + bits_per_symbol - 1u) /
                       bits_per_symbol;
    return 20u + 4u * symbols + 6u;
}

static inline int clean_ofdm_plcp_rate(uint8_t frame[6], uint32_t mpdu_length,
                                       uint32_t rate_index) {
    if (rate_index >= CLEAN_OFDM_RATES || mpdu_length < 14u || mpdu_length > 4095u)
        return 0;
    uint32_t signal = clean_ofdm_rates[rate_index].signal | (mpdu_length << 5u);
    uint32_t parity = 0u;
    for (uint32_t bits = signal; bits != 0u; bits >>= 1u) parity ^= bits & 1u;
    signal |= parity << 17u;
    frame[0] = (uint8_t)signal;
    frame[1] = (uint8_t)(signal >> 8u);
    frame[2] = (uint8_t)(signal >> 16u);
    frame[3] = frame[4] = frame[5] = 0u;
    return 1;
}

static inline int clean_ofdm_plcp_valid(const volatile uint8_t frame[6],
                                        uint32_t mpdu_length) {
    uint32_t signal = frame[0] | ((uint32_t)frame[1] << 8u) |
                      ((uint32_t)frame[2] << 16u);
    if (clean_ofdm_rate_index((uint8_t)signal) < 0 || (signal & 0xfc0010u) != 0u ||
        ((signal >> 5u) & 4095u) != mpdu_length ||
        mpdu_length < 14u || mpdu_length > 4095u ||
        frame[3] != 0u || frame[4] != 0u || frame[5] != 0u) return 0;
    uint32_t parity = 0u;
    for (uint32_t bits = signal; bits != 0u; bits >>= 1u) parity ^= bits & 1u;
    return parity == 0u;
}

#endif

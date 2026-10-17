#ifndef CLEAN_RX_PHY_RATES_H
#define CLEAN_RX_PHY_RATES_H

#include "../../src/shared/stdint.h"
#include "frame_limits.h"

#define CLEAN_CCK_RATES 4u
#define CLEAN_RX_OFDM 0x10u
#define CLEAN_RX_SHORT_PREAMBLE 0x20u
#define CLEAN_RX_UNSUPPORTED_PHY 0x40u

typedef struct {
    uint8_t half_mbps, signal;
    uint16_t phyctl1;
} clean_cck_rate_t;

static const clean_cck_rate_t clean_cck_rates[CLEAN_CCK_RATES] = {
    {2u,10u,0x0002u}, {4u,20u,0x0802u},
    {11u,55u,0x1002u}, {22u,110u,0x1802u}
};

static inline int clean_cck_rate_index(uint8_t signal) {
    for (uint32_t i = 0u; i < CLEAN_CCK_RATES; ++i)
        if (clean_cck_rates[i].signal == signal) return (int)i;
    return -1;
}

static inline int clean_cck_preamble_valid(uint32_t rate, uint32_t short_preamble) {
    return rate < CLEAN_CCK_RATES && short_preamble <= 1u &&
           (!short_preamble || rate != 0u);
}

static inline uint32_t clean_cck_duration_us(uint32_t length, uint32_t rate) {
    if (length < 14u || length > CLEAN_MPDU_BYTES || rate >= CLEAN_CCK_RATES) return 0u;
    uint32_t half_mbps = clean_cck_rates[rate].half_mbps;
    return (length * 16u + half_mbps - 1u) / half_mbps;
}

static inline uint32_t clean_cck_airtime_us(uint32_t length, uint32_t rate,
                                          uint32_t short_preamble) {
    if (!clean_cck_preamble_valid(rate, short_preamble)) return 0u;
    uint32_t duration = clean_cck_duration_us(length, rate);
    return duration ? duration + (short_preamble ? 96u : 192u) : 0u;
}

static inline uint16_t clean_cck_plcp_crc(const volatile uint8_t *plcp) {
    uint16_t crc = 0xffffu;
    for (uint32_t i = 0u; i < 4u; ++i) {
        crc ^= plcp[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit)
            crc = (uint16_t)((crc >> 1u) ^ ((crc & 1u) ? 0x8408u : 0u));
    }
    return (uint16_t)~crc;
}

static inline int clean_cck_plcp_rate(uint8_t plcp[6], uint32_t length, uint32_t rate) {
    uint32_t duration = clean_cck_duration_us(length, rate);
    if (!duration) return 0;
    plcp[0] = clean_cck_rates[rate].signal;
    plcp[1] = (uint8_t)(4u | (rate == 3u && duration * 11u - length * 8u >= 8u ? 128u : 0u));
    plcp[2] = (uint8_t)duration; plcp[3] = (uint8_t)(duration >> 8u);
    uint16_t crc = clean_cck_plcp_crc(plcp);
    plcp[4] = (uint8_t)crc; plcp[5] = (uint8_t)(crc >> 8u);
    return 1;
}

static inline int clean_cck_plcp_valid(const volatile uint8_t plcp[6], uint32_t length) {
    int rate = clean_cck_rate_index(plcp[0]);
    if (rate < 0) return 0;
    uint8_t expected[6];
    if (!clean_cck_plcp_rate(expected, length, (uint32_t)rate)) return 0;
    for (uint32_t i = 0u; i < 6u; ++i)
        if (plcp[i] != expected[i]) return 0;
    return 1;
}

static inline uint8_t clean_rx_phy_metadata(uint16_t phy_status0, uint16_t rx_status,
                                           uint32_t channel) {
    uint32_t format = phy_status0 & 3u;
    uint8_t flags = format > 1u ? CLEAN_RX_UNSUPPORTED_PHY :
        format == 1u || (rx_status & 0x2000u) ? CLEAN_RX_OFDM :
        (phy_status0 & 0x80u) ? CLEAN_RX_SHORT_PREAMBLE : 0u;
    return (uint8_t)((channel & 15u) | flags);
}

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

#ifndef CLEAN_RX_HT20_H
#define CLEAN_RX_HT20_H

#include "ht_contract.h"
#include "phy_rates.h"
#include "mac.h"

typedef struct {
    uint16_t ndbps, phyctl1;
} clean_ht20_rate_t;

static const clean_ht20_rate_t clean_ht20_rates[8] = {
    {26u,0x0002u}, {52u,0x0802u}, {78u,0x0a02u}, {104u,0x1002u},
    {156u,0x1202u}, {208u,0x1902u}, {234u,0x1a02u}, {260u,0x1c02u}
};

typedef struct {
    clean_ht_tx_t controls;
    uint32_t symbols, airtime_us, rate_kbps;
    uint8_t mcs;
} clean_ht20_config_t;

enum {
    CLEAN_AMPDU_DMA_NORMAL = 0,
    CLEAN_AMPDU_DMA_GOOD = 1,
    CLEAN_AMPDU_DMA_BAD = 2,
    CLEAN_AMPDU_DMA_HEADER_ERROR = -1,
    CLEAN_AMPDU_DMA_PREFIX_ERROR = -2,
    CLEAN_AMPDU_DMA_FCS_ERROR = -3
};

static inline uint32_t clean_ht20_symbols(uint32_t length, uint32_t mcs) {
    if (length < 14u || length > CLEAN_MPDU_BYTES || mcs >= 8u) return 0u;
    uint32_t n = clean_ht20_rates[mcs].ndbps;
    return (22u + 8u * length + n - 1u) / n;
}

static inline uint32_t clean_ht20_airtime_us(uint32_t length, uint32_t mcs) {
    uint32_t symbols = clean_ht20_symbols(length, mcs);
    return symbols ? 42u + 4u * symbols : 0u;
}

static inline uint32_t clean_ht20_lsig_length(uint32_t length, uint32_t mcs) {
    uint32_t symbols = clean_ht20_symbols(length, mcs);
    return symbols ? 3u * symbols + 9u : 0u;
}

static inline int clean_ht20_lsig(uint8_t sig[6], uint32_t length, uint32_t mcs) {
    uint32_t protection = clean_ht20_lsig_length(length, mcs);
    if (!protection || protection > 4095u) return 0;
    clean_ofdm_signal_encode(sig, 0xbu | (protection << 5u));
    return 1;
}

static inline int clean_ht20_tx_controls(clean_ht_tx_t *tx, uint32_t length, uint32_t mcs) {
    uint32_t protection = clean_ht20_lsig_length(length, mcs);
    if (!protection || protection > 4095u) return 0;
    tx->phyctl = 0x0042u;
    tx->phyctl1 = clean_ht20_rates[mcs].phyctl1;
    /* MMPLCP accepts separate RATE/LENGTH fields, not serialized L-SIG bits. */
    tx->mmplcp[0] = 0xbu;
    tx->mmplcp[1] = (uint16_t)protection;
    return 1;
}

static inline int clean_ht20_select_mcs(uint32_t mask, uint32_t ceiling) {
    if ((mask & ~CLEAN_HT20_MCS_MASK) || ceiling >= 8u) return -1;
    for (uint32_t n = ceiling + 1u; n; --n)
        if (mask & (1u << (n - 1u))) return (int)(n - 1u);
    return -1;
}

static inline int clean_ht20_configure(clean_ht20_config_t *out, uint32_t length,
                                        uint32_t mcs, uint32_t allowed_mcs) {
    if (mcs >= 8u || (allowed_mcs & ~CLEAN_HT20_MCS_MASK) ||
        !(allowed_mcs & (1u << mcs))) return 0;
    clean_ht20_config_t next = {0};
    if (!clean_ht20_tx_controls(&next.controls, length, mcs)) return 0;
    next.mcs = (uint8_t)mcs;
    next.symbols = clean_ht20_symbols(length, mcs);
    next.airtime_us = clean_ht20_airtime_us(length, mcs);
    next.rate_kbps = 250u * clean_ht20_rates[mcs].ndbps;
    *out = next;
    return 1;
}

/* Unadvertised target IE: activating HT still requires the mandatory MAC. */
static inline void clean_ht20_capability_ie(uint8_t ie[CLEAN_HT20_CAPABILITY_BYTES]) {
    for (uint32_t i = 0u; i < CLEAN_HT20_CAPABILITY_BYTES; ++i) ie[i] = 0u;
    ie[0] = 45u; ie[1] = 26u;
    ie[2] = 12u; /* SM power save disabled; HT20, long GI, no optional PHY modes. */
    ie[4] = 28u; /* 8191-byte A-MPDU target, 16-us minimum MPDU spacing. */
    ie[5] = CLEAN_HT20_MCS_MASK;
    ie[15] = 65u;
    ie[17] = 1u; /* Defined TX MCS set, equal to RX. */
}

static inline uint8_t clean_ht20_crc(const volatile uint8_t sig[6]) {
    uint8_t crc = 255u;
    for (uint32_t i = 0u; i < 34u; ++i) {
        uint32_t feedback = (crc >> 7u) ^ ((sig[i / 8u] >> (i & 7u)) & 1u);
        crc = (uint8_t)((crc << 1u) ^ (feedback ? 7u : 0u));
    }
    return (uint8_t)~crc;
}

static inline int clean_ht20_sig(uint8_t sig[6], uint32_t length, uint32_t mcs) {
    if (!clean_ht20_symbols(length, mcs)) return 0;
    sig[0] = (uint8_t)mcs;
    sig[1] = (uint8_t)length; sig[2] = (uint8_t)(length >> 8u);
    sig[3] = 7u; sig[4] = sig[5] = 0u;
    uint8_t crc = clean_ht20_crc(sig);
    for (uint32_t i = 0u; i < 8u; ++i)
        sig[(34u + i) / 8u] |= (uint8_t)(((crc >> (7u - i)) & 1u) << ((34u + i) & 7u));
    return 1;
}

static inline int clean_ht20_sig_valid(const volatile uint8_t sig[6], uint32_t length) {
    uint8_t expected[6];
    if (!clean_ht20_sig(expected, length, sig[0])) return 0;
    for (uint32_t i = 0u; i < 6u; ++i)
        if (sig[i] != expected[i]) return 0;
    return 1;
}

static inline int clean_ht20_rx_sig_valid(const volatile uint8_t sig[6], uint32_t length) {
    if (!clean_ht20_symbols(length, sig[0]) ||
        (uint32_t)(sig[1] | ((uint32_t)sig[2] << 8u)) != length ||
        (sig[3] & 254u) != 6u || (sig[4] & 3u) || (sig[5] & 252u)) return 0;
    uint8_t crc = clean_ht20_crc(sig);
    for (uint32_t i = 0u; i < 8u; ++i)
        if (((sig[(34u+i)/8u] >> ((34u+i)&7u)) & 1u) != ((crc >> (7u-i)) & 1u))
            return 0;
    return 1;
}

static inline int clean_ht20_ampdu_sig(uint8_t sig[6], uint32_t length, uint32_t mcs) {
    if (!clean_ht20_sig(sig, length, mcs)) return 0;
    sig[3] |= 8u;
    sig[4] = sig[5] = 0u;
    uint8_t crc = clean_ht20_crc(sig);
    for (uint32_t i = 0u; i < 8u; ++i)
        sig[(34u+i)/8u] |= (uint8_t)(((crc >> (7u-i)) & 1u) << ((34u+i)&7u));
    return 1;
}

static inline int clean_ht_ampdu_probe_sig(uint8_t sig[6], uint32_t mcs) {
    return clean_ht20_ampdu_sig(sig,CLEAN_HT_AMPDU_PROBE_BYTES,mcs);
}

static inline int clean_ht_ampdu_probe_sig_valid(const volatile uint8_t sig[6],
                                                 uint32_t length) {
    uint8_t expected[6];
    if (length != CLEAN_HT_AMPDU_PROBE_BYTES ||
        !clean_ht_ampdu_probe_sig(expected,sig[0])) return 0;
    for (uint32_t i = 0u; i < sizeof(expected); ++i)
        if (sig[i] != expected[i]) return 0;
    return 1;
}

static inline int clean_ht_ampdu_dma_decode(const volatile uint8_t *buf, uint32_t bytes,
                                             volatile uint8_t raw[88]) {
    if (!buf || !raw || bytes < 24u) return CLEAN_AMPDU_DMA_HEADER_ERROR;
    uint16_t outcome = (uint16_t)(buf[2] | ((uint16_t)buf[3] << 8u));
    if (!outcome) return CLEAN_AMPDU_DMA_NORMAL;
    uint16_t length = (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8u));
    uint16_t ordinal = (uint16_t)(buf[18] | ((uint16_t)buf[19] << 8u));
    uint16_t magic = (uint16_t)(buf[20] | ((uint16_t)buf[21] << 8u));
    uint32_t payload = ordinal == 1u ? 44u : 38u;
    if (outcome > 2u || magic != 0xda15u || !ordinal || ordinal > 3u ||
        length != (ordinal == 1u ? 86u : 80u) ||
        bytes < payload + CLEAN_HT_AMPDU_PROBE_MPDU ||
        ((buf[4] & 3u) != 1u && (buf[4] & 3u) != 2u) ||
        !clean_ht_ampdu_probe_sig_valid(buf + 12u, CLEAN_HT_AMPDU_PROBE_BYTES))
        return CLEAN_AMPDU_DMA_HEADER_ERROR;
    if (outcome == 2u) return CLEAN_AMPDU_DMA_BAD;
    /* RXE retains the PPDU PLCP in its count; only the first DMA body
     * carries those six bytes. Continuations begin with their MAC header. */
    if (ordinal == 1u)
        for (uint32_t i = 0u; i < CLEAN_PLCP_BYTES; ++i)
            if (buf[38u + i] != buf[12u + i]) return CLEAN_AMPDU_DMA_PREFIX_ERROR;
    if (!clean_mac_valid(buf + payload, CLEAN_HT_AMPDU_PROBE_MPDU))
        return CLEAN_AMPDU_DMA_FCS_ERROR;
    raw[0] = raw[1] = 0u;
    for (uint32_t i = 0u; i < CLEAN_PLCP_BYTES; ++i) raw[2u + i] = buf[12u + i];
    for (uint32_t i = 0u; i < CLEAN_HT_AMPDU_PROBE_MPDU; ++i)
        raw[8u + i] = buf[payload + i];
    return CLEAN_AMPDU_DMA_GOOD;
}

static inline int clean_ht_ampdu_shm_decode(const clean_ampdu_probe_trace_t *trace,
        uint32_t index, uint16_t outcome, volatile uint8_t raw[88]) {
    if (!trace || !raw || trace->magic != CLEAN_AMPDU_PROBE_MAGIC ||
        !trace->count || trace->count > CLEAN_HT_AMPDU_PROBE_COUNT ||
        index >= trace->count || trace->length != 86u ||
        ((trace->phy & 3u) != 1u && (trace->phy & 3u) != 2u) ||
        !outcome || outcome > 2u)
        return CLEAN_AMPDU_DMA_HEADER_ERROR;
    const uint8_t *saved = trace->raw + index * CLEAN_HT_AMPDU_PROBE_SLOT_BYTES;
    if (!clean_ht_ampdu_probe_sig_valid(saved + 2u, CLEAN_HT_AMPDU_PROBE_BYTES))
        return CLEAN_AMPDU_DMA_HEADER_ERROR;
    if (outcome == 2u) return CLEAN_AMPDU_DMA_BAD;
    if (!clean_mac_valid(saved + 8u, CLEAN_HT_AMPDU_PROBE_MPDU))
        return CLEAN_AMPDU_DMA_FCS_ERROR;
    for (uint32_t i = 0u; i < CLEAN_HT_AMPDU_PROBE_SLOT_BYTES; ++i)
        raw[i] = saved[i];
    return CLEAN_AMPDU_DMA_GOOD;
}

static inline int clean_ht_data_size_valid(uint32_t length) {
    return (length >= 28u && length <= CLEAN_MTU_MAX_BYTES) ||
        (CLEAN_RX_HT_RX && length == CLEAN_AMSDU_MPDU_BYTES);
}

static inline int clean_ht_clocked_policy_valid(uint32_t profile, uint16_t basic,
        uint32_t short_slot, uint32_t response) {
    return profile == 3u && basic == 0x150u && short_slot == 1u && response == 3u;
}

static inline uint32_t clean_ht_ack_timeout_us(uint32_t mcs, uint32_t slot) {
    if (mcs >= 8u || (slot != 9u && slot != 20u)) return 0u;
    return 10u + slot + 64u + clean_cck_airtime_us(14u, 0u, 0u);
}

static inline int clean_ht_rx_metadata(uint8_t rate, uint8_t flags, uint8_t version,
                                       uint32_t length, uint32_t channel, uint32_t mcs) {
    return rate == mcs && mcs < 8u && version == CLEAN_HT_DATA_VERSION &&
        channel >= 1u && channel <= 11u && length >= 14u && length <= CLEAN_MPDU_BYTES &&
        flags == (CLEAN_RX_HT20 | CLEAN_RX_UNSUPPORTED_PHY | channel);
}

/* Keep UNSUPPORTED set so legacy metadata consumers cannot mistake MCS for CCK. */
static inline uint8_t clean_ht_rx_version(uint32_t enabled, uint16_t phy,
        const volatile uint8_t sig[6], uint32_t length) {
#if CLEAN_RX_AMPDU_DELIVERY
    if (enabled && ((phy & 3u) == 1u || (phy & 3u) == 2u) &&
        length == CLEAN_HT_AMPDU_PROBE_MPDU &&
        clean_ht_ampdu_probe_sig_valid(sig, CLEAN_HT_AMPDU_PROBE_BYTES))
        return CLEAN_HT_AMPDU_PROBE_VERSION;
#endif
    return enabled && (phy & 3u) == 2u && clean_ht20_rx_sig_valid(sig, length) ?
        CLEAN_HT_DATA_VERSION : 0u;
}

static inline uint32_t clean_ht_probe_template_word(uint32_t word, uint32_t index,
                                                    uint32_t probe, uint32_t clear_crc) {
    return probe && clear_crc && index == 1u ? word & 0xfffffc03u : word;
}

#endif

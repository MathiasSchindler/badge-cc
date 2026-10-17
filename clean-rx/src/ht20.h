#ifndef CLEAN_RX_HT20_H
#define CLEAN_RX_HT20_H

#include "phy_rates.h"
#include "mac.h"
#include "tx_diag.h"
#include "unicast.h"

#ifndef CLEAN_RX_HT_PROBE
#define CLEAN_RX_HT_PROBE 0
#endif
#ifndef CLEAN_RX_HT_CLEAR_SIG_CRC
#define CLEAN_RX_HT_CLEAR_SIG_CRC 0
#endif
#ifndef CLEAN_RX_HT_DATA
#define CLEAN_RX_HT_DATA 0
#endif

_Static_assert(CLEAN_RX_HT_CLEAR_SIG_CRC == 0 ||
               (CLEAN_RX_HT_CLEAR_SIG_CRC == 1 && CLEAN_RX_HT_PROBE),
               "HT CRC experiment requires the explicit probe build");

#define CLEAN_HT_PROBE_CMD 11u
#define CLEAN_HT_PROBE_MPDU 88u
#define CLEAN_HT_PROBE_BYTES (6u + CLEAN_HT_PROBE_MPDU)
#define CLEAN_HT_RX_ADDR 0x20300u
#define CLEAN_HT_RX_MAGIC 0x31525448u
#define CLEAN_HT_DETAIL 520u
#define CLEAN_HT_DIAG_ADDR 0x20380u
#define CLEAN_HT_DIAG_DETAIL 640u
#define CLEAN_HT_DIAG_MAGIC 0x33445448u
#define CLEAN_HT_DATA_CMD 12u
#define CLEAN_HT_DATA_VERSION 1u
#define CLEAN_HT_AMSDU_VERSION 2u
#define CLEAN_HT_ACK_VERSION 3u
#define CLEAN_HT_CLOCKED_VERSION 4u
#define CLEAN_HT_DATA_HEADER 8u
#define CLEAN_HT_DATA_STORAGE (CLEAN_HT_DATA_HEADER + CLEAN_TX_STORAGE_BYTES)
#define CLEAN_HT_DATA_MAX_MPDU (CLEAN_RX_HT_RX ? CLEAN_AMSDU_MPDU_BYTES : CLEAN_MTU_MAX_BYTES)
#define CLEAN_RX_HT20 0x80u

_Static_assert(CLEAN_RX_HT_DATA == 0 || CLEAN_RX_HT_DATA == 1, "HT data build gate");
_Static_assert(CLEAN_TX_BUFFER_ADDR + CLEAN_HT_DATA_STORAGE <= 0x80000u,
               "Versioned HT staging must fit controller SRAM");

typedef struct {
    uint16_t ndbps, phyctl1;
} clean_ht20_rate_t;

static const clean_ht20_rate_t clean_ht20_rates[8] = {
    {26u,0x0002u}, {52u,0x0802u}, {78u,0x0a02u}, {104u,0x1002u},
    {156u,0x1202u}, {208u,0x1902u}, {234u,0x1a02u}, {260u,0x1c02u}
};

typedef struct {
    uint32_t magic, captures;
    uint16_t rxe_length, phy[4];
    uint8_t sig[6], mpdu[96];
} clean_ht_rx_t;

typedef struct {
    uint16_t starts, phy;
    uint8_t sig[6];
    uint16_t initial_count, final_count, status1, status2, outcome;
} clean_ht_early_t;

typedef struct {
    uint16_t phyctl, phyctl1, mmplcp[2];
} clean_ht_tx_t;

typedef struct {
    uint32_t magic;
    clean_ht_early_t early;
    clean_ht_tx_t tx;
    uint32_t template_crc_zeroed;
} clean_ht_diag_t;

_Static_assert(sizeof(clean_ht_early_t) == 2u * CLEAN_HT_EARLY_WORDS,
               "Early HT PSM trace layout");
_Static_assert(sizeof(clean_ht_tx_t) == 8u && sizeof(clean_ht_diag_t) == 36u,
               "HT control diagnostic layout");
_Static_assert(CLEAN_HT_DIAG_ADDR >= CLEAN_HT_RX_ADDR + sizeof(clean_ht_rx_t) &&
               CLEAN_HT_DIAG_ADDR + sizeof(clean_ht_diag_t) <= CLEAN_DCF_TRACE_ADDR &&
               CLEAN_HT_DIAG_DETAIL >= CLEAN_HT_DETAIL + sizeof(clean_ht_rx_t) &&
               CLEAN_HT_DIAG_DETAIL + sizeof(clean_ht_diag_t) <= 1088u,
               "HT diagnostics must not overlap existing regions");
_Static_assert(CLEAN_RX_LAYOUT_ADDR + 4u * CLEAN_RX_LAYOUT_WORDS <= 0x20d80u,
               "RX layout manifest must not overlap frame storage");

_Static_assert(sizeof(clean_ht_rx_t) == 120u, "HT RX diagnostic layout");
_Static_assert(CLEAN_HT_RX_ADDR >= CLEAN_TX_TAIL_ADDR + 24u &&
               CLEAN_HT_RX_ADDR + sizeof(clean_ht_rx_t) <= 0x20d80u,
               "HT RX diagnostic must not overlap existing SRAM regions");
_Static_assert(CLEAN_HT_DETAIL + sizeof(clean_ht_rx_t) <= 1088u,
               "HT diagnostic must fit before saved packet");

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

static inline uint8_t clean_ht_data_byte(uint32_t at, const uint8_t nonce[14]);
static inline uint8_t clean_ht_amsdu_byte(uint32_t at, const uint8_t nonce[14]);
static inline uint8_t clean_ht_ack_byte(uint32_t at, const uint8_t nonce[14]);
static inline uint32_t clean_ht_data_length(const uint8_t nonce[14]);

static inline int clean_ht_data_size_valid(uint32_t length) {
    return (length >= 28u && length <= CLEAN_MTU_MAX_BYTES) ||
        (CLEAN_RX_HT_RX && length == CLEAN_AMSDU_MPDU_BYTES);
}

static inline int clean_ht_payload_command_valid(uint32_t cmd, uint32_t enabled,
        uint32_t clocked, const volatile uint8_t *packet) {
    if (!enabled || (clocked && !CLEAN_RX_HT_RESPONSE)) return 0;
    uint32_t length = clean_tx_length(cmd);
    if (length < 34u || length > CLEAN_HT_DATA_MAX_MPDU + 6u ||
        cmd != (CLEAN_HT_DATA_CMD | clean_tx_length_bits(length) | (1u << 19u))) return 0;
    int aggregate = CLEAN_RX_HT_RX && packet[0] == CLEAN_HT_AMSDU_VERSION;
    int ack = CLEAN_RX_HT_RESPONSE &&
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
    if (m[0] != 8u || m[1] ||
        m[2] || m[3] || (m[10] & 3u) != 2u || (m[22] & 15u)) return 0;
    for (uint32_t i = 0u; i < 6u; ++i)
        if (m[4u+i] != 255u || m[16u+i] != 255u) return 0;
    return 1;
}

static inline int clean_ht_data_command_valid(uint32_t cmd, uint32_t enabled,
        uint32_t station, const volatile uint8_t *packet) {
    return !station && clean_ht_payload_command_valid(cmd, enabled, 0u, packet);
}

static inline int clean_ht_clocked_command_valid(uint32_t cmd, uint32_t enabled,
        uint32_t profile, const volatile uint8_t *packet) {
    return CLEAN_RX_HT_RESPONSE && profile == 3u &&
        clean_ht_payload_command_valid(cmd, enabled, 1u, packet);
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
    return enabled && (phy & 3u) == 2u && clean_ht20_rx_sig_valid(sig, length) ?
        CLEAN_HT_DATA_VERSION : 0u;
}

static inline uint32_t clean_ht_data_length(const uint8_t nonce[14]) {
    return (uint32_t)nonce[11] | ((uint32_t)nonce[12] << 8u);
}

static inline uint8_t clean_ht_data_byte(uint32_t at, const uint8_t nonce[14]) {
    if (at == 0u) return 8u;
    if ((at >= 4u && at < 10u) || (at >= 16u && at < 22u)) return 255u;
    if (at == 10u) return 2u;
    if (at >= 11u && at < 16u) return nonce[at - 11u];
    if (at == 22u) return (uint8_t)(nonce[0] << 4u);
    if (at == 23u) return (uint8_t)((nonce[0] >> 4u) | (nonce[1] << 4u));
    if (at >= 24u) return nonce[(at - 24u) % 14u];
    return 0u;
}

static inline uint8_t clean_ht_amsdu_byte(uint32_t at, const uint8_t nonce[14]) {
    if (at == 0u) return 0x88u;
    if (at >= 4u && at < 22u) {
        uint32_t address = (at - 4u) / 6u, pos = (at - 4u) % 6u;
        if (!pos) return 2u;
        return nonce[pos - 1u] ^ (pos == 5u ? (address == 0u ? 128u :
                                                         address == 2u ? 64u : 0u) : 0u);
    }
    if (at == 22u || at == 23u) return clean_ht_data_byte(at, nonce);
    if (at == 24u) return 0xa0u; /* A-MSDU, No Ack, TID 0. */
    if (at < 26u) return 0u;
    uint32_t offset = at - 26u;
    if (offset == 2318u || offset == 2319u) return 0u;
    uint32_t size = offset < 2320u ? 2304u : 1505u;
    if (offset >= 2320u) offset -= 2320u;
    if (offset < 12u)
        return clean_ht_amsdu_byte(4u + offset, nonce);
    if (offset == 12u) return (uint8_t)(size >> 8u);
    if (offset == 13u) return (uint8_t)size;
    return nonce[(offset - 14u) % 14u];
}

static inline uint8_t clean_ht_ack_byte(uint32_t at, const uint8_t nonce[14]) {
    if (at == 2u) return (uint8_t)(10u + clean_ofdm_airtime_us(14u,
        (uint32_t)clean_ht_ack_rate(0x150u, nonce[13]) - 4u));
    if (at < 24u) return clean_ht_amsdu_byte(at, nonce);
    if (at == 24u) return nonce[6] & 15u;
    if (at == 25u) return 0u;
    return nonce[(at - 26u) % 14u];
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

static inline int clean_ht_probe_frame(uint8_t frame[96], const uint8_t nonce[14]) {
    if (!clean_ht20_sig(frame, CLEAN_HT_PROBE_MPDU, nonce[13])) return 0;
    uint8_t *m = frame + 6u;
    for (uint32_t i = 0u; i < 90u; ++i) m[i] = 0u;
    m[0] = 8u;
    for (uint32_t i = 0u; i < 6u; ++i) m[4u+i] = m[16u+i] = 255u;
    m[10] = 2u;
    for (uint32_t i = 0u; i < 5u; ++i) m[11u+i] = nonce[i];
    m[22] = (uint8_t)(nonce[0] << 4u);
    m[23] = (uint8_t)((nonce[0] >> 4u) | (nonce[1] << 4u));
    m[24] = 'H'; m[25] = 'T'; m[26] = 'D'; m[27] = '1';
    for (uint32_t i = 28u; i < CLEAN_HT_PROBE_MPDU - 4u; ++i)
        m[i] = nonce[(i - 28u) % 14u];
    uint32_t crc = clean_mac_fcs(m, CLEAN_HT_PROBE_MPDU - 4u);
    for (uint32_t i = 0u; i < 4u; ++i)
        m[CLEAN_HT_PROBE_MPDU - 4u + i] = (uint8_t)(crc >> (8u*i));
    return 1;
}

static inline int clean_ht_probe_match(const volatile uint8_t *m, uint32_t length,
                                        const uint8_t nonce[14]) {
    uint8_t expected[96];
    if (length != CLEAN_HT_PROBE_MPDU || !clean_ht_probe_frame(expected, nonce)) return 0;
    for (uint32_t i = 0u; i < length; ++i)
        if (m[i] != expected[6u+i]) return 0;
    return 1;
}

static inline int clean_ht_probe_rx_valid(const clean_ht_rx_t *rx, const uint8_t nonce[14]) {
    return rx->magic == CLEAN_HT_RX_MAGIC && rx->captures &&
        rx->rxe_length == CLEAN_HT_PROBE_BYTES && (rx->phy[0] & 3u) == 2u &&
        clean_ht20_rx_sig_valid(rx->sig, CLEAN_HT_PROBE_MPDU) && rx->sig[0] == nonce[13] &&
        clean_ht_probe_match(rx->mpdu, CLEAN_HT_PROBE_MPDU, nonce);
}

static inline uint32_t clean_ht_probe_template_word(uint32_t word, uint32_t index,
                                                    uint32_t probe, uint32_t clear_crc) {
    return probe && clear_crc && index == 1u ? word & 0xfffffc03u : word;
}

static inline int clean_ht_probe_command_valid(uint32_t cmd, uint32_t enabled,
                                               uint32_t station,
                                               const volatile uint8_t *frame) {
    if (!enabled || station ||
        cmd != (CLEAN_HT_PROBE_CMD | clean_tx_length_bits(CLEAN_HT_PROBE_BYTES) | (1u << 19u)) ||
        !clean_ht20_sig_valid(frame, CLEAN_HT_PROBE_MPDU)) return 0;
    uint8_t nonce[14];
    for (uint32_t i = 0u; i < 14u; ++i) nonce[i] = frame[6u+28u+i];
    return clean_ht_probe_match(frame + 6u, CLEAN_HT_PROBE_MPDU, nonce) &&
           nonce[13] == frame[0];
}

#endif

#ifndef CLEAN_RX_HT20_H
#define CLEAN_RX_HT20_H

#include "phy_rates.h"
#include "mac.h"
#include "tx_diag.h"
#include "unicast.h"
#include "block_ack.h"

#ifndef CLEAN_RX_HT_PROBE
#define CLEAN_RX_HT_PROBE 0
#endif
#ifndef CLEAN_RX_HT_CLEAR_SIG_CRC
#define CLEAN_RX_HT_CLEAR_SIG_CRC 0
#endif
#ifndef CLEAN_RX_HT_DATA
#define CLEAN_RX_HT_DATA 0
#endif
#ifndef CLEAN_RX_HT_STIMULUS
#define CLEAN_RX_HT_STIMULUS 0
#endif
#ifndef CLEAN_RX_HT_HANDOFF_STIMULUS
#define CLEAN_RX_HT_HANDOFF_STIMULUS 0
#endif
_Static_assert(CLEAN_RX_HT_HANDOFF_STIMULUS == 0 || CLEAN_RX_HT_HANDOFF_STIMULUS == 1,
               "Handoff stimulus gate");
_Static_assert(!CLEAN_RX_HT_HANDOFF_STIMULUS || (CLEAN_RX_HT_DATA && CLEAN_RX_HT_RX),
               "Handoff stimulus requires the extended HT payload API");
_Static_assert(CLEAN_RX_HT_STIMULUS == 0 || CLEAN_RX_HT_STIMULUS == 1,
               "Fixed HT stimulus gate");
_Static_assert(!CLEAN_RX_HT_STIMULUS || (CLEAN_RX_HT_DATA && CLEAN_RX_HT_RX),
               "Fixed HT stimulus requires the extended HT payload API");

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
#define CLEAN_HT_AMPDU_PROBE_VERSION 5u
#define CLEAN_HT_AMPDU_PROBE_TX_MODE 30u
#define CLEAN_HT_AMPDU_PROBE_RX_MODE 31u
#define CLEAN_HT_AMPDU_PROBE_MPDU 80u
#define CLEAN_HT_AMPDU_PROBE_SLOT_BYTES (8u + CLEAN_HT_AMPDU_PROBE_MPDU)
#define CLEAN_HT_AMPDU_PROBE_COUNT 3u
#define CLEAN_HT_AMPDU_PROBE_BYTES (CLEAN_HT_AMPDU_PROBE_COUNT * (4u + CLEAN_HT_AMPDU_PROBE_MPDU))
#define CLEAN_HT_AMPDU_PROBE_TX_BYTES (6u + CLEAN_HT_AMPDU_PROBE_BYTES)
#define CLEAN_HT_AMPDU_PROBE_STORAGE ((CLEAN_HT_DATA_HEADER + CLEAN_HT_AMPDU_PROBE_TX_BYTES + 3u) & ~3u)
#define CLEAN_HT_DATA_HEADER 8u
#define CLEAN_HT_DATA_STORAGE (CLEAN_HT_DATA_HEADER + CLEAN_TX_STORAGE_BYTES)
#define CLEAN_HT_DATA_MAX_MPDU (CLEAN_RX_HT_RX ? CLEAN_AMSDU_MPDU_BYTES : CLEAN_MTU_MAX_BYTES)
#define CLEAN_RX_HT20 0x80u
#define CLEAN_HT20_MCS_MASK 0xffu
#define CLEAN_HT20_CAPABILITY_BYTES 28u
#define CLEAN_HT20_WIDTH_MHZ 20u
#define CLEAN_HT20_GI_NS 800u
#define CLEAN_HT20_STREAMS 1u

_Static_assert(CLEAN_RX_HT_DATA == 0 || CLEAN_RX_HT_DATA == 1, "HT data build gate");
_Static_assert(!CLEAN_RX_AMPDU_PROBE || CLEAN_RX_HT_DATA,
               "Aggregate hardware probing requires the finite HT payload API");
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
    clean_ht_tx_t controls;
    uint32_t symbols, airtime_us, rate_kbps;
    uint8_t mcs;
} clean_ht20_config_t;

typedef struct {
    uint32_t magic;
    clean_ht_early_t early;
    clean_ht_tx_t tx;
    uint32_t template_crc_zeroed;
} clean_ht_diag_t;

typedef struct {
    uint32_t magic;
    uint16_t count, outcome, length, status1, status2, phy, time_lo, time_hi;
    uint8_t raw[272];
} clean_ampdu_probe_trace_t;

typedef struct {
    uint32_t magic;
    uint16_t mode, count, prefix_tsf, prefix_count;
    uint16_t record[CLEAN_AMPDU_IF_RECORDS][8];
    uint16_t tail[6];
    uint8_t tail_raw[88];
} clean_ampdu_interface_t;

_Static_assert(sizeof(clean_ampdu_interface_t) == 4u + 2u*CLEAN_AMPDU_IF_WORDS,
               "Aggregate interface trace layout");
_Static_assert(sizeof(clean_ampdu_probe_trace_t) == 4u + 2u * CLEAN_AMPDU_PROBE_WORDS &&
               CLEAN_HT_AMPDU_PROBE_COUNT * CLEAN_HT_AMPDU_PROBE_SLOT_BYTES + 8u <= 272u,
               "Aggregate probe must retain three raw MPDUs and their status words");
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

static inline int clean_ht_ampdu_probe_mpdu(uint8_t m[80], const uint8_t nonce[14],
                                            uint32_t sequence) {
    if (nonce[13] >= 8u || sequence >= CLEAN_HT_AMPDU_PROBE_COUNT) return 0;
    for (uint32_t i = 0u; i < CLEAN_HT_AMPDU_PROBE_MPDU; ++i) m[i] = 0u;
    m[0] = 0x88u; m[1] = 2u;
    m[4] = m[10] = m[16] = 2u;
    for (uint32_t i = 0u; i < 5u; ++i) {
        m[5u+i] = nonce[i];
        m[11u+i] = m[17u+i] = nonce[i];
    }
    /* Keep receiver and transmitter distinct even for repeated challenge bytes. */
    m[15] ^= 0x80u; m[21] ^= 0x80u;
    m[22] = (uint8_t)(sequence << 4u);
    m[24] = 0x60u;
    for (uint32_t i = 0u; i < 14u; ++i) m[26u+i] = nonce[i];
    static const uint8_t marker[8] = {'A','M','P','D','U','-','D','2'};
    for (uint32_t i = 0u; i < sizeof(marker); ++i) m[40u+i] = marker[i];
    for (uint32_t i = 48u; i < 76u; ++i) m[i] = (uint8_t)(nonce[i%14u] ^ i ^ sequence);
    uint32_t fcs = clean_mac_fcs(m,76u);
    for (uint32_t i = 0u; i < 4u; ++i) m[76u+i] = (uint8_t)(fcs >> (8u*i));
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

enum {
    CLEAN_AMPDU_DMA_NORMAL = 0,
    CLEAN_AMPDU_DMA_GOOD = 1,
    CLEAN_AMPDU_DMA_BAD = 2,
    CLEAN_AMPDU_DMA_HEADER_ERROR = -1,
    CLEAN_AMPDU_DMA_PREFIX_ERROR = -2,
    CLEAN_AMPDU_DMA_FCS_ERROR = -3
};

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

static inline int clean_ht_ampdu_probe_frame(uint8_t packet[CLEAN_HT_AMPDU_PROBE_STORAGE],
                                             const uint8_t nonce[14]) {
    if (nonce[13] >= 8u) return 0;
    for (uint32_t i = 0u; i < CLEAN_HT_AMPDU_PROBE_STORAGE; ++i) packet[i] = 0u;
    if (!clean_ht_ampdu_probe_sig(packet+CLEAN_HT_DATA_HEADER,nonce[13])) return 0;
    packet[0] = CLEAN_HT_AMPDU_PROBE_VERSION;
    packet[2] = nonce[13];
    packet[4] = (uint8_t)CLEAN_HT_AMPDU_PROBE_BYTES;
    packet[5] = (uint8_t)(CLEAN_HT_AMPDU_PROBE_BYTES >> 8u);
    for (uint32_t i = 0u; i < CLEAN_HT_AMPDU_PROBE_COUNT; ++i) {
        uint8_t *delimiter = packet + CLEAN_HT_DATA_HEADER + 6u + i*84u;
        delimiter[0] = 0u; delimiter[1] = 5u;
        delimiter[2] = clean_ampdu_delimiter_crc(delimiter);
        delimiter[3] = 0x4eu;
        if (!clean_ht_ampdu_probe_mpdu(delimiter+4u,nonce,i)) return 0;
        if (CLEAN_RX_AMPDU_BAD_FCS & (1u << i)) delimiter[4u+79u] ^= 0x80u;
    }
    return 1;
}

static inline int clean_ht_ampdu_probe_command(uint32_t cmd,
                                               const volatile uint8_t *packet) {
    if (!CLEAN_RX_AMPDU_PROBE ||
        cmd != (CLEAN_HT_DATA_CMD | clean_tx_length_bits(CLEAN_HT_AMPDU_PROBE_TX_BYTES) |
                (1u << 19u))) return 0;
    uint8_t expected[CLEAN_HT_AMPDU_PROBE_STORAGE], nonce[14];
    for (uint32_t i = 0u; i < 14u; ++i) nonce[i] = packet[44u+i];
    if (!clean_ht_ampdu_probe_frame(expected,nonce)) return 0;
    for (uint32_t i = 0u; i < sizeof(expected); ++i)
        if (packet[i] != expected[i]) return 0;
    return 1;
}

#define CLEAN_HT_STIMULUS_VERSION 6u
#define CLEAN_HT_STIMULUS_MPDU 554u
#define CLEAN_HT_STIMULUS_PSDU 1118u
#define CLEAN_HT_STIMULUS_STORAGE 1132u
#define CLEAN_HT_STIMULUS_COUNT 24u
_Static_assert(CLEAN_HT_STIMULUS_PSDU ==
               4u+((CLEAN_HT_STIMULUS_MPDU+3u)&~3u)+4u+CLEAN_HT_STIMULUS_MPDU &&
               CLEAN_HT_STIMULUS_STORAGE == CLEAN_HT_DATA_HEADER+6u+CLEAN_HT_STIMULUS_PSDU,
               "Fixed two-body stimulus length and staging geometry");

static inline int clean_ht_stimulus_sig(uint8_t sig[6], uint32_t mcs) {
    return clean_ht20_ampdu_sig(sig,CLEAN_HT_STIMULUS_PSDU,mcs);
}

static inline int clean_ht_stimulus_sig_valid(const volatile uint8_t *sig, uint32_t length) {
    uint8_t expected[6];
    if (!CLEAN_RX_HT_STIMULUS || length != CLEAN_HT_STIMULUS_PSDU ||
        !clean_ht_stimulus_sig(expected,sig[0])) return 0;
    for (uint32_t i = 0u; i < 6u; ++i)
        if (sig[i] != expected[i]) return 0;
    return 1;
}

/* Kinds cycle through legacy OFDM, single HT and a fixed two-body HT PPDU. */
static inline uint32_t clean_ht_stimulus_frame(uint8_t packet[CLEAN_HT_STIMULUS_STORAGE],
                                               const uint8_t nonce[14], uint32_t index) {
    if (nonce[12] >= 8u || index >= CLEAN_HT_STIMULUS_COUNT) return 0u;
    uint32_t kind = index%3u, aggregate = kind == 2u;
    uint32_t length = aggregate ? CLEAN_HT_STIMULUS_PSDU : CLEAN_HT_STIMULUS_MPDU;
    for (uint32_t i = 0u; i < CLEAN_HT_STIMULUS_STORAGE; ++i) packet[i] = 0u;
    uint8_t *sig = packet+CLEAN_HT_DATA_HEADER;
    if (kind == 0u) {
        if (!clean_ofdm_plcp_rate(sig,length,0u)) return 0u;
    } else if (!(aggregate ? clean_ht_stimulus_sig(sig,nonce[12]) :
                            clean_ht20_sig(sig,length,nonce[12]))) return 0u;
    packet[0] = aggregate ? CLEAN_HT_STIMULUS_VERSION : CLEAN_HT_DATA_VERSION;
    packet[2] = nonce[12];
    packet[4] = (uint8_t)length; packet[5] = (uint8_t)(length>>8u);
    for (uint32_t body = 0u; body < (aggregate ? 2u : 1u); ++body) {
        uint8_t *m = sig+6u;
        if (aggregate) {
            uint8_t *d = m+body*560u;
            d[0] = 0xa0u; d[1] = 0x22u;
            d[2] = clean_ampdu_delimiter_crc(d); d[3] = 0x4eu;
            m = d+4u;
        }
        m[0] = aggregate ? 0x88u : 8u;
        for (uint32_t i = 0u; i < 6u; ++i) m[4u+i] = m[16u+i] = 255u;
        m[10] = 2u;
        for (uint32_t i = 0u; i < 5u; ++i) m[11u+i] = nonce[i];
        uint32_t sequence = 2u*index+body;
        m[22] = (uint8_t)(sequence<<4u); m[23] = (uint8_t)(sequence>>4u);
        if (aggregate) m[24] = 0x60u;
        for (uint32_t i = 0u; i < 14u; ++i) m[26u+i] = nonce[i];
        m[40] = 'D'; m[41] = '2'; m[42] = (uint8_t)kind; m[43] = (uint8_t)body;
        for (uint32_t i = 44u; i < CLEAN_HT_STIMULUS_MPDU-4u; ++i)
            m[i] = (uint8_t)(nonce[i%14u]^i^sequence);
        uint32_t fcs = clean_mac_fcs(m,CLEAN_HT_STIMULUS_MPDU-4u);
        for (uint32_t i = 0u; i < 4u; ++i)
            m[CLEAN_HT_STIMULUS_MPDU-4u+i] = (uint8_t)(fcs>>(8u*i));
    }
    return kind == 0u ? 5u | clean_tx_length_bits(length+6u) | (1u<<18u) | (1u<<19u) :
        CLEAN_HT_DATA_CMD | clean_tx_length_bits(length+6u) | (1u<<19u);
}

static inline int clean_ht_stimulus_command(uint32_t cmd, const volatile uint8_t *packet) {
    if (!CLEAN_RX_HT_STIMULUS || packet[0] != CLEAN_HT_STIMULUS_VERSION) return 0;
    uint8_t expected[CLEAN_HT_STIMULUS_STORAGE], nonce[14];
    const volatile uint8_t *m = packet+CLEAN_HT_DATA_HEADER+10u;
    for (uint32_t i = 0u; i < 14u; ++i) nonce[i] = m[26u+i];
    uint32_t sequence = ((uint32_t)m[22] | ((uint32_t)m[23]<<8u))>>4u;
    uint32_t index = sequence/2u;
    if (index%3u != 2u || cmd != clean_ht_stimulus_frame(expected,nonce,index)) return 0;
    for (uint32_t i = 0u; i < CLEAN_HT_STIMULUS_STORAGE; ++i)
        if (packet[i] != expected[i]) return 0;
    return 1;
}

#define CLEAN_HT_HANDOFF_CASES 5u

static inline uint32_t clean_ht_handoff_length(const uint8_t nonce[14], uint32_t body) {
    return (nonce[11] == 1u || (CLEAN_RX_HT_VARIABLE && nonce[11] >= 4u && !(nonce[11]&1u))) !=
        (body != 0u) ? 170u : 298u;
}

static inline uint32_t clean_ht_handoff_length_at(const uint8_t nonce[14],
                                                  uint32_t index, uint32_t body) {
    if (index >= CLEAN_HT_STIMULUS_COUNT || body > 1u) return 0u;
    if (CLEAN_RX_HT_VARIABLE && nonce[11] >= 4u && index%3u == 2u) {
        static const uint16_t lengths[8][2] = {
            {80u,554u},{127u,381u},{170u,298u},{298u,170u},
            {257u,255u},{479u,554u},{80u,80u},{321u,173u}};
        return lengths[(index/3u+(nonce[11]-4u)/2u)%8u][body ^ (nonce[11]&1u)];
    }
    return clean_ht_handoff_length(nonce,body);
}

static inline uint32_t clean_ht_handoff_psdu(const uint8_t nonce[14], uint32_t index) {
    uint32_t first = clean_ht_handoff_length_at(nonce,index,0u);
    if (!CLEAN_RX_HT_VARIABLE) return index%3u == 2u ? CLEAN_HT_HANDOFF_PSDU : first;
    return index%3u == 2u ? 4u+((first+3u)&~3u)+4u+
        clean_ht_handoff_length_at(nonce,index,1u) : first;
}

/* Only aggregate 2 carries the selected fault; later PPDUs remain good. */
static inline uint32_t clean_ht_handoff_frame(uint8_t packet[CLEAN_HT_STIMULUS_STORAGE],
                                              const uint8_t nonce[14], uint32_t index) {
    if (nonce[10] >= CLEAN_HT_HANDOFF_CASES ||
        (nonce[11] != 1u && nonce[11] != 2u &&
         !(CLEAN_RX_HT_VARIABLE && nonce[11] >= 4u && nonce[11] < 20u)) ||
        !clean_ht_stimulus_frame(packet,nonce,index)) return 0u;
    uint32_t kind = index%3u, first = clean_ht_handoff_length_at(nonce,index,0u);
    uint32_t second = clean_ht_handoff_length_at(nonce,index,1u);
    uint32_t length = CLEAN_RX_HT_HANDOFF_GUARD && kind == 2u ?
        CLEAN_HT_HANDOFF_PSDU : clean_ht_handoff_psdu(nonce,index);
    uint8_t *sig = packet+CLEAN_HT_DATA_HEADER, *m = sig+6u;
    if (CLEAN_RX_HT_VARIABLE && nonce[11] >= 4u) {
        for (uint32_t ordinal = 0u; ordinal < (kind == 2u ? 2u : 1u); ++ordinal) {
            uint32_t size = clean_ht_handoff_length_at(nonce,index,ordinal);
            uint8_t *bytes = m+(kind == 2u ?
                4u+(ordinal ? 4u+((first+3u)&~3u) : 0u) : 0u);
            for (uint32_t i = 0u; i < size; ++i) bytes[i] = 0u;
            bytes[0] = kind == 2u ? 0x88u : 8u;
            for (uint32_t i = 0u; i < 6u; ++i) bytes[4u+i] = bytes[16u+i] = 255u;
            bytes[10] = 2u;
            for (uint32_t i = 0u; i < 5u; ++i) bytes[11u+i] = nonce[i];
            uint32_t sequence = 2u*index+ordinal;
            bytes[22] = (uint8_t)(sequence<<4u); bytes[23] = (uint8_t)(sequence>>4u);
            if (kind == 2u) bytes[24] = 0x60u;
            for (uint32_t i = 0u; i < 14u; ++i) bytes[26u+i] = nonce[i];
            bytes[40] = 'D'; bytes[41] = '2'; bytes[42] = (uint8_t)kind;
            bytes[43] = (uint8_t)ordinal;
            for (uint32_t i = 44u; i < size-4u; ++i)
                bytes[i] = (uint8_t)(nonce[i%14u]^i^sequence);
        }
    }
    if (kind == 2u) {
        uint32_t at = 4u+((first+3u)&~3u);
        if (!(CLEAN_RX_HT_VARIABLE && nonce[11] >= 4u))
            for (uint32_t i = 0u; i < second; ++i) m[at+4u+i] = m[564u+i];
        for (uint32_t body = 0u; body < 2u; ++body) {
            uint8_t *delimiter = m+(body ? at : 0u), *bytes = delimiter+4u;
            uint32_t size = body ? second : first;
            /* Legal More Data variation distinguishes even a stale MAC prefix. */
            bytes[1] = body ? 2u : 0x22u;
            bytes[4] = bytes[16] = 2u;
            for (uint32_t i = 0u; i < 5u; ++i)
                bytes[5u+i] = bytes[17u+i] = nonce[i];
            bytes[9] ^= 0x80u;
            delimiter[0] = (uint8_t)(size<<4u); delimiter[1] = (uint8_t)(size>>4u);
            delimiter[2] = clean_ampdu_delimiter_crc(delimiter); delimiter[3] = 0x4eu;
            uint32_t fcs = clean_mac_fcs(bytes,size-4u);
            for (uint32_t i = 0u; i < 4u; ++i) bytes[size-4u+i] = (uint8_t)(fcs>>(8u*i));
        }
        for (uint32_t i = first+4u; i < at; ++i) m[i] = 0u;
#if CLEAN_RX_HT_HANDOFF_GUARD
        uint32_t guard_at = at+4u+((second+3u)&~3u);
        uint8_t *guard = m+guard_at+4u;
        for (uint32_t i = at+4u+second; i < guard_at; ++i) m[i] = 0u;
        for (uint32_t i = 0u; i < CLEAN_HT_HANDOFF_GUARD_BYTES; ++i) guard[i] = m[at+4u+i];
        guard[22] = (uint8_t)((2u*index+2u)<<4u);
        guard[23] = (uint8_t)((2u*index+2u)>>4u);
        guard[43] = 2u;
        uint32_t guard_fcs = clean_mac_fcs(guard,CLEAN_HT_HANDOFF_GUARD_BYTES-4u);
        for (uint32_t i = 0u; i < 4u; ++i)
            guard[CLEAN_HT_HANDOFF_GUARD_BYTES-4u+i] = (uint8_t)(guard_fcs>>(8u*i));
        m[guard_at] = (uint8_t)(CLEAN_HT_HANDOFF_GUARD_BYTES<<4u);
        m[guard_at+1u] = (uint8_t)(CLEAN_HT_HANDOFF_GUARD_BYTES>>4u);
        m[guard_at+2u] = clean_ampdu_delimiter_crc(m+guard_at);
        m[guard_at+3u] = 0x4eu;
#endif
        if (index == 2u) {
            if (nonce[10] == 1u) m[4u+first-1u] ^= 1u;
            if (nonce[10] == 2u) m[at+4u+second-1u] ^= 1u;
            if (nonce[10] == 3u) m[at+2u] ^= 1u;
            if (nonce[10] == 4u) {
                uint32_t truncated = second+8u;
                m[at] = (uint8_t)(truncated<<4u); m[at+1u] = (uint8_t)(truncated>>4u);
                m[at+2u] = clean_ampdu_delimiter_crc(m+at);
            }
        }
        packet[0] = CLEAN_HT_HANDOFF_VERSION;
        if (!clean_ht20_ampdu_sig(sig,length,nonce[12])) return 0u;
    } else {
        uint32_t fcs = clean_mac_fcs(m,first-4u);
        for (uint32_t i = 0u; i < 4u; ++i) m[first-4u+i] = (uint8_t)(fcs>>(8u*i));
        if (!(kind ? clean_ht20_sig(sig,length,nonce[12]) :
                     clean_ofdm_plcp_rate(sig,length,0u))) return 0u;
    }
    packet[4] = (uint8_t)length; packet[5] = (uint8_t)(length>>8u);
    for (uint32_t i = CLEAN_HT_DATA_HEADER+6u+length; i < CLEAN_HT_STIMULUS_STORAGE; ++i)
        packet[i] = 0u;
    return kind == 0u ? 5u | clean_tx_length_bits(length+6u) | (1u<<18u) | (1u<<19u) :
        CLEAN_HT_DATA_CMD | clean_tx_length_bits(length+6u) | (1u<<19u);
}

static inline int clean_ht_handoff_sig_valid(const volatile uint8_t *sig, uint32_t length) {
    uint8_t expected[6];
    if (!CLEAN_RX_HT_HANDOFF_STIMULUS ||
        (CLEAN_RX_HT_VARIABLE ? length < 104u || length > CLEAN_HT_STIMULUS_PSDU :
         length != CLEAN_HT_HANDOFF_PSDU) ||
        !clean_ht20_ampdu_sig(expected,length,sig[0])) return 0;
    for (uint32_t i = 0u; i < 6u; ++i) if (sig[i] != expected[i]) return 0;
    return 1;
}

static inline int clean_ht_handoff_command(uint32_t cmd, const volatile uint8_t *packet) {
    if (!CLEAN_RX_HT_HANDOFF_STIMULUS || packet[0] != CLEAN_HT_HANDOFF_VERSION) return 0;
    uint8_t expected[CLEAN_HT_STIMULUS_STORAGE], nonce[14];
    const volatile uint8_t *m = packet+CLEAN_HT_DATA_HEADER+10u;
    for (uint32_t i = 0u; i < 14u; ++i) nonce[i] = m[26u+i];
    uint32_t index = (((uint32_t)m[22] | ((uint32_t)m[23]<<8u))>>4u)/2u;
    if (index%3u != 2u || cmd != clean_ht_handoff_frame(expected,nonce,index)) return 0;
    for (uint32_t i = 0u; i < CLEAN_HT_STIMULUS_STORAGE; ++i)
        if (packet[i] != expected[i]) return 0;
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
    return !station && enabled && (clean_ht_stimulus_command(cmd,packet) ||
        clean_ht_handoff_command(cmd,packet) ||
        clean_ht_ampdu_probe_command(cmd,packet) ||
        clean_ht_payload_command_valid(cmd, enabled, 0u, packet));
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
#if CLEAN_RX_AMPDU_DELIVERY
    if (enabled && ((phy & 3u) == 1u || (phy & 3u) == 2u) &&
        length == CLEAN_HT_AMPDU_PROBE_MPDU &&
        clean_ht_ampdu_probe_sig_valid(sig, CLEAN_HT_AMPDU_PROBE_BYTES))
        return CLEAN_HT_AMPDU_PROBE_VERSION;
#endif
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

#ifndef CLEAN_RX_UNICAST_H
#define CLEAN_RX_UNICAST_H

#include "unicast_contract.h"
#include "phy_rates.h"
#include "mac.h"

static inline int clean_ack_test_status_valid(const clean_ack_test_status_t *s) {
    return s->magic == CLEAN_ACK_TEST_MAGIC && s->configured <= CLEAN_TEST_ACK_MAX &&
        s->limit == s->configured && s->remaining <= s->limit &&
        s->dropped <= s->limit && s->remaining + s->dropped == s->limit &&
        !s->fault && !s->phase && !s->reserved;
}

static inline int clean_edca_command_valid(uint32_t command, uint32_t enabled,
                                           uint32_t profile) {
    return enabled && profile == 3u && command == CLEAN_EDCA_CMD;
}

static inline int clean_ht_response_policy(uint32_t command, uint32_t enabled,
                                          uint32_t profile) {
    if (!enabled || profile != 3u || (command & 255u) != CLEAN_HT_RESPONSE_CMD ||
        (command >> 8u) > 3u) return -1;
    return (int)(command >> 8u);
}

static inline int clean_tx_tail_poll(clean_tx_tail_t *s, int active) {
    if (s->ended) return 0;
    if (active) s->active = 1u;
    else if (s->active) {
        s->ended = 1u;
        return 1;
    }
    return 0;
}

static inline uint32_t clean_tx_tail_ack_start(const clean_tx_tail_t *s,
        uint32_t started, uint32_t phy_started) {
    return s->ended ? started : phy_started;
}

static inline uint16_t clean_dcf_cancel_request(uint16_t request) {
    return request == CLEAN_DCF_FIRED ? CLEAN_DCF_FIRED : 0u;
}

static inline int clean_tx_phy_start_owned(uint16_t request, uint16_t pending,
        uint16_t response, uint16_t completion, uint16_t ifs) {
    return request == CLEAN_DCF_FIRED && pending == 1u && response == 0u &&
        !(completion & CLEAN_TX_COMPLETION_CLOSED) && (ifs & 0x100u);
}

static inline int clean_tx_completion_owned(uint16_t request, uint16_t pending,
        uint16_t response, uint16_t completion, uint16_t txe, uint16_t ifs) {
    return request == CLEAN_DCF_FIRED && !pending && !response &&
        !(completion & CLEAN_TX_COMPLETION_CLOSED) &&
        (txe & 0x480u) == 0x400u && !(ifs & 0x100u);
}

static inline int clean_tx_completion_waiting(uint32_t started, uint32_t now,
        uint32_t airtime) {
    return now - started < airtime + 2000u;
}

static inline int clean_tx_incomplete_retryable(uint32_t conditions, uint16_t request,
        uint16_t pending, uint16_t response, uint16_t txe, uint16_t ifs) {
    return (conditions & ~CLEAN_TX_COMPLETION_CLOSED) == 5u &&
        request == CLEAN_DCF_FIRED && !pending &&
        !(txe & 0x80u) && !(ifs & 0x100u) &&
        (response == 0u ? (conditions & CLEAN_TX_COMPLETION_CLOSED) || !(txe & 0x400u) :
                         response == 2u && (txe & 0x400u));
}

static inline int clean_test_ack_count(uint32_t command, uint32_t profile) {
    if (profile != 3u || (command & 255u) != 9u ||
        (command >> 8u) > CLEAN_TEST_ACK_MAX) return -1;
    return (int)(command >> 8u);
}

static inline uint32_t clean_mac_frame_kind(uint8_t fc) {
    return fc == 0xb0u ? 0u : fc == 0x10u ? 1u : fc == 8u ? 2u : 3u;
}

static inline int clean_ack_rate(uint16_t basic, uint32_t received) {
    static const uint8_t rates[12] = {2u,4u,11u,22u,12u,18u,24u,36u,48u,72u,96u,108u};
    if (received >= 12u || !basic || (basic & 0xf000u)) return -1;
    uint32_t first = received < 4u ? 0u : 4u;
    int selected = -1;
    for (uint32_t i = first; i <= received; ++i)
        if ((basic & (1u << i)) && rates[i] <= rates[received]) selected = (int)i;
    if (selected >= 0) return selected;
    uint16_t mandatory = received < 4u ? 3u : 0x150u;
    for (uint32_t i = first; i <= received; ++i)
        if (mandatory & (1u << i)) selected = (int)i;
    return selected;
}

static inline int clean_ack_template(uint8_t plcp[6], uint32_t rate, uint16_t *phyctl1) {
    if (rate >= 12u) return 0;
    if (rate >= 4u) {
        *phyctl1 = clean_ofdm_rates[rate - 4u].phyctl1;
        return clean_ofdm_plcp_rate(plcp, 14u, rate - 4u);
    }
    *phyctl1 = clean_cck_rates[rate].phyctl1;
    return clean_cck_plcp_rate(plcp, 14u, rate);
}

static inline int clean_ht_ack_rate(uint16_t basic, uint32_t mcs) {
    static const uint8_t reference[8] = {12u,24u,36u,48u,72u,96u,108u,108u};
    static const uint8_t rates[12] = {2u,4u,11u,22u,12u,18u,24u,36u,48u,72u,96u,108u};
    if (mcs >= 8u || !basic || (basic & 0xf000u)) return -1;
    int selected = -1;
    for (uint32_t i = 0u; i < 12u; ++i)
        if ((basic & (1u << i)) && rates[i] <= reference[mcs] &&
            (selected < 0 || rates[i] > rates[(uint32_t)selected])) selected = (int)i;
    if (selected >= 0) return selected;
    return clean_ack_rate(0x150u, mcs == 0u ? 4u : mcs < 3u ? 6u : 8u);
}

static inline uint32_t clean_ack_airtime_us(uint16_t basic, uint32_t received) {
    int rate = clean_ack_rate(basic, received);
    if (rate < 0) return 0u;
    if (rate >= 4) return clean_ofdm_airtime_us(14u, (uint32_t)rate - 4u);
    return clean_cck_airtime_us(14u, (uint32_t)rate, 0u);
}

static inline uint32_t clean_ack_timeout_us(uint32_t received, uint8_t slot_us) {
    if (received >= 12u || (slot_us != 9u && slot_us != 20u)) return 0u;
    /* GoodFCS is later than PHY-RXSTART. Include the complete ACK PPDU and a
     * bounded 64-us status/pipeline allowance: the two-radio 2304-byte test
     * measured 115 us from predicted data end to ACK notification. This is
     * an experimental completion deadline, not SIFS timing qualification. */
    uint32_t airtime = received < 4u ? 304u : clean_ofdm_airtime_us(14u, 0u);
    return 10u + slot_us + 64u + airtime;
}

static inline int clean_cck11_valid(const volatile uint8_t *plcp, uint32_t mpdu) {
    return plcp[0] == 110u && clean_cck_plcp_valid(plcp, mpdu);
}

/* Header selection only; callers must independently validate FCS and peers. */
static inline int clean_station_tx_tid(const volatile uint8_t *m, uint32_t n,
                                       uint32_t qos_enabled) {
    if (n < 28u) return -1;
    if ((m[22] & 15u) || (m[1] & 4u)) {
        /* A bounded legacy fragment is an independently ACKed MPDU.
         * QoS/HT fragment scheduling is not installed by this path. */
        uint32_t overhead = (m[1] & 64u) ? 16u : 0u;
        if (m[0] != 8u || (m[1] & 0x83u) != 1u ||
            n <= 28u + overhead || n > 28u + overhead + CLEAN_MSDU_BYTES ||
            ((m[22] & 15u) == 15u && (m[1] & 4u))) return -1;
        return 0;
    }
    if ((m[0] & 15u) == 0u && !(m[1] & 7u)) return 6;
    if (m[0] == 8u && (m[1] & 7u) == 1u) return 0;
    if (!qos_enabled || n < 30u || (m[0] != 0x88u && m[0] != 0xc8u) ||
        (m[1] & 0x87u) != 1u || (m[24] & 0xf8u) || m[25] ||
        ((m[1] & 64u) && n < 46u) ||
        n > 30u + CLEAN_MSDU_BYTES + ((m[1] & 64u) ? 16u : 0u) ||
        (m[0] == 0xc8u && (n != 30u || (m[1] & 64u)))) return -1;
    return m[24] & 7u;
}

static inline uint32_t clean_legacy_exchange_us(const volatile uint8_t *frame,
        uint32_t length, uint32_t ofdm, uint32_t short_preamble, uint16_t basic) {
    if (length < 20u || length > CLEAN_TX_BYTES || ofdm > 1u || short_preamble > 1u)
        return 0u;
    int index = ofdm ? clean_ofdm_rate_index(frame[0]) : clean_cck_rate_index(frame[0]);
    if (index < 0 || (ofdm ? short_preamble || !clean_ofdm_plcp_valid(frame, length - 6u) :
        !clean_cck_preamble_valid((uint32_t)index, short_preamble) ||
        !clean_cck_plcp_valid(frame, length - 6u))) return 0u;
    uint32_t data = ofdm ? clean_ofdm_airtime_us(length - 6u, (uint32_t)index) :
        clean_cck_airtime_us(length - 6u, (uint32_t)index, short_preamble);
    uint32_t ack = clean_ack_airtime_us(basic, (uint32_t)index + (ofdm ? 4u : 0u));
    return data && ack ? data + 10u + ack : 0u;
}

static inline int clean_wmm_policy_decode(clean_edca_ac_t ac[4], uint32_t command,
        uint32_t enabled, uint32_t profile, const volatile uint8_t *bytes) {
    if (!enabled || profile != 3u || command != CLEAN_WMM_POLICY_CMD ||
        bytes[14] > 1u || bytes[15] != 3u || bytes[16] != 1u ||
        bytes[17] || bytes[18] || bytes[19] ||
        clean_ack_rate((uint16_t)(bytes[12] | ((uint16_t)bytes[13] << 8u)), 0u) < 0)
        return 0;
    clean_edca_ac_t decoded[4];
    clean_edca_t candidate;
    if (!clean_edca_decode(decoded, bytes + 20u) || !clean_edca_valid(decoded) ||
        !clean_edca_configure(&candidate, decoded, 0u, 1u, bytes[14]) ||
        clean_edca_select(&candidate, 0u, 0u) < 0) return 0;
    for (uint32_t i = 0u; i < 4u; ++i) ac[i] = decoded[i];
    return 1;
}

static inline int clean_response_rates_prepare(uint16_t words[CLEAN_RESPONSE_RATE_WORDS],
                                                uint16_t basic, uint32_t ht) {
    if (ht > 1u || clean_ack_rate(basic, 0u) < 0) return 0;
    uint16_t prepared[CLEAN_RESPONSE_RATE_WORDS];
    for (uint32_t i = CLEAN_RESPONSE_LEGACY_WORDS; i < CLEAN_RESPONSE_RATE_WORDS; ++i)
        prepared[i] = 0u;
    for (uint32_t i = 0u; i < 12u; ++i) {
        uint8_t plcp[6];
        uint16_t phy;
        int rate = clean_ack_rate(basic, i);
        if (rate < 0 || !clean_ack_template(plcp, (uint32_t)rate, &phy)) return 0;
        for (uint32_t w = 0u; w < 3u; ++w)
            prepared[4u*i + w] = (uint16_t)(plcp[2u*w] | ((uint16_t)plcp[2u*w + 1u] << 8u));
        prepared[4u*i + 3u] = phy;
        prepared[48u + i] = (uint16_t)clean_ack_airtime_us(basic, i);
    }
    if (ht) for (uint32_t i = 0u; i < 8u; ++i) {
        uint8_t plcp[6];
        uint16_t phy;
        int rate = clean_ht_ack_rate(basic, i);
        if (rate < 0 || !clean_ack_template(plcp, (uint32_t)rate, &phy)) return 0;
        uint32_t at = CLEAN_RESPONSE_LEGACY_WORDS + CLEAN_HT_ACK_RECORD_WORDS*i;
        for (uint32_t w = 0u; w < 3u; ++w)
            prepared[at + w] = (uint16_t)(plcp[2u*w] | ((uint16_t)plcp[2u*w + 1u] << 8u));
        prepared[at + 3u] = phy;
        prepared[at + 4u] = rate < 4 ? 0x40u : 0x41u;
        prepared[at + 5u] = (uint16_t)(rate < 4 ?
            clean_cck_airtime_us(14u, (uint32_t)rate, 0u) :
            clean_ofdm_airtime_us(14u, (uint32_t)rate - 4u));
    }
    for (uint32_t i = 0u; i < CLEAN_RESPONSE_RATE_WORDS; ++i) words[i] = prepared[i];
    return 1;
}

static inline uint32_t clean_response_rate_word(uint32_t index) {
    return index < CLEAN_RESPONSE_LEGACY_WORDS ? CLEAN_ACK_TABLE + index :
        CLEAN_HT_ACK_TABLE + index - CLEAN_RESPONSE_LEGACY_WORDS;
}

/* The caller keeps PSM paused until all policy fields have also been verified. */
static inline int clean_response_rates_install(clean_response_io_t *io,
                                                uint16_t basic, uint32_t ht) {
    uint16_t words[CLEAN_RESPONSE_RATE_WORDS];
    io->failed_word = 0u;
    io->actual = basic;
    if (!clean_response_rates_prepare(words, basic, ht)) return 0;
    uint32_t count = ht ? CLEAN_RESPONSE_RATE_WORDS : CLEAN_RESPONSE_LEGACY_WORDS;
    for (uint32_t i = 0u; i < count; ++i)
        io->write(io->context, clean_response_rate_word(i), words[i]);
    for (uint32_t i = 0u; i < count; ++i) {
        uint32_t word = clean_response_rate_word(i);
        uint16_t actual = io->read(io->context, word);
        if (actual != words[i]) {
            io->failed_word = word;
            io->actual = actual;
            return 0;
        }
    }
    return 1;
}

#endif

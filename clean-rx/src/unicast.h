#ifndef CLEAN_RX_UNICAST_H
#define CLEAN_RX_UNICAST_H

#include "phy_rates.h"

#define CLEAN_UNICAST_ADDR 0x201e0u
#define CLEAN_UNICAST_MAGIC 0x314b4341u
#define CLEAN_TX_ACK_REQUIRED 0x80000000u
#define CLEAN_ACK_ENABLE 0x630u
#define CLEAN_ACK_WAITING 0x631u
#define CLEAN_ACK_SEEN 0x632u
#define CLEAN_ACK_SENT 0x633u
#define CLEAN_ACK_ERROR 0x634u
#define CLEAN_RESPONSE_POLICY 0x637u
#define CLEAN_CTS_ALLOWED 0x638u
#define CLEAN_ACK_CONSUMED 0x639u
#define CLEAN_RESPONSE_INTERNAL_ACK 4u

typedef struct {
    uint8_t active, ended;
} clean_tx_tail_t;

static inline int clean_tx_tail_poll(clean_tx_tail_t *s, int active) {
    if (s->ended) return 0;
    if (active) s->active = 1u;
    else if (s->active) {
        s->ended = 1u;
        return 1;
    }
    return 0;
}

#define CLEAN_RESPONSE_FC 0x63du
#define CLEAN_CTS_SENT 0x63eu
#define CLEAN_SHORT_SENT 0x63fu
#define CLEAN_ACK_TABLE 0x640u
#define CLEAN_ACK_AIRTIME 0x670u
#define CLEAN_ACK_KINDS 0x67cu
#define CLEAN_ACK_PEER 0x63au
#define CLEAN_ACK_HOOK 314u
#define CLEAN_DCF_ENABLE 0x680u
#define CLEAN_DCF_REQUEST 0x681u
#define CLEAN_DCF_SLOTS 0x682u
#define CLEAN_DCF_EIFS 0x683u
#define CLEAN_DCF_EIFS_UNTIL 0x684u
#define CLEAN_DCF_EIFS_US 0x685u
#define CLEAN_DCF_RX_LO 0x686u
#define CLEAN_DCF_RX_HI 0x687u
#define CLEAN_DCF_TX_LO 0x688u
#define CLEAN_DCF_TX_HI 0x689u
#define CLEAN_DCF_ACK_US 0x68au
#define CLEAN_DCF_ACK_UNTIL 0x68bu
#define CLEAN_DCF_IFS_SAMPLE 0x68cu
#define CLEAN_DCF_CLOCK_SAMPLE 0x68du
#define CLEAN_DCF_POLLS 0x68eu
#define CLEAN_DCF_IDLE_SINCE 0x68fu
#define CLEAN_DCF_IDLE_VALID 0x690u
#define CLEAN_DCF_SLOT_US 0x691u
#define CLEAN_ACK_STAGE 0x692u
#define CLEAN_ACK_FRAME_CONTROL 0x693u
#define CLEAN_ACK_RX_STATUS 0x694u
#define CLEAN_BAD_FRAME_CONTROL 0x695u
#define CLEAN_BAD_FRAME_LENGTH 0x696u
#define CLEAN_ACK_ELAPSED 0x697u
#define CLEAN_ACK_WINDOW 0x698u
#define CLEAN_RESPONSE_DELAY 0x69au
#define CLEAN_DCF_TX_PENDING 0x69bu
#define CLEAN_DCF_DIFS_US 0x69cu
#define CLEAN_RESPONSE_PENDING 0x69du
#define CLEAN_TEST_ACK_REMAINING 0x69eu
#define CLEAN_TEST_ACK_DROPPED 0x69fu
#define CLEAN_TEST_ACK_MAX 16u
#define CLEAN_DCF_FIRED 2u
#define CLEAN_DCF_DEFERRED 3u
#define CLEAN_MAC_RADIO_ADDR 0x20210u
#define CLEAN_MAC_RADIO_MAGIC 0x3343414du
#define CLEAN_MAC_TRACE_ADDR 0x20250u
#define CLEAN_MAC_TRACE_MAGIC 0x3152434du

enum { CLEAN_UNICAST_IDLE, CLEAN_UNICAST_ACKED, CLEAN_UNICAST_TIMEOUT,
       CLEAN_UNICAST_BUSY };

typedef struct {
    uint32_t magic, result, attempts, ack_received;
    uint32_t ack_sent, ack_error, started_us, finished_us;
    uint32_t ack_txe_status, ack_ifs_status;
} clean_unicast_status_t;

typedef struct {
    uint32_t magic, profile, cw, slots, nav_until, deferrals, control_acks, timeouts;
    uint32_t rx_waits, pending_rx;
    uint32_t cts_responses, short_responses;
    uint32_t policy_pending;
    uint32_t prepared_acks;
    uint32_t address_lo, address_hi;
} clean_mac_radio_status_t;

typedef struct {
    uint32_t magic;
    uint32_t responses[4], published[4], invalid;
    uint32_t ifs_status, ifs_backoff, ifs_idle, psm_ifs, psm_tsf, psm_polls;
    uint32_t ack_stage, ack_frame_control, ack_rx_status;
    uint32_t bad_fcs, bad_frame_control, bad_frame_length;
    uint32_t ack_elapsed, ack_window;
    uint32_t response_delay;
    uint32_t ack_dropped;
} clean_mac_trace_status_t;

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

#endif

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
#ifndef CLEAN_RX_HT_RESPONSE
#define CLEAN_RX_HT_RESPONSE 0
#endif
#define CLEAN_QOS_RESPONSE_POLICY 0x6bau
#define CLEAN_HT_ACK_TABLE 0x6c0u
#define CLEAN_HT_ACK_RECORD_WORDS 6u
#define CLEAN_HT_RESPONSE_CMD 14u
#define CLEAN_ACK_RX_SHM 0x6f0u
#define CLEAN_ACK_RX_WORDS 14u
#define CLEAN_ACK_RX_ADDR (CLEAN_RX_LAYOUT_ADDR + 4u * CLEAN_RX_LAYOUT_WORDS)
#define CLEAN_ACK_RX_MAGIC 0x32524b41u
#define CLEAN_ACK_RX_DETAIL 720u
#define CLEAN_HT_ACK_MPDU 60u
#ifndef CLEAN_RX_EDCA
#define CLEAN_RX_EDCA 0
#endif
#define CLEAN_EDCA_CMD 15u
#define CLEAN_EDCA_STATUS_ADDR 0x204a0u
#define CLEAN_EDCA_STATUS_MAGIC 0x31434445u
#define CLEAN_EDCA_DETAIL 752u

typedef struct {
    uint32_t magic;
    uint16_t cw_min, cw_max, aifs_us, txop_32us;
    uint8_t ac, tid, enabled, reserved;
} clean_edca_status_t;

_Static_assert(CLEAN_RX_EDCA == 0 || (CLEAN_RX_EDCA == 1 && CLEAN_RX_HT_RESPONSE),
               "EDCA requires the gated QoS response build");
_Static_assert(sizeof(clean_edca_status_t) == 16u &&
               CLEAN_EDCA_DETAIL >= CLEAN_ACK_RX_DETAIL + 32u &&
               CLEAN_EDCA_DETAIL + sizeof(clean_edca_status_t) <= 800u,
               "EDCA evidence must fit before MAC reports");

static inline int clean_edca_command_valid(uint32_t command, uint32_t enabled,
                                           uint32_t profile) {
    return enabled && profile == 3u && command == CLEAN_EDCA_CMD;
}

#define CLEAN_HT_FAILURE_SHM 0x700u
#define CLEAN_HT_FAILURE_WORDS 78u
#define CLEAN_HT_FAILURE_ADDR 0x20400u
#define CLEAN_HT_FAILURE_MAGIC 0x32464848u
#define CLEAN_HT_FAILURE_DETAIL 1640u

#define CLEAN_AUTH_FAILURE_SHM 0x750u
#define CLEAN_AUTH_FAILURE_WORDS 28u
#define CLEAN_AUTH_FAILURE_ADDR 0x204b0u
#define CLEAN_AUTH_FAILURE_MAGIC 0x31464142u
#define CLEAN_AUTH_FAILURE_DETAIL 600u

typedef struct {
    uint32_t magic;
    uint16_t count, length, phy, status1, status2, time_lo, time_hi, reserved;
    uint8_t raw[40];
} clean_auth_failure_t;

_Static_assert(sizeof(clean_auth_failure_t) == 4u + 2u * CLEAN_AUTH_FAILURE_WORDS &&
               CLEAN_AUTH_FAILURE_SHM >= CLEAN_HT_FAILURE_SHM + CLEAN_HT_FAILURE_WORDS &&
               CLEAN_AUTH_FAILURE_SHM + CLEAN_AUTH_FAILURE_WORDS <= 0x7a0u &&
               CLEAN_AUTH_FAILURE_ADDR >= CLEAN_EDCA_STATUS_ADDR + sizeof(clean_edca_status_t) &&
               CLEAN_AUTH_FAILURE_ADDR + sizeof(clean_auth_failure_t) <= 0x20d80u &&
               CLEAN_AUTH_FAILURE_DETAIL + sizeof(clean_auth_failure_t) <= 796u,
               "Rejected authentication evidence must not overlap other records");

typedef struct {
    uint32_t magic;
    uint16_t count, cause, time_lo, time_hi, length, status1, status2, phy;
    uint16_t raw[(CLEAN_PLCP_BYTES + CLEAN_HT_ACK_MPDU) / 2u];
    uint16_t repeated_raw[(CLEAN_PLCP_BYTES + CLEAN_HT_ACK_MPDU) / 2u];
    uint16_t final_length, final_status1, final_status2, final_phy;
} clean_ht_failure_t;

_Static_assert(sizeof(clean_ht_failure_t) == 4u + 2u * CLEAN_HT_FAILURE_WORDS &&
               CLEAN_HT_FAILURE_SHM >= CLEAN_ACK_RX_SHM + CLEAN_ACK_RX_WORDS &&
               CLEAN_HT_FAILURE_SHM + CLEAN_HT_FAILURE_WORDS <= 0x7a0u &&
               CLEAN_HT_FAILURE_ADDR + sizeof(clean_ht_failure_t) <= 0x20d80u &&
               CLEAN_HT_FAILURE_DETAIL >= 1088u + CLEAN_DETAIL_PACKET_BYTES &&
               CLEAN_HT_FAILURE_DETAIL + sizeof(clean_ht_failure_t) <= 1972u,
               "Failed HT fixture evidence must not overlap captures or other diagnostics");
_Static_assert(CLEAN_EDCA_STATUS_ADDR >= CLEAN_HT_FAILURE_ADDR + sizeof(clean_ht_failure_t) &&
               CLEAN_EDCA_STATUS_ADDR + sizeof(clean_edca_status_t) <= 0x20d80u,
               "EDCA status must not overlap failed captures or frame slots");

typedef struct {
    uint32_t magic;
    uint16_t count, time_lo, time_hi, phy, mpdu[7], plcp[3];
} clean_ack_rx_t;

_Static_assert(sizeof(clean_ack_rx_t) == 32u &&
               CLEAN_ACK_RX_ADDR + sizeof(clean_ack_rx_t) <= CLEAN_HT_FAILURE_ADDR,
               "ACK evidence must fit between layout manifest and capture slots");

_Static_assert(CLEAN_RX_HT_RESPONSE == 0 ||
               (CLEAN_RX_HT_RESPONSE == 1 && CLEAN_RX_HT_RX),
               "HT responses require the extended receive build");
_Static_assert(CLEAN_QOS_RESPONSE_POLICY >= CLEAN_HT_EARLY_SHM + CLEAN_HT_EARLY_WORDS &&
               CLEAN_QOS_RESPONSE_POLICY < CLEAN_HT_ACK_TABLE &&
               CLEAN_HT_ACK_TABLE + 8u * CLEAN_HT_ACK_RECORD_WORDS <= CLEAN_ACK_RX_SHM &&
               CLEAN_ACK_RX_SHM + CLEAN_ACK_RX_WORDS <= 0x7a0u,
               "HT response controls must not overlap captures, traces or statistics");

static inline int clean_ht_response_policy(uint32_t command, uint32_t enabled,
                                          uint32_t profile) {
    if (!enabled || profile != 3u || (command & 255u) != CLEAN_HT_RESPONSE_CMD ||
        (command >> 8u) > 3u) return -1;
    return (int)(command >> 8u);
}

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
#define CLEAN_DCF_TRACE_SHM 0x6bbu
#define CLEAN_DCF_TRACE_ADDR 0x203b0u
#define CLEAN_DCF_TRACE_MAGIC 0x32464344u
#define CLEAN_DCF_TRACE_DETAIL 976u
#define CLEAN_DCF_MAX_SLOTS 1023u
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
    uint32_t magic;
    uint16_t draw, before, after, clock, fault, slots;
} clean_dcf_trace_t;

_Static_assert(CLEAN_DCF_TRACE_SHM > CLEAN_QOS_RESPONSE_POLICY &&
               CLEAN_DCF_TRACE_SHM + 5u <= CLEAN_HT_ACK_TABLE &&
               sizeof(clean_dcf_trace_t) == 16u &&
               CLEAN_DCF_TRACE_ADDR + sizeof(clean_dcf_trace_t) <= CLEAN_RX_LAYOUT_ADDR &&
               CLEAN_DCF_TRACE_DETAIL + sizeof(clean_dcf_trace_t) <= 1000u,
               "DCF counter evidence must not overlap control or evidence regions");

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
    uint32_t rx_filtered, rx_capture_max_us;
} clean_mac_trace_status_t;

_Static_assert(CLEAN_MAC_TRACE_ADDR + sizeof(clean_mac_trace_status_t) <= CLEAN_RX_TRACE_ADDR,
               "MAC trace must not overlap RX/TX traces");
_Static_assert(864u + sizeof(clean_mac_trace_status_t) <= CLEAN_DCF_TRACE_DETAIL,
               "Clocked HT MAC trace must fit before DCF evidence");

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

#endif

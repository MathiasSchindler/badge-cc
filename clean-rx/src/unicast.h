#ifndef CLEAN_RX_UNICAST_H
#define CLEAN_RX_UNICAST_H

#include "phy_rates.h"
#include "mac.h"
#include "tx_diag.h"

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
#ifndef CLEAN_RX_WMM
#define CLEAN_RX_WMM 0
#endif
#define CLEAN_WMM_POLICY_CMD 16u
#define CLEAN_WMM_POLICY_BYTES 36u
#define CLEAN_WMM_DETAIL 768u
#define CLEAN_WMM_MAGIC 0x314d4d57u

_Static_assert(CLEAN_RX_WMM == 0 || (CLEAN_RX_WMM == 1 && CLEAN_RX_EDCA),
               "WMM station operation requires EDCA and QoS responses");

typedef struct {
    uint32_t magic;
    uint16_t cw_min, cw_max, aifs_us, txop_32us;
    uint8_t ac, tid, enabled, reserved;
} clean_edca_status_t;

_Static_assert(CLEAN_RX_EDCA == 0 || (CLEAN_RX_EDCA == 1 && CLEAN_RX_HT_RESPONSE),
               "EDCA requires the gated QoS response build");
_Static_assert(sizeof(clean_edca_status_t) == 16u &&
               CLEAN_EDCA_DETAIL >= CLEAN_ACK_RX_DETAIL + 32u &&
               CLEAN_EDCA_DETAIL + sizeof(clean_edca_status_t) <= CLEAN_WMM_DETAIL &&
               CLEAN_WMM_DETAIL + 24u <= 796u,
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
#ifndef CLEAN_RX_REJECT_PROBE
#define CLEAN_RX_REJECT_PROBE 0
#endif
_Static_assert(CLEAN_RX_REJECT_PROBE == 0 || CLEAN_RX_REJECT_PROBE == 1,
               "Rejected RX probe must be zero or one");
_Static_assert(!CLEAN_RX_REJECT_PROBE || !CLEAN_RX_HT_RX,
               "Rejected RX probe borrows unused legacy-only HT failure storage");
#define CLEAN_RX_REJECT_SHM CLEAN_HT_FAILURE_SHM
#define CLEAN_RX_REJECT_ADDR CLEAN_HT_FAILURE_ADDR
#define CLEAN_RX_REJECT_DETAIL 1060u
typedef struct {
    uint16_t next_length;
    uint8_t tail[26];
} clean_rx_reject_probe_t;
_Static_assert(sizeof(clean_rx_reject_probe_t) == 28u &&
               CLEAN_RX_REJECT_SHM + sizeof(clean_rx_reject_probe_t) / 2u <= CLEAN_AUTH_FAILURE_SHM &&
               CLEAN_RX_REJECT_ADDR + sizeof(clean_rx_reject_probe_t) <= CLEAN_EDCA_STATUS_ADDR &&
               CLEAN_RX_REJECT_DETAIL >= CLEAN_PHY_EDGE_DETAIL + sizeof(clean_phy_edge_t) &&
               CLEAN_RX_REJECT_DETAIL + sizeof(clean_rx_reject_probe_t) <= 1088u,
               "Rejected RX probe must fit before authentication/status/packet storage");

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

static inline uint32_t clean_tx_tail_ack_start(const clean_tx_tail_t *s,
        uint32_t started, uint32_t phy_started) {
    return s->ended ? started : phy_started;
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
#define CLEAN_TX_CLEANUP_REQUEST 0x6acu
#define CLEAN_TX_SELECT_OPERAND 0x781u
#define CLEAN_TX_SELECT_READBACK 0x782u
#define CLEAN_TX_CLEANUP_COUNT 0x6adu
#define CLEAN_TX_COMPLETION 0x6aeu
#define CLEAN_TX_PRE_RESPONSE 0x6afu
#define CLEAN_TX_COMPLETION_CLOSED 8u
#define CLEAN_TX_COMPLETION_ADDR 0x204f0u
#define CLEAN_TX_COMPLETION_DETAIL 492u
#define CLEAN_TX_COMPLETION_MAGIC 0x3143544du
#define CLEAN_TX_START_HANDOFFS_ADDR (CLEAN_TX_COMPLETION_ADDR + 28u)
#define CLEAN_TX_COMPLETION_HANDOFFS_ADDR (CLEAN_TX_START_HANDOFFS_ADDR + 4u)
typedef struct {
    uint32_t magic, latched, preserved, raw_conditions;
    uint32_t request, pipeline;
    uint32_t timeouts;
} clean_tx_completion_status_t;
_Static_assert(CLEAN_TX_COMPLETION_ADDR >= CLEAN_AUTH_FAILURE_ADDR + sizeof(clean_auth_failure_t) &&
               CLEAN_TX_COMPLETION_ADDR + sizeof(clean_tx_completion_status_t) ==
                   CLEAN_TX_START_HANDOFFS_ADDR &&
               CLEAN_TX_COMPLETION_HANDOFFS_ADDR + 4u <= 0x20d00u &&
               CLEAN_TX_COMPLETION_DETAIL >= 296u + 180u &&
               CLEAN_TX_COMPLETION_DETAIL + sizeof(clean_tx_completion_status_t) <= 520u,
               "Owned TX completion evidence must not overlap existing reports");
_Static_assert(CLEAN_TX_CLEANUP_REQUEST >= CLEAN_RX_TRACE_SHM + CLEAN_RX_TRACE_WORDS &&
               CLEAN_TX_PRE_RESPONSE + 1u <= CLEAN_HT_EARLY_SHM,
               "TX cleanup state must not overlap receive evidence");
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
#define CLEAN_ACK_TEST_SHM (CLEAN_AUTH_FAILURE_SHM + CLEAN_AUTH_FAILURE_WORDS)
#define CLEAN_ACK_TEST_LIMIT (CLEAN_ACK_TEST_SHM + 0u)
#define CLEAN_ACK_TEST_FAULT (CLEAN_ACK_TEST_SHM + 1u)
#define CLEAN_ACK_TEST_PHASE (CLEAN_ACK_TEST_SHM + 2u)
#define CLEAN_ACK_TEST_BEFORE (CLEAN_ACK_TEST_SHM + 3u)
#define CLEAN_ACK_TEST_AFTER (CLEAN_ACK_TEST_SHM + 4u)
#define CLEAN_ACK_TEST_DROPPED_BEFORE (CLEAN_ACK_TEST_SHM + 5u)
#define CLEAN_ACK_TEST_CLOCK (CLEAN_ACK_TEST_SHM + 6u)
#define CLEAN_ACK_TEST_FC (CLEAN_ACK_TEST_SHM + 7u)
#define CLEAN_ACK_TEST_SEQUENCE (CLEAN_ACK_TEST_SHM + 8u)
#define CLEAN_ACK_TEST_EPOCH (CLEAN_ACK_TEST_SHM + 9u)
#define CLEAN_ACK_TEST_WORDS 10u
#define CLEAN_ACK_TEST_ADDR (CLEAN_TX_COMPLETION_HANDOFFS_ADDR + 4u)
#define CLEAN_ACK_TEST_DETAIL 688u
#define CLEAN_ACK_TEST_MAGIC 0x31545341u
typedef struct {
    uint32_t magic;
    uint16_t configured, limit, remaining, dropped, fault, phase;
    uint16_t before, after, dropped_before, clock, frame_control, sequence, epoch, reserved;
} clean_ack_test_status_t;
_Static_assert(sizeof(clean_ack_test_status_t) == 32u &&
               CLEAN_ACK_TEST_SHM + CLEAN_ACK_TEST_WORDS <= 0x7a0u &&
               CLEAN_ACK_TEST_ADDR + sizeof(clean_ack_test_status_t) <= 0x20d00u &&
               CLEAN_ACK_TEST_DETAIL >= 584u + 100u &&
               CLEAN_ACK_TEST_DETAIL + sizeof(clean_ack_test_status_t) <= 720u,
               "ACK suppression evidence must not overlap captures or diagnostics");

static inline int clean_ack_test_status_valid(const clean_ack_test_status_t *s) {
    return s->magic == CLEAN_ACK_TEST_MAGIC && s->configured <= CLEAN_TEST_ACK_MAX &&
        s->limit == s->configured && s->remaining <= s->limit &&
        s->dropped <= s->limit && s->remaining + s->dropped == s->limit &&
        !s->fault && !s->phase && !s->reserved;
}

#define CLEAN_ACK_OBSERVE_SHM (CLEAN_ACK_TEST_SHM + CLEAN_ACK_TEST_WORDS)
#define CLEAN_ACK_OBSERVE_NOW CLEAN_ACK_OBSERVE_SHM
#define CLEAN_ACK_OBSERVE_WAITING (CLEAN_ACK_OBSERVE_SHM + 1u)
#define CLEAN_ACK_OBSERVE_DUE (CLEAN_ACK_OBSERVE_SHM + 2u)
#define CLEAN_ACK_OBSERVE_START (CLEAN_ACK_OBSERVE_SHM + 3u)
#define CLEAN_ACK_OBSERVE_WINDOW (CLEAN_ACK_OBSERVE_SHM + 4u)
#define CLEAN_ACK_CLEAR_REASON (CLEAN_ACK_OBSERVE_SHM + 5u)
#define CLEAN_ACK_CLEAR_NOW (CLEAN_ACK_OBSERVE_SHM + 6u)
#define CLEAN_ACK_CLEAR_DUE (CLEAN_ACK_OBSERVE_SHM + 7u)
#define CLEAN_ACK_OBSERVE_WORDS 8u
#define CLEAN_ACK_OBSERVE_ADDR (CLEAN_ACK_TEST_ADDR + sizeof(clean_ack_test_status_t))
#define CLEAN_ACK_OBSERVE_DETAIL 832u
#define CLEAN_ACK_OBSERVE_MAGIC 0x3153424fu
typedef struct {
    uint32_t magic;
    uint16_t observed_at, waiting, due, started, window, clear_reason, cleared_at, clear_due;
    uint32_t arm_at, arm_started, arm_window;
} clean_ack_observe_t;
_Static_assert(sizeof(clean_ack_observe_t) == 32u &&
               CLEAN_ACK_OBSERVE_SHM + CLEAN_ACK_OBSERVE_WORDS <= 0x7a0u &&
               CLEAN_ACK_OBSERVE_ADDR + sizeof(clean_ack_observe_t) <= 0x20d00u &&
               CLEAN_ACK_OBSERVE_DETAIL + sizeof(clean_ack_observe_t) <= 1000u,
               "ACK deadline evidence must fit before transmit status and EDCA");

#define CLEAN_ACK_ARITH_SHM (CLEAN_ACK_OBSERVE_SHM + CLEAN_ACK_OBSERVE_WORDS)
#define CLEAN_ACK_TEST_AIRTIME CLEAN_ACK_ARITH_SHM
#define CLEAN_ACK_TEST_SUM (CLEAN_ACK_ARITH_SHM + 1u)
#define CLEAN_ACK_TEST_LIMIT_SEEN (CLEAN_ACK_ARITH_SHM + 2u)
#define CLEAN_ACK_ARITH_ADDR (CLEAN_ACK_OBSERVE_ADDR + sizeof(clean_ack_observe_t))
#define CLEAN_ACK_ARITH_DETAIL 864u
#define CLEAN_ACK_ARITH_MAGIC 0x314d5553u
typedef struct {
    uint32_t magic;
    uint16_t airtime, sum, limit, reserved;
} clean_ack_arith_t;
_Static_assert(CLEAN_TX_SELECT_OPERAND >= CLEAN_ACK_ARITH_SHM + 3u &&
               CLEAN_TX_SELECT_READBACK + 1u <= 0x7a0u &&
               CLEAN_TX_ERROR_ADDR >= CLEAN_ACK_ARITH_ADDR + sizeof(clean_ack_arith_t) &&
               CLEAN_TX_ERROR_DETAIL >= CLEAN_ACK_ARITH_DETAIL + sizeof(clean_ack_arith_t),
               "TX phase evidence must not overlap ACK arithmetic");
_Static_assert(sizeof(clean_ack_arith_t) == 12u &&
               CLEAN_ACK_ARITH_SHM + 3u <= 0x7a0u &&
               CLEAN_ACK_ARITH_ADDR + sizeof(clean_ack_arith_t) <= 0x20d00u &&
               CLEAN_ACK_ARITH_DETAIL + sizeof(clean_ack_arith_t) <= 1000u,
               "ACK arithmetic operands must not overlap transmit status or EDCA");

#define CLEAN_DCF_FIRED 2u
#define CLEAN_DCF_DEFERRED 3u
#define CLEAN_TX_CLEANUP_ABORT 2u
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

#define CLEAN_RESPONSE_LEGACY_WORDS 60u
#define CLEAN_RESPONSE_RATE_WORDS (CLEAN_RESPONSE_LEGACY_WORDS + 8u * CLEAN_HT_ACK_RECORD_WORDS)

typedef struct {
    uint16_t (*read)(void *context, uint32_t word);
    void (*write)(void *context, uint32_t word, uint16_t value);
    void *context;
    uint32_t failed_word;
    uint16_t actual;
} clean_response_io_t;

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

_Static_assert(CLEAN_ACK_AIRTIME == CLEAN_ACK_TABLE + 48u &&
               CLEAN_ACK_AIRTIME + 12u == CLEAN_ACK_KINDS &&
               CLEAN_RESPONSE_LEGACY_WORDS == 12u * 5u,
               "Response table layout must match the PSM rate and airtime loads");

#endif

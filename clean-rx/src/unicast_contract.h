#ifndef CLEAN_RX_UNICAST_CONTRACT_H
#define CLEAN_RX_UNICAST_CONTRACT_H

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

#define CLEAN_HT_FAILURE_SHM 0x700u
#define CLEAN_HT_FAILURE_WORDS 78u
#define CLEAN_HT_FAILURE_ADDR 0x20400u
#define CLEAN_HT_FAILURE_MAGIC 0x32464848u
#define CLEAN_HT_FAILURE_DETAIL 1640u
#ifndef CLEAN_RX_HT_REJECT_TRACE
#define CLEAN_RX_HT_REJECT_TRACE 0
#endif
#ifndef CLEAN_RX_HT_REJECT_BAD_FCS
#define CLEAN_RX_HT_REJECT_BAD_FCS 0
#endif
#ifndef CLEAN_RX_HT_REJECT_BASELINE
#define CLEAN_RX_HT_REJECT_BASELINE 0
#endif
#ifndef CLEAN_RX_HT_CANDIDATE_TRACE
#define CLEAN_RX_HT_CANDIDATE_TRACE 0
#endif
#define CLEAN_HT_CANDIDATE_SHM 0x400u
#define CLEAN_HT_CANDIDATE_ADDR 0x20800u
#define CLEAN_HT_CANDIDATE_DETAIL 1216u
#define CLEAN_HT_CANDIDATE_MAGIC 0x31435048u
#define CLEAN_HT_CANDIDATE_WORDS 50u
typedef struct {
    uint32_t magic;
    uint16_t sequence, start_lo, start_hi, length, status1, status2, phy;
    uint16_t copy[5], raw[33];
    uint16_t cause, reject_lo, reject_hi, reject_length, committed;
} clean_ht_candidate_trace_t;
_Static_assert(sizeof(clean_ht_candidate_trace_t) == 4u + 2u*CLEAN_HT_CANDIDATE_WORDS &&
               CLEAN_HT_CANDIDATE_SHM > CLEAN_AMPDU_DMA_SUBMITTED &&
               CLEAN_HT_CANDIDATE_SHM + CLEAN_HT_CANDIDATE_WORDS <= 0x600u &&
               CLEAN_HT_CANDIDATE_ADDR == CLEAN_AMPDU_IF_ADDR &&
               sizeof(clean_ht_candidate_trace_t) <= 4u + 2u*CLEAN_AMPDU_IF_WORDS &&
               CLEAN_HT_CANDIDATE_DETAIL >= 1104u + 18u + CLEAN_HT_ACK_MPDU &&
               CLEAN_HT_CANDIDATE_DETAIL + sizeof(clean_ht_candidate_trace_t) <= 1520u,
               "Per-candidate evidence must fit diagnostic-only aggregate and ACK60 report gaps");
#define CLEAN_HT_REJECT_COPY_SHM 0x790u
#define CLEAN_HT_REJECT_COPY_ADDR 0x20600u
#define CLEAN_HT_REJECT_COPY_DETAIL 1040u
#define CLEAN_HT_REJECT_COPY_MAGIC 0x31504348u
#define CLEAN_HT_REJECT_COPY_WORDS 16u
typedef struct {
    uint32_t magic;
    uint16_t count, cause, time_lo, time_hi, length, stream_threshold;
    uint16_t before[5], after[5];
} clean_ht_reject_copy_t;
_Static_assert(sizeof(clean_ht_reject_copy_t) == 4u + 2u*CLEAN_HT_REJECT_COPY_WORDS &&
               CLEAN_HT_REJECT_COPY_SHM + CLEAN_HT_REJECT_COPY_WORDS <= 0x7a0u &&
               CLEAN_HT_REJECT_COPY_ADDR >= CLEAN_PHY_EDGE_ADDR + sizeof(clean_phy_edge_t) &&
               CLEAN_HT_REJECT_COPY_ADDR == CLEAN_AMPDU_PROBE_ADDR &&
               sizeof(clean_ht_reject_copy_t) <= 4u + 2u*CLEAN_AMPDU_PROBE_WORDS &&
               CLEAN_HT_REJECT_COPY_DETAIL + sizeof(clean_ht_reject_copy_t) <= 1088u,
               "Rejected HT copy trace must fit reserved SHM, SRAM and HT report space");

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
_Static_assert(CLEAN_HT_REJECT_COPY_SHM >= CLEAN_AUTH_FAILURE_SHM + CLEAN_AUTH_FAILURE_WORDS &&
               CLEAN_HT_REJECT_COPY_ADDR >= CLEAN_AUTH_FAILURE_ADDR + sizeof(clean_auth_failure_t),
               "Rejected HT copy trace must follow authentication evidence");

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

typedef struct {
    uint8_t active, ended;
} clean_tx_tail_t;

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
_Static_assert(CLEAN_HT_REJECT_COPY_ADDR >= CLEAN_TX_COMPLETION_HANDOFFS_ADDR + 4u &&
               CLEAN_HT_REJECT_COPY_SHM >= CLEAN_TX_SELECT_READBACK + 1u,
               "Rejected HT copy evidence must not overlap TX completion, handoff or select state");
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
_Static_assert(CLEAN_HT_REJECT_COPY_SHM >= CLEAN_ACK_ARITH_SHM + 3u &&
               CLEAN_HT_REJECT_COPY_ADDR >= CLEAN_ACK_ARITH_ADDR + sizeof(clean_ack_arith_t),
               "Rejected HT copy evidence must not overlap ACK suppression, deadline or arithmetic state");

#define CLEAN_DCF_FIRED 2u
#define CLEAN_DCF_DEFERRED 3u
#define CLEAN_TX_CLEANUP_ABORT 2u

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

#define CLEAN_RESPONSE_LEGACY_WORDS 60u
#define CLEAN_RESPONSE_RATE_WORDS (CLEAN_RESPONSE_LEGACY_WORDS + 8u * CLEAN_HT_ACK_RECORD_WORDS)

typedef struct {
    uint16_t (*read)(void *context, uint32_t word);
    void (*write)(void *context, uint32_t word, uint16_t value);
    void *context;
    uint32_t failed_word;
    uint16_t actual;
} clean_response_io_t;

_Static_assert(CLEAN_ACK_AIRTIME == CLEAN_ACK_TABLE + 48u &&
               CLEAN_ACK_AIRTIME + 12u == CLEAN_ACK_KINDS &&
               CLEAN_RESPONSE_LEGACY_WORDS == 12u * 5u,
               "Response table layout must match the PSM rate and airtime loads");

#endif

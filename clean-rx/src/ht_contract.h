#ifndef CLEAN_RX_HT_CONTRACT_H
#define CLEAN_RX_HT_CONTRACT_H

#include "tx_diag.h"
#include "unicast_contract.h"

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

#define CLEAN_HT_STIMULUS_VERSION 6u
#define CLEAN_HT_STIMULUS_MPDU 554u
#define CLEAN_HT_STIMULUS_PSDU 1118u
#define CLEAN_HT_STIMULUS_STORAGE 1132u
#define CLEAN_HT_STIMULUS_COUNT 24u
_Static_assert(CLEAN_HT_STIMULUS_PSDU ==
               4u+((CLEAN_HT_STIMULUS_MPDU+3u)&~3u)+4u+CLEAN_HT_STIMULUS_MPDU &&
               CLEAN_HT_STIMULUS_STORAGE == CLEAN_HT_DATA_HEADER+6u+CLEAN_HT_STIMULUS_PSDU,
               "Fixed two-body stimulus length and staging geometry");

#define CLEAN_HT_HANDOFF_CASES 5u

#endif

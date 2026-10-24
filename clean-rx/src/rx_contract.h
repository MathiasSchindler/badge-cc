#ifndef CLEAN_RX_RX_CONTRACT_H
#define CLEAN_RX_RX_CONTRACT_H

#include "frame_limits.h"
#include "types.h"

#define CLEAN_HT_HANDOFF_VERSION (CLEAN_RX_HT_VARIABLE ? 9u : CLEAN_RX_HT_HANDOFF_GUARD ? 8u : 7u)
#define CLEAN_HT_HANDOFF_PSDU (CLEAN_RX_HT_HANDOFF_GUARD ? 564u : 478u)
#define CLEAN_HT_HANDOFF_GUARD_BYTES 80u
#define CLEAN_AMPDU_BA_CMD 17u
#define CLEAN_AMPDU_BA_WORD (CLEAN_AMPDU_IF_SHM + 80u)
#define CLEAN_AMPDU_BA_WORDS 20u
#define CLEAN_AMPDU_BA_RX_DETAIL 1480u
#define CLEAN_AMPDU_BA_RX_MAGIC 0x31524241u
#define CLEAN_AMPDU_SHM_HELD_STATE 16u
#define CLEAN_AMPDU_SHM_STATUS_WORD 68u
#define CLEAN_AMPDU_SHM_STATUS_WORDS 10u
/* Shared receipt-evidence ABI for both bounded aggregate delivery paths. */
#define CLEAN_AMPDU_DMA_PENDING 0x300u
#define CLEAN_AMPDU_DMA_SUBMITTED 0x301u
#define CLEAN_AMPDU_DMA_DETAIL 704u
#define CLEAN_AMPDU_DMA_PAYLOAD_DETAIL 1664u
#define CLEAN_AMPDU_DMA_MAGIC 0x314d4441u
#define CLEAN_AMPDU_DMA_TAG 0xda16u
#define CLEAN_AMPDU_DMA_STATE_ADDR 0x20b00u
#define CLEAN_AMPDU_DMA_STATE_DETAIL 720u
#define CLEAN_AMPDU_DMA_STATE_GENERATION_DETAIL (CLEAN_AMPDU_BA_RX_DETAIL + 128u)
typedef struct {
    uint32_t indices, status0, status1, descriptor_flags;
    uint32_t magic, descriptor_bytes, descriptor_low, descriptor_high;
    uint32_t ownership, progress, rxe_status, copy_status;
    uint8_t prefix[128];
    uint32_t generation;
} clean_ampdu_dma_state_t;
#define CLEAN_AMPDU_HANDOFF_WORD 68u
#define CLEAN_AMPDU_HANDOFF_WORDS 20u
#define CLEAN_AMPDU_HANDOFF_COMMIT 128u
#define CLEAN_AMPDU_IF_SHM 0x200u
#define CLEAN_AMPDU_IF_RECORDS 16u
#define CLEAN_AMPDU_IF_WORDS (4u + 8u * CLEAN_AMPDU_IF_RECORDS + 6u + 44u)
#define CLEAN_AMPDU_IF_ADDR 0x20800u
#define CLEAN_AMPDU_IF_DETAIL 1104u
#define CLEAN_AMPDU_IF_MAGIC 0x32464941u
_Static_assert(80u + CLEAN_AMPDU_BA_WORDS <= CLEAN_AMPDU_HANDOFF_COMMIT &&
               CLEAN_AMPDU_BA_RX_DETAIL + 40u <= CLEAN_AMPDU_DMA_PAYLOAD_DETAIL,
               "Diagnostic BA evidence must not overlap handoff or payload records");
#define CLEAN_AMPDU_PROBE_SHM 0x100u
#define CLEAN_AMPDU_PROBE_WORDS 144u
#define CLEAN_AMPDU_PROBE_ADDR 0x20600u
#define CLEAN_AMPDU_PROBE_DETAIL 768u
#define CLEAN_AMPDU_PROBE_MAGIC 0x32504441u
_Static_assert(sizeof(clean_ampdu_dma_state_t) == 180u &&
               CLEAN_AMPDU_DMA_STATE_ADDR >= CLEAN_AMPDU_IF_ADDR + 4u + 2u*CLEAN_AMPDU_IF_WORDS &&
               CLEAN_AMPDU_DMA_STATE_ADDR + sizeof(clean_ampdu_dma_state_t) <= 0x20d80u &&
               CLEAN_AMPDU_DMA_STATE_DETAIL + 48u == CLEAN_AMPDU_PROBE_DETAIL,
               "DMA state must fit reserved SRAM and report space");
_Static_assert(CLEAN_AMPDU_DMA_STATE_GENERATION_DETAIL + 4u <= CLEAN_AMPDU_DMA_PAYLOAD_DETAIL,
               "DMA generation must not overlap delivered payloads");
/* SHM addresses are 16-bit word indices, not byte addresses. */
#define CLEAN_SHM_CAPTURE_LIMIT 0x616u
#define CLEAN_SHM_RXE_LIMIT 0x617u
#define CLEAN_SHM_PHY_STATUS0 0x7f6u
#define CLEAN_RX_TRACE_SHM 0x6a0u
#define CLEAN_RX_TRACE_ADDR 0x202c0u
#define CLEAN_RX_TRACE_WORDS 12u
#define CLEAN_HT_EARLY_SHM 0x6b0u
#define CLEAN_HT_EARLY_WORDS 10u
#define CLEAN_TX_TAIL_ADDR (CLEAN_RX_TRACE_ADDR + 2u * CLEAN_RX_TRACE_WORDS)
#define CLEAN_RX_FAULT_WORDS 12u
#define CLEAN_RX_STREAM_PREFIX_BYTES (CLEAN_RX_HT_VARIABLE_STREAM64 ? 64u : \
    CLEAN_RX_HT_COPY_DEFER_STREAM ? 384u : 128u)
#define CLEAN_RX_CONTINUATION_STREAM_BYTES (CLEAN_RX_HT_COPY_STREAM_PHASE == 2 ? \
    128u : CLEAN_RX_HT_COPY_STREAM_PHASE == 3 ? 256u : CLEAN_RX_STREAM_PREFIX_BYTES)
#define CLEAN_RX_LAYOUT_ADDR 0x203c0u
#define CLEAN_RX_LAYOUT_MAGIC 0x3159414cu
#define CLEAN_RX_LAYOUT_WORDS 8u
#define CLEAN_RX_LAYOUT_VALUES {CLEAN_RX_LAYOUT_MAGIC, CLEAN_MPDU_BYTES, CLEAN_SHM_FRAME, \
    CLEAN_D11_RING_ADDR, CLEAN_SDIO_RING_ADDR, CLEAN_TX_BUFFER_ADDR, \
    CLEAN_F2_MAX_FRAGMENTS, CLEAN_D11_BUFFER_BYTES}
#define CLEAN_DETAIL_PACKET_BYTES 528u
_Static_assert(CLEAN_AMPDU_SHM_STATUS_WORD >= 4u + 3u * 8u &&
               CLEAN_AMPDU_SHM_STATUS_WORD + CLEAN_AMPDU_SHM_STATUS_WORDS <=
                   CLEAN_AMPDU_HANDOFF_COMMIT,
               "SHM terminal ownership evidence must fit unused handoff-2 rows");
_Static_assert(CLEAN_AMPDU_DMA_PENDING >= CLEAN_AMPDU_IF_SHM + CLEAN_AMPDU_IF_WORDS &&
               CLEAN_AMPDU_DMA_SUBMITTED < 0x600u,
               "Aggregate DMA controls must not overlap evidence or station SHM");
_Static_assert(!CLEAN_RX_AMPDU_DELIVERY ||
               CLEAN_AMPDU_DMA_PAYLOAD_DETAIL + 3u * 80u <= 2048u,
               "Aggregate delivered-payload evidence must fit the USB report");
_Static_assert(CLEAN_AMPDU_HANDOFF_WORD >= 4u + 3u * 8u &&
               CLEAN_AMPDU_HANDOFF_WORD + 3u * CLEAN_AMPDU_HANDOFF_WORDS <=
                   CLEAN_AMPDU_HANDOFF_COMMIT &&
               CLEAN_AMPDU_HANDOFF_COMMIT + 3u <= 132u,
               "Handoff snapshots must fit unused early-interface rows");
_Static_assert(CLEAN_AMPDU_IF_SHM >= CLEAN_AMPDU_PROBE_SHM + CLEAN_AMPDU_PROBE_WORDS &&
               CLEAN_AMPDU_IF_SHM + CLEAN_AMPDU_IF_WORDS <= 0x600u &&
               CLEAN_AMPDU_IF_ADDR >= CLEAN_AMPDU_PROBE_ADDR + 4u + 2u*CLEAN_AMPDU_PROBE_WORDS &&
               CLEAN_AMPDU_IF_ADDR + 4u + 2u*CLEAN_AMPDU_IF_WORDS <= 0x20d80u &&
               4u + 2u*CLEAN_AMPDU_IF_WORDS <= CLEAN_DETAIL_PACKET_BYTES,
               "Aggregate interface trace must not overlap live state");
_Static_assert(CLEAN_AMPDU_PROBE_SHM + CLEAN_AMPDU_PROBE_WORDS <= 0x600u &&
               CLEAN_AMPDU_PROBE_ADDR + 4u + 2u * CLEAN_AMPDU_PROBE_WORDS <= 0x20d80u &&
               CLEAN_AMPDU_PROBE_DETAIL + 4u + 2u * CLEAN_AMPDU_PROBE_WORDS <= 1088u,
               "Aggregate probe evidence must not overlap live state or packet evidence");
_Static_assert(CLEAN_RX_TRACE_ADDR >= 0x202b8u &&
               CLEAN_TX_TAIL_ADDR + 24u <= 0x20d80u,
               "RX/TX trace must not overlap MAC status or frame storage");
_Static_assert(CLEAN_HT_EARLY_SHM >= CLEAN_RX_TRACE_SHM + CLEAN_RX_TRACE_WORDS &&
               CLEAN_HT_EARLY_SHM + CLEAN_HT_EARLY_WORDS <= 0x7a0u,
               "Early HT trace must not overlap PSM controls or statistics");
_Static_assert(CLEAN_RX_STREAM_PREFIX_BYTES >= CLEAN_PLCP_BYTES + 36u + 8u &&
               CLEAN_RX_STREAM_PREFIX_BYTES < 506u,
               "Stream after the MAC/CCMP prefix and before PHY staging fills");
_Static_assert(CLEAN_RX_FAULT_WORDS <= 16u && 984u + 2u * CLEAN_RX_FAULT_WORDS <= 1024u,
               "RX fault snapshot must fit mailbox polling and MTU trace");

#endif

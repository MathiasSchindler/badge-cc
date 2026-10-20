#ifndef CLEAN_RX_FRAME_LIMITS_H
#define CLEAN_RX_FRAME_LIMITS_H

#define CLEAN_MSDU_BYTES 2304u
#define CLEAN_AMSDU_BYTES 3839u
#define CLEAN_AMSDU_MPDU_BYTES (26u + CLEAN_AMSDU_BYTES + 4u)
#ifndef CLEAN_RX_HT_RX
#define CLEAN_RX_HT_RX 0
#endif
#ifndef CLEAN_RX_HT_HANDOFF_GUARD
#define CLEAN_RX_HT_HANDOFF_GUARD 0
#endif
#ifndef CLEAN_RX_HT_VARIABLE
#define CLEAN_RX_HT_VARIABLE 0
#endif
_Static_assert(CLEAN_RX_HT_VARIABLE == 0 || CLEAN_RX_HT_VARIABLE == 1,
               "Variable handoff diagnostic must be zero or one");
_Static_assert(!CLEAN_RX_HT_VARIABLE || (CLEAN_RX_HT_RX && !CLEAN_RX_HT_HANDOFF_GUARD),
               "Variable handoff requires extended RX without the fixed guard body");
_Static_assert(CLEAN_RX_HT_HANDOFF_GUARD == 0 || CLEAN_RX_HT_HANDOFF_GUARD == 1,
               "Handoff guard gate must be zero or one");
_Static_assert(!CLEAN_RX_HT_HANDOFF_GUARD || CLEAN_RX_HT_RX,
               "Handoff guard requires extended HT receive geometry");
#define CLEAN_HT_HANDOFF_VERSION (CLEAN_RX_HT_VARIABLE ? 9u : CLEAN_RX_HT_HANDOFF_GUARD ? 8u : 7u)
#define CLEAN_HT_HANDOFF_PSDU (CLEAN_RX_HT_HANDOFF_GUARD ? 564u : 478u)
#define CLEAN_HT_HANDOFF_GUARD_BYTES 80u
#ifndef CLEAN_RX_AMPDU_PROBE
#define CLEAN_RX_AMPDU_PROBE 0
#endif
#ifndef CLEAN_RX_AMPDU_INTERFACE
#define CLEAN_RX_AMPDU_INTERFACE 0
#endif
#ifndef CLEAN_RX_AMPDU_HANDOFF
#define CLEAN_RX_AMPDU_HANDOFF 0
#endif
#ifndef CLEAN_RX_AMPDU_BAD_FCS
#define CLEAN_RX_AMPDU_BAD_FCS 0
#endif
#ifndef CLEAN_RX_AMPDU_DMA
#define CLEAN_RX_AMPDU_DMA 0
#endif
#ifndef CLEAN_RX_AMPDU_SHM
#define CLEAN_RX_AMPDU_SHM 0
#endif
#ifndef CLEAN_RX_AMPDU_BA
#define CLEAN_RX_AMPDU_BA 0
#endif
#define CLEAN_AMPDU_BA_CMD 17u
#define CLEAN_AMPDU_BA_WORD (CLEAN_AMPDU_IF_SHM + 80u)
#define CLEAN_AMPDU_BA_WORDS 20u
#define CLEAN_AMPDU_BA_RX_DETAIL 1480u
#define CLEAN_AMPDU_BA_RX_MAGIC 0x31524241u
_Static_assert(CLEAN_RX_AMPDU_BA == 0 ||
               (CLEAN_RX_AMPDU_BA == 1 && CLEAN_RX_AMPDU_SHM),
               "Diagnostic Block ACK requires retained SHM delivery");
#define CLEAN_RX_AMPDU_DELIVERY (CLEAN_RX_AMPDU_DMA || CLEAN_RX_AMPDU_SHM)
#define CLEAN_AMPDU_SHM_HELD_STATE 16u
#define CLEAN_AMPDU_SHM_STATUS_WORD 68u
#define CLEAN_AMPDU_SHM_STATUS_WORDS 10u
/* Shared receipt-evidence ABI for both bounded aggregate delivery paths. */
#define CLEAN_AMPDU_DMA_PENDING 0x300u
#define CLEAN_AMPDU_DMA_SUBMITTED 0x301u
#define CLEAN_AMPDU_DMA_DETAIL 704u
#define CLEAN_AMPDU_DMA_PAYLOAD_DETAIL 1664u
#define CLEAN_AMPDU_DMA_MAGIC 0x314d4441u
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
#if CLEAN_RX_HT_RX
#define CLEAN_MPDU_BYTES 3896u
#define CLEAN_SHM_FRAME 0x800u
#define CLEAN_SHM_FRAME_END 0x1000u
#define CLEAN_D11_RING_ADDR 0x30000u
#define CLEAN_D11_BUFFER_ADDR 0x30200u
#define CLEAN_SDIO_RING_ADDR 0x52000u
#define CLEAN_SDIO_BUFFER_ADDR 0x52200u
#define CLEAN_TX_BUFFER_ADDR 0x64000u
#define CLEAN_TRANSPORT_TRACE_BYTES 8192u
#define CLEAN_TRANSPORT_COMPARE_OFFSET 4200u
#define CLEAN_TRANSPORT_HISTORY_OFFSET 4240u
#define CLEAN_MTU_TX_DIAG_OFFSET 5000u
#define CLEAN_MTU_FIFO_OFFSET 5200u
#define CLEAN_MTU_EROM_OFFSET 5240u
#else
#define CLEAN_MPDU_BYTES 2400u
#define CLEAN_SHM_FRAME 0x100u
#define CLEAN_SHM_FRAME_END 0x600u
#define CLEAN_D11_RING_ADDR 0x26000u
#define CLEAN_D11_BUFFER_ADDR 0x26200u
#define CLEAN_SDIO_RING_ADDR 0x3a000u
#define CLEAN_SDIO_BUFFER_ADDR 0x3a200u
#define CLEAN_TX_BUFFER_ADDR 0x4a400u
#define CLEAN_TRANSPORT_TRACE_BYTES 4096u
#define CLEAN_TRANSPORT_COMPARE_OFFSET 2680u
#define CLEAN_TRANSPORT_HISTORY_OFFSET 2720u
#define CLEAN_MTU_TX_DIAG_OFFSET 3500u
#define CLEAN_MTU_FIFO_OFFSET 3700u
#define CLEAN_MTU_EROM_OFFSET 3740u
#endif
#define CLEAN_PLCP_BYTES 6u
#define CLEAN_RAW_BYTES (CLEAN_MPDU_BYTES + 8u)
#define CLEAN_RXE_BYTES (CLEAN_MPDU_BYTES + CLEAN_PLCP_BYTES)
#define CLEAN_TX_BYTES CLEAN_RXE_BYTES
#define CLEAN_TX_STORAGE_BYTES ((CLEAN_TX_BYTES + 3u) & ~3u)
#define CLEAN_SDPCM_BYTES (CLEAN_MPDU_BYTES + 18u)
#define CLEAN_RX_PACKET_BYTES ((CLEAN_SDPCM_BYTES + 3u) & ~3u)
#define CLEAN_F2_PACKET_BYTES 2044u
#define CLEAN_F2_FRAGMENT_BYTES 1536u
#define CLEAN_F2_FRAGMENT_HEADER 22u
#define CLEAN_F2_MAX_FRAGMENTS ((CLEAN_MPDU_BYTES + CLEAN_F2_FRAGMENT_BYTES - 1u) / CLEAN_F2_FRAGMENT_BYTES)
#define CLEAN_D11_BUFFER_BYTES ((38u + CLEAN_RXE_BYTES + 31u) & ~31u)
#define CLEAN_MTU_MAX_BYTES (CLEAN_MSDU_BYTES + 24u + 4u)
#define CLEAN_MTU_MIN_BYTES (CLEAN_MTU_MAX_BYTES - 64u)
#define CLEAN_MTU_LENGTHS (CLEAN_MTU_MAX_BYTES - CLEAN_MTU_MIN_BYTES + 1u)

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
#ifndef CLEAN_RX_HT_COPY_DEFER_STREAM
#define CLEAN_RX_HT_COPY_DEFER_STREAM 0
#endif
_Static_assert(CLEAN_RX_HT_COPY_DEFER_STREAM == 0 || CLEAN_RX_HT_COPY_DEFER_STREAM == 1,
               "Deferred streaming control must be zero or one");
#ifndef CLEAN_RX_HT_VARIABLE_STREAM64
#define CLEAN_RX_HT_VARIABLE_STREAM64 0
#endif
_Static_assert(CLEAN_RX_HT_VARIABLE_STREAM64 == 0 || CLEAN_RX_HT_VARIABLE_STREAM64 == 1,
               "Variable early streaming probe must be zero or one");
_Static_assert(!CLEAN_RX_HT_VARIABLE_STREAM64 ||
               (CLEAN_RX_HT_VARIABLE && !CLEAN_RX_HT_COPY_DEFER_STREAM),
               "64-byte streaming requires the variable non-deferred fixture");
#define CLEAN_RX_STREAM_PREFIX_BYTES (CLEAN_RX_HT_VARIABLE_STREAM64 ? 64u : \
    CLEAN_RX_HT_COPY_DEFER_STREAM ? 384u : 128u)
#ifndef CLEAN_RX_HT_COPY_STREAM_PHASE
#define CLEAN_RX_HT_COPY_STREAM_PHASE 0
#endif
_Static_assert(CLEAN_RX_HT_COPY_STREAM_PHASE >= 0 && CLEAN_RX_HT_COPY_STREAM_PHASE <= 3,
               "Streaming phase control must be off, matched hold, resume128 or resume256");
#define CLEAN_RX_CONTINUATION_STREAM_BYTES (CLEAN_RX_HT_COPY_STREAM_PHASE == 2 ? \
    128u : CLEAN_RX_HT_COPY_STREAM_PHASE == 3 ? 256u : CLEAN_RX_STREAM_PREFIX_BYTES)

#define CLEAN_SDIO_BUFFER_BYTES 2048u
#define CLEAN_RX_LAYOUT_ADDR 0x203c0u
#define CLEAN_RX_LAYOUT_MAGIC 0x3159414cu
#define CLEAN_RX_LAYOUT_WORDS 8u
#define CLEAN_RX_LAYOUT_VALUES {CLEAN_RX_LAYOUT_MAGIC, CLEAN_MPDU_BYTES, CLEAN_SHM_FRAME, \
    CLEAN_D11_RING_ADDR, CLEAN_SDIO_RING_ADDR, CLEAN_TX_BUFFER_ADDR, \
    CLEAN_F2_MAX_FRAGMENTS, CLEAN_D11_BUFFER_BYTES}

#define CLEAN_DETAIL_PACKET_BYTES 528u

_Static_assert(CLEAN_RX_HT_RX == 0 || CLEAN_RX_HT_RX == 1, "HT RX build gate");
_Static_assert(CLEAN_RX_AMPDU_SHM == 0 ||
               (CLEAN_RX_AMPDU_SHM == 1 && !CLEAN_RX_AMPDU_DMA &&
                CLEAN_RX_AMPDU_PROBE && CLEAN_RX_AMPDU_INTERFACE == 3 &&
                CLEAN_RX_AMPDU_HANDOFF == 2),
               "SHM aggregate delivery requires the finite handoff without DMA publication");
_Static_assert(CLEAN_AMPDU_SHM_STATUS_WORD >= 4u + 3u * 8u &&
               CLEAN_AMPDU_SHM_STATUS_WORD + CLEAN_AMPDU_SHM_STATUS_WORDS <=
                   CLEAN_AMPDU_HANDOFF_COMMIT,
               "SHM terminal ownership evidence must fit unused handoff-2 rows");
_Static_assert(CLEAN_RX_AMPDU_DMA == 0 ||
               (CLEAN_RX_AMPDU_DMA == 1 && CLEAN_RX_AMPDU_PROBE &&
                CLEAN_RX_AMPDU_INTERFACE == 3 && CLEAN_RX_AMPDU_HANDOFF == 2),
               "Aggregate DMA experiment requires the qualified finite handoff");
_Static_assert(CLEAN_AMPDU_DMA_PENDING >= CLEAN_AMPDU_IF_SHM + CLEAN_AMPDU_IF_WORDS &&
               CLEAN_AMPDU_DMA_SUBMITTED < 0x600u,
               "Aggregate DMA controls must not overlap evidence or station SHM");
_Static_assert(!CLEAN_RX_AMPDU_DELIVERY ||
               CLEAN_AMPDU_DMA_PAYLOAD_DETAIL + 3u * 80u <= 2048u,
               "Aggregate delivered-payload evidence must fit the USB report");
_Static_assert(CLEAN_RX_AMPDU_PROBE == 0 ||
               (CLEAN_RX_AMPDU_PROBE == 1 && CLEAN_RX_HT_RX),
               "Aggregate hardware probing requires extended RX");
_Static_assert(CLEAN_RX_AMPDU_INTERFACE == 0 ||
               (CLEAN_RX_AMPDU_INTERFACE >= 1 && CLEAN_RX_AMPDU_INTERFACE <= 3 &&
                CLEAN_RX_AMPDU_PROBE),
               "Aggregate interface tracing requires the finite hardware probe");
_Static_assert(CLEAN_RX_AMPDU_HANDOFF == 0 ||
               (CLEAN_RX_AMPDU_HANDOFF >= 1 && CLEAN_RX_AMPDU_HANDOFF <= 2 &&
                CLEAN_RX_AMPDU_INTERFACE == 3),
               "Handoff snapshots require the isolated early aggregate probe");
_Static_assert(CLEAN_RX_AMPDU_BAD_FCS == 0 ||
               (CLEAN_RX_AMPDU_BAD_FCS >= 1 && CLEAN_RX_AMPDU_BAD_FCS <= 7 &&
                CLEAN_RX_AMPDU_HANDOFF == 2),
               "Bad-FCS injection requires the finite one-copy diagnostic");
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
_Static_assert(CLEAN_SHM_FRAME + CLEAN_RAW_BYTES / 2u <= CLEAN_SHM_FRAME_END &&
               (!CLEAN_RX_HT_RX || CLEAN_SHM_FRAME >= 0x800u),
               "Capture must not overlap PSM controls");
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
_Static_assert(CLEAN_F2_PACKET_BYTES <= 0x7ffu &&
               CLEAN_F2_PACKET_BYTES <= CLEAN_SDIO_BUFFER_BYTES &&
               CLEAN_F2_FRAGMENT_HEADER + CLEAN_F2_FRAGMENT_BYTES <= CLEAN_F2_PACKET_BYTES,
               "Physical F2 packets must fit gSPI and SDIO slots");
_Static_assert(CLEAN_F2_MAX_FRAGMENTS <= 3u, "At most three descriptors per logical frame");
_Static_assert(CLEAN_SDIO_BUFFER_ADDR + 32u * CLEAN_SDIO_BUFFER_BYTES <= CLEAN_TX_BUFFER_ADDR,
               "SDIO slots must not overlap TX staging");
_Static_assert(CLEAN_TX_BUFFER_ADDR + CLEAN_TX_STORAGE_BYTES <= 0x80000u,
               "TX staging must fit controller SRAM");
_Static_assert((CLEAN_D11_RING_ADDR & 8191u) == 0u &&
               (CLEAN_SDIO_RING_ADDR & 8191u) == 0u, "DMA ring alignment");
_Static_assert(256u + CLEAN_RX_PACKET_BYTES <= CLEAN_TRANSPORT_COMPARE_OFFSET &&
               CLEAN_TRANSPORT_COMPARE_OFFSET + 4u <= CLEAN_TRANSPORT_HISTORY_OFFSET &&
               CLEAN_TRANSPORT_HISTORY_OFFSET + 13u * 16u <= CLEAN_TRANSPORT_TRACE_BYTES,
               "Transport evidence regions must not overlap");
_Static_assert(800u + 2u * CLEAN_MTU_LENGTHS <= 944u &&
               1024u + CLEAN_RX_PACKET_BYTES <= CLEAN_MTU_TX_DIAG_OFFSET &&
               CLEAN_MTU_TX_DIAG_OFFSET + 180u <= CLEAN_MTU_FIFO_OFFSET &&
               CLEAN_MTU_FIFO_OFFSET + 32u <= CLEAN_MTU_EROM_OFFSET &&
               CLEAN_MTU_EROM_OFFSET + 65u * 4u <= CLEAN_TRANSPORT_TRACE_BYTES,
               "Full-MSDU diagnostics must fit without overlap");

#endif

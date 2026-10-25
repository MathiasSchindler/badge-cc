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
#ifndef CLEAN_RX_AMPDU_REPEAT
#define CLEAN_RX_AMPDU_REPEAT 0
#endif
#ifndef CLEAN_RX_AMPDU_BA
#define CLEAN_RX_AMPDU_BA 0
#endif
_Static_assert(CLEAN_RX_AMPDU_BA == 0 ||
               (CLEAN_RX_AMPDU_BA == 1 && CLEAN_RX_AMPDU_SHM),
               "Diagnostic Block ACK requires retained SHM delivery");
_Static_assert(CLEAN_RX_AMPDU_REPEAT == 0 ||
               (CLEAN_RX_AMPDU_REPEAT == 1 && CLEAN_RX_AMPDU_SHM && !CLEAN_RX_AMPDU_BA),
               "Repeated aggregate handoff requires retained SHM without diagnostic BA");
#define CLEAN_RX_AMPDU_DELIVERY (CLEAN_RX_AMPDU_DMA || CLEAN_RX_AMPDU_SHM)
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
#ifndef CLEAN_RX_HT_COPY_STREAM_PHASE
#define CLEAN_RX_HT_COPY_STREAM_PHASE 0
#endif
_Static_assert(CLEAN_RX_HT_COPY_STREAM_PHASE >= 0 && CLEAN_RX_HT_COPY_STREAM_PHASE <= 3,
               "Streaming phase control must be off, matched hold, resume128 or resume256");

#define CLEAN_SDIO_BUFFER_BYTES 2048u

_Static_assert(CLEAN_RX_HT_RX == 0 || CLEAN_RX_HT_RX == 1, "HT RX build gate");
_Static_assert(CLEAN_RX_AMPDU_SHM == 0 ||
               (CLEAN_RX_AMPDU_SHM == 1 && !CLEAN_RX_AMPDU_DMA &&
                CLEAN_RX_AMPDU_PROBE && CLEAN_RX_AMPDU_INTERFACE == 3 &&
                CLEAN_RX_AMPDU_HANDOFF == 2),
               "SHM aggregate delivery requires the finite handoff without DMA publication");
_Static_assert(CLEAN_RX_AMPDU_DMA == 0 ||
               (CLEAN_RX_AMPDU_DMA == 1 && CLEAN_RX_AMPDU_PROBE &&
                CLEAN_RX_AMPDU_INTERFACE == 3 && CLEAN_RX_AMPDU_HANDOFF == 2),
               "Aggregate DMA experiment requires the qualified finite handoff");
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
_Static_assert(CLEAN_SHM_FRAME + CLEAN_RAW_BYTES / 2u <= CLEAN_SHM_FRAME_END &&
               (!CLEAN_RX_HT_RX || CLEAN_SHM_FRAME >= 0x800u),
               "Capture must not overlap PSM controls");
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

#ifndef CLEAN_RX_FRAME_LIMITS_H
#define CLEAN_RX_FRAME_LIMITS_H

#define CLEAN_MSDU_BYTES 2304u
#define CLEAN_MPDU_BYTES 2400u
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
#define CLEAN_MTU_MAX_BYTES (CLEAN_MSDU_BYTES + 24u + 4u)
#define CLEAN_MTU_MIN_BYTES (CLEAN_MTU_MAX_BYTES - 64u)
#define CLEAN_MTU_LENGTHS (CLEAN_MTU_MAX_BYTES - CLEAN_MTU_MIN_BYTES + 1u)

/* SHM addresses are 16-bit word indices, not byte addresses. */
#define CLEAN_SHM_FRAME 0x100u
#define CLEAN_SHM_CAPTURE_LIMIT 0x616u
#define CLEAN_SHM_RXE_LIMIT 0x617u
#define CLEAN_RX_FAULT_WORDS 12u
#define CLEAN_RX_STREAM_PREFIX_BYTES 128u

#define CLEAN_D11_RING_ADDR 0x26000u
#define CLEAN_D11_BUFFER_ADDR 0x26200u
#define CLEAN_SDIO_RING_ADDR 0x3a000u
#define CLEAN_SDIO_BUFFER_ADDR 0x3a200u
#define CLEAN_SDIO_BUFFER_BYTES 2048u
#define CLEAN_TX_BUFFER_ADDR 0x4a400u

#define CLEAN_DETAIL_PACKET_BYTES 528u
#define CLEAN_TRANSPORT_TRACE_BYTES 4096u
#define CLEAN_TRANSPORT_COMPARE_OFFSET 2680u
#define CLEAN_TRANSPORT_HISTORY_OFFSET 2720u
#define CLEAN_MTU_TX_DIAG_OFFSET 3500u
#define CLEAN_MTU_FIFO_OFFSET 3700u
#define CLEAN_MTU_EROM_OFFSET 3740u

_Static_assert(CLEAN_SHM_FRAME + CLEAN_RAW_BYTES / 2u <= 0x600u,
               "Capture must not overlap PSM controls");
_Static_assert(CLEAN_RX_STREAM_PREFIX_BYTES >= CLEAN_PLCP_BYTES + 36u + 8u &&
               CLEAN_RX_STREAM_PREFIX_BYTES < 506u,
               "Stream after the MAC/CCMP prefix and before PHY staging fills");
_Static_assert(CLEAN_RX_FAULT_WORDS <= 16u && 984u + 2u * CLEAN_RX_FAULT_WORDS <= 1024u,
               "RX fault snapshot must fit mailbox polling and MTU trace");
_Static_assert(CLEAN_F2_PACKET_BYTES <= 0x7ffu &&
               CLEAN_F2_PACKET_BYTES <= CLEAN_SDIO_BUFFER_BYTES &&
               CLEAN_F2_FRAGMENT_HEADER + CLEAN_F2_FRAGMENT_BYTES <= CLEAN_F2_PACKET_BYTES,
               "Physical F2 packets must fit gSPI and SDIO slots");
_Static_assert(CLEAN_MPDU_BYTES <= 2u * CLEAN_F2_FRAGMENT_BYTES,
               "At most two descriptors per logical frame");
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

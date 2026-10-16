#ifndef CLEAN_RX_FRAME_LIMITS_H
#define CLEAN_RX_FRAME_LIMITS_H

#define CLEAN_MPDU_BYTES 1600u
#define CLEAN_PLCP_BYTES 6u
#define CLEAN_RAW_BYTES (CLEAN_MPDU_BYTES + 8u)
#define CLEAN_RXE_BYTES (CLEAN_MPDU_BYTES + CLEAN_PLCP_BYTES)
#define CLEAN_TX_BYTES CLEAN_RXE_BYTES
#define CLEAN_TX_STORAGE_BYTES ((CLEAN_TX_BYTES + 3u) & ~3u)
#define CLEAN_SDPCM_BYTES (CLEAN_MPDU_BYTES + 18u)
#define CLEAN_RX_PACKET_BYTES ((CLEAN_SDPCM_BYTES + 3u) & ~3u)

/* SHM addresses are 16-bit word indices, not byte addresses. */
#define CLEAN_SHM_FRAME 0x200u
#define CLEAN_SHM_CAPTURE_LIMIT 0x616u
#define CLEAN_SHM_RXE_LIMIT 0x617u

#define CLEAN_D11_RING_ADDR 0x26000u
#define CLEAN_D11_BUFFER_ADDR 0x26200u
#define CLEAN_SDIO_RING_ADDR 0x34000u
#define CLEAN_SDIO_BUFFER_ADDR 0x34200u
#define CLEAN_SDIO_BUFFER_BYTES 2048u
#define CLEAN_TX_BUFFER_ADDR 0x44400u

#define CLEAN_DETAIL_PACKET_BYTES 528u
#define CLEAN_TRANSPORT_TRACE_BYTES 4096u
#define CLEAN_TRANSPORT_HISTORY_OFFSET 2048u

_Static_assert(CLEAN_SHM_FRAME + CLEAN_RAW_BYTES / 2u <= 0x600u,
               "Capture must not overlap PSM controls");
_Static_assert(CLEAN_RX_PACKET_BYTES <= 0x7ffu, "gSPI 11-bit transfer size");
_Static_assert(CLEAN_RX_PACKET_BYTES <= CLEAN_SDIO_BUFFER_BYTES, "SDIO slot size");
_Static_assert(CLEAN_SDIO_BUFFER_ADDR + 32u * CLEAN_SDIO_BUFFER_BYTES <= CLEAN_TX_BUFFER_ADDR,
               "SDIO slots must not overlap TX staging");
_Static_assert((CLEAN_D11_RING_ADDR & 8191u) == 0u &&
               (CLEAN_SDIO_RING_ADDR & 8191u) == 0u, "DMA ring alignment");
_Static_assert(256u + CLEAN_RX_PACKET_BYTES <= CLEAN_TRANSPORT_HISTORY_OFFSET &&
               CLEAN_TRANSPORT_HISTORY_OFFSET + 13u * 16u <= CLEAN_TRANSPORT_TRACE_BYTES,
               "Transport evidence regions must not overlap");

#endif

#ifndef MICRO_SIGNAL_MTU_H
#define MICRO_SIGNAL_MTU_H

#include "micro-signal.h"
#include "../../clean-rx/sdio_test.h"

/* Broadcast Data with 2240..2304-byte MSDUs, including LLC/SNAP, plus header/FCS. */
static inline uint32_t micro_mtu_length(const micro_signal_event_t *event) {
    return CLEAN_MTU_MIN_BYTES + (event->sequence - 1u) % CLEAN_MTU_LENGTHS;
}

static inline uint32_t micro_mtu_frame(uint8_t frame[CLEAN_TX_STORAGE_BYTES],
                                      const micro_signal_event_t *event) {
    uint8_t inner[96];
    micro_signal_frame(inner, event);
    uint32_t length = micro_mtu_length(event);
    badger_bytes_zero(frame, CLEAN_TX_STORAGE_BYTES);
    clean_cck11_plcp(frame, length);
    uint8_t *mpdu = frame + 6u;
    badger_bytes_copy(mpdu, inner + 6u, 24u);
    mpdu[0] = 0x08u;
    static const uint8_t snap[8] = {0xaau,0xaau,3u,0u,0u,0u,0x88u,0xb5u};
    badger_bytes_copy(mpdu + 24u, snap, sizeof(snap));
    badger_bytes_copy(mpdu + 32u, inner + 6u, 88u);
    for (uint32_t i = 120u; i < length - 4u; ++i)
        mpdu[i] = clean_sdio_test_byte(event->sequence ^ event->session, i);
    badger_bytes_store_u32(mpdu + length - 4u, diag_crc32(mpdu, length - 4u));
    return length + CLEAN_PLCP_BYTES;
}

static inline int micro_mtu_decode(const uint8_t *mpdu, uint32_t length,
                                   micro_signal_event_t *event) {
    if (length < CLEAN_MTU_MIN_BYTES || !clean_frame_valid(mpdu, length)) return 0;
    micro_signal_event_t candidate;
    if (!micro_signal_decode(mpdu + 32u, 88u, &candidate) ||
        micro_mtu_length(&candidate) != length) return 0;
    uint8_t expected[CLEAN_TX_STORAGE_BYTES];
    (void)micro_mtu_frame(expected, &candidate);
    if (!badger_bytes_equal(mpdu, expected + CLEAN_PLCP_BYTES, length)) return 0;
    *event = candidate;
    return 1;
}

#endif

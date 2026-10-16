#ifndef MICRO_SIGNAL_OFDM_H
#define MICRO_SIGNAL_OFDM_H

#include "micro-signal-mtu.h"

#define MICRO_OFDM_LENGTHS 6u
#define MICRO_OFDM_CASES (MICRO_OFDM_LENGTHS * CLEAN_OFDM_RATES)
#define MICRO_OFDM_TEST_MAGIC 0x4754534du

static const uint16_t micro_ofdm_lengths[MICRO_OFDM_LENGTHS] = {
    88u, 128u, 1600u, 2026u, 2027u, CLEAN_MTU_MAX_BYTES
};

static inline uint32_t micro_ofdm_case(const micro_signal_event_t *event) {
    return (event->sequence - 1u) % MICRO_OFDM_CASES;
}

static inline uint32_t micro_ofdm_frame(uint8_t frame[CLEAN_TX_STORAGE_BYTES],
                                       const micro_signal_event_t *event) {
    uint32_t index = micro_ofdm_case(event);
    uint32_t length = micro_ofdm_lengths[index / CLEAN_OFDM_RATES];
    if (length == 88u) {
        badger_bytes_zero(frame, CLEAN_TX_STORAGE_BYTES);
        micro_signal_frame(frame, event);
    } else if (micro_data_frame(frame, event, length) == 0u) return 0u;
    if (!clean_ofdm_plcp_rate(frame, length, index % CLEAN_OFDM_RATES)) return 0u;
    return length + CLEAN_PLCP_BYTES;
}

static inline int micro_ofdm_decode(const uint8_t *mpdu, uint32_t length,
                                    uint8_t rate, uint8_t metadata,
                                    micro_signal_event_t *event) {
    micro_signal_event_t candidate;
    if (!(metadata & 0x10u) ||
        !(length == 88u ? micro_signal_decode(mpdu, length, &candidate) :
                         micro_data_decode(mpdu, length, &candidate))) return 0;
    uint32_t index = micro_ofdm_case(&candidate);
    if (length != micro_ofdm_lengths[index / CLEAN_OFDM_RATES] ||
        clean_ofdm_rate_index(rate) != (int)(index % CLEAN_OFDM_RATES)) return 0;
    *event = candidate;
    return 1;
}

#endif

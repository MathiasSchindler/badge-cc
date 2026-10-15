#ifndef MICRO_SIGNAL_H
#define MICRO_SIGNAL_H

#include "../../clean-rx/badge/frames.h"

#define MICRO_SIGNAL_CHANNEL 6u
#define MICRO_SIGNAL_FRAME_BYTES 94u

typedef struct {
    uint8_t sender[8];
    uint32_t session;
    uint32_t sequence;
    uint8_t button;
} micro_signal_event_t;

typedef struct {
    micro_signal_event_t events[4];
    uint8_t used;
    uint8_t replace;
} micro_signal_peers_t;

static inline void micro_signal_frame(uint8_t frame[96], const micro_signal_event_t *event) {
    badger_bytes_zero(frame, 96u);
    clean_cck_plcp(frame, 0x6eu, 64u);
    frame[6] = 0xd0u;
    for (uint32_t i = 0u; i < 6u; ++i) frame[10u + i] = 255u;
    frame[16] = 2u;
    badger_bytes_copy(frame + 17u, event->sender, 5u);
    static const uint8_t network[6] = {2u,'M','S','I','G',1u};
    badger_bytes_copy(frame + 22u, network, sizeof(network));
    frame[28] = (uint8_t)(event->sequence << 4u);
    frame[29] = (uint8_t)(event->sequence >> 4u);
    frame[30] = 127u;
    frame[31] = 2u;
    static const uint8_t magic[4] = {'M','S','I','G'};
    badger_bytes_copy(frame + 34u, magic, sizeof(magic));
    frame[38] = 1u;
    frame[39] = event->button;
    badger_bytes_store_u32(frame + 40u, event->session);
    badger_bytes_store_u32(frame + 44u, event->sequence);
    badger_bytes_copy(frame + 48u, event->sender, 8u);
    static const uint8_t message[] = "BUTTON 1 PRESSED";
    badger_bytes_copy(frame + 56u, message, sizeof(message) - 1u);
    frame[63] = (uint8_t)('0' + event->button);
    badger_bytes_store_u32(frame + 90u, diag_crc32(frame + 6u, 84u));
}

static inline int micro_signal_decode(const uint8_t *mpdu, uint32_t length,
                                      micro_signal_event_t *event) {
    if (length != 88u || !clean_frame_valid(mpdu, length) ||
        mpdu[33] < 1u || mpdu[33] > 5u) return 0;
    micro_signal_event_t candidate;
    candidate.button = mpdu[33];
    candidate.session = badger_bytes_load_u32(mpdu + 34u);
    candidate.sequence = badger_bytes_load_u32(mpdu + 38u);
    badger_bytes_copy(candidate.sender, mpdu + 42u, 8u);
    uint8_t expected[96];
    micro_signal_frame(expected, &candidate);
    if (!badger_bytes_equal(mpdu, expected + 6u, 88u)) return 0;
    *event = candidate;
    return 1;
}

static inline int micro_signal_accept(micro_signal_peers_t *peers,
                                      const micro_signal_event_t *event,
                                      const uint8_t self[8]) {
    if (badger_bytes_equal(event->sender, self, 8u)) return 0;
    uint32_t index = 0u;
    for (; index < peers->used; ++index) {
        micro_signal_event_t *previous = &peers->events[index];
        if (!badger_bytes_equal(event->sender, previous->sender, 8u)) continue;
        uint32_t delta = event->sequence - previous->sequence;
        if (event->session == previous->session && (delta == 0u || delta >= 0x80000000u))
            return 0;
        break;
    }
    if (index == peers->used) {
        if (peers->used < 4u) ++peers->used;
        else {
            index = peers->replace;
            peers->replace = (uint8_t)((peers->replace + 1u) & 3u);
        }
    }
    peers->events[index] = *event;
    return 1;
}

#endif

#ifndef CLEAN_RX_BLOCK_ACK_H
#define CLEAN_RX_BLOCK_ACK_H

#include "mac.h"

#define CLEAN_BA_TIDS 16u
#define CLEAN_BA_WINDOW 8u
#define CLEAN_BA_SLOTS 16u
#define CLEAN_BA_GAP_US 100000u
#define CLEAN_AMPDU_BYTES 8191u

enum {
    CLEAN_BA_INVALID = -1, CLEAN_BA_NO_AGREEMENT = -2,
    CLEAN_BA_SINK_ERROR = -3, CLEAN_BA_BUSY = -4,
    CLEAN_BA_OLD = -5, CLEAN_BA_CONFLICT = -6,
    CLEAN_BA_IGNORED = 0, CLEAN_BA_ACCEPTED = 1,
    CLEAN_BA_DUPLICATE = 2, CLEAN_BA_PREPARED = 3
};

typedef struct {
    uint32_t last_rx, timeout_us;
    uint16_t head, bitmap_start, pending_start, timeout_tu;
    uint8_t bitmap[8], active, pending, window, dialog, amsdu, closing;
} clean_ba_tid_t;

typedef struct {
    uint32_t received_us;
    uint16_t sequence, length;
    uint8_t tid, used;
    uint8_t mpdu[CLEAN_MPDU_BYTES];
} clean_ba_slot_t;

typedef struct {
    clean_ba_tid_t tid[CLEAN_BA_TIDS];
    clean_ba_slot_t slot[CLEAN_BA_SLOTS];
    uint8_t own[6], peer[6];
    uint32_t accepted, duplicates, old, pressure, delivered, skipped, expired;
} clean_ba_rx_t;

/* 1 consumes the immutable MPDU, 0 applies backpressure, -1 aborts processing.
 * The sink must not reenter or reset this BA state while it is running. */
typedef int (*clean_ba_sink_t)(void *, const uint8_t *, uint32_t, uint32_t);

void clean_ba_init(clean_ba_rx_t *s, const uint8_t own[6], const uint8_t peer[6]);
/* Request parsing only prepares an agreement. No positive reply or active
 * agreement until the caller installs the controller/PSM response state. */
int clean_ba_action(clean_ba_rx_t *s, const uint8_t *mpdu, uint32_t length);
int clean_ba_activate(clean_ba_rx_t *s, uint32_t tid, uint32_t now);
uint32_t clean_ba_addba_response(const clean_ba_rx_t *s, uint32_t tid,
                                uint16_t sequence, uint8_t response[37]);
/* Explicit session reset discards and wipes this TID's queued ciphertext.
 * Received DELBA instead uses poll to flush accepted MPDUs under backpressure. */
int clean_ba_close(clean_ba_rx_t *s, uint32_t tid);
int clean_ba_receive(clean_ba_rx_t *s, const uint8_t *mpdu, uint32_t length,
                      uint32_t now, clean_ba_sink_t sink, void *context);
int clean_ba_poll(clean_ba_rx_t *s, uint32_t now, clean_ba_sink_t sink, void *context);
int clean_ba_bar(clean_ba_rx_t *s, const uint8_t *mpdu, uint32_t length,
                  uint32_t now, clean_ba_sink_t sink, void *context);
/* Serializes a compressed BA; does not schedule or transmit it at SIFS. */
uint32_t clean_ba_response(const clean_ba_rx_t *s, uint32_t tid, uint8_t response[32]);

typedef struct {
    uint32_t expected, consumed, frames, bad_fcs, null_delimiters;
    uint16_t length, have;
    uint8_t delimiter[4], delimiter_bytes, padding, failed;
    uint8_t mpdu[CLEAN_MPDU_BYTES];
} clean_ampdu_stream_t;

uint8_t clean_ampdu_delimiter_crc(const uint8_t bytes[2]);
int clean_ampdu_begin(clean_ampdu_stream_t *s, uint32_t bytes);
/* Returns ACCEPTED, BUSY or an explicit error. *used always identifies the
 * input consumed; retry the remainder after BUSY, including a zero-byte tail.
 * FCS failures are counted and skipped; corrupt delimiters abort the stream. */
int clean_ampdu_feed(clean_ampdu_stream_t *s, const uint8_t *bytes, uint32_t length,
                      uint32_t *used, clean_ba_sink_t sink, void *context, uint32_t now);
int clean_ampdu_end(const clean_ampdu_stream_t *s);

#endif

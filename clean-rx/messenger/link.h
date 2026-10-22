#ifndef CLEAN_MESSENGER_LINK_H
#define CLEAN_MESSENGER_LINK_H

#include "../src/ht20.h"
#include "../../src/shared/bytes.h"
#include "../../src/tools/diagnostics_logic.h"

#define MSG_MPDU 80u
#define MSG_STORAGE 96u
#define MSG_CHANNEL 1u
#define MSG_MCS 0u
#define MSG_QUEUE 8u
#define MSG_ATTEMPTS 6u
#define MSG_STATE 1u
#define MSG_ACK 2u
#define MSG_HEARTBEAT 0u
#define MSG_BUTTON 1u
#define MSG_USB 2u
#define MSG_TRACE_MAGIC 0x3147534du
#ifndef MSG_BASELINE
#define MSG_BASELINE 0
#endif
_Static_assert(MSG_BASELINE==0 || MSG_BASELINE==1,"Baseline selector must be zero or one");
#define MSG_ACK_DELAY_US (MSG_BASELINE ? 20000u : 1000u)
#define MSG_RETRY_BASE (MSG_BASELINE ? 400000u : 200000u)
#define MSG_RETRY_ROLE (MSG_BASELINE ? 70000u : 7000u)
#define MSG_RETRY_SEQUENCE (MSG_BASELINE ? 23000u : 2300u)

typedef struct {
    uint32_t token, sequence, detected_at, queued_at, first_tx_at, last_tx_at;
    uint32_t first_done_at, last_done_at, completed_at, attempts, outcome, flags;
} msg_tx_time_t;

typedef struct {
    uint32_t token, session, sequence, received_at, ack_selected_at, ack_done_at;
    uint32_t displayed_at, flags, duplicates;
} msg_rx_time_t;

typedef struct {
    uint32_t session, sequence, target, token;
    uint8_t role, kind, buttons, origin;
} msg_frame_t;

typedef struct {
    uint32_t token, detected_at, queued_at;
    uint8_t buttons, origin;
} msg_event_t;

typedef struct {
    uint32_t session, sequence, retry_at, ack_at, peer_at;
    uint32_t tx_data, tx_ack, rx_data, rx_ack, duplicates, failed, dropped;
    uint32_t peer_session, peer_sequence, peer_presses;
    msg_frame_t pending, reply, last_event, last_acked;
    msg_event_t queue[MSG_QUEUE];
    msg_tx_time_t timing, last_timing;
    msg_rx_time_t receipt;
    diag_buttons_t debounce;
    uint8_t role, head, count, active, attempts, ack_pending;
    uint8_t buttons, peer_buttons, last_press;
} msg_link_t;

int msg_encode(uint8_t frame[MSG_MPDU], const msg_frame_t *message);
int msg_decode(msg_frame_t *message, const uint8_t *frame, uint32_t size);
int msg_template(uint8_t packet[MSG_STORAGE], const uint8_t frame[MSG_MPDU]);
void msg_init(msg_link_t *link, uint8_t role, uint32_t session);
int msg_enqueue(msg_link_t *link, uint8_t buttons, uint8_t origin, uint32_t token,
                uint32_t now);
void msg_buttons(msg_link_t *link, uint8_t raw, uint32_t now);
int msg_next(msg_link_t *link, uint32_t now, uint8_t frame[MSG_MPDU]);
int msg_receive(msg_link_t *link, const uint8_t *frame, uint32_t size, uint32_t now);
void msg_transmitted(msg_link_t *link, const uint8_t frame[MSG_MPDU], uint32_t now);
void msg_displayed(msg_link_t *link, uint32_t now);

#endif

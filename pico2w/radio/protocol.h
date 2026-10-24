#ifndef PICO2W_RADIO_PROTOCOL_H
#define PICO2W_RADIO_PROTOCOL_H

#include "../../clean-rx/badge/frames.h"

#define PR_CHANNEL 6u
#define PR_MPDU 80u
#define PR_STORAGE 88u
#define PR_DATA 14u
#define PR_PACKET (18u + PR_MPDU)
#define PR_TRACE_MAGIC 0x31523250u
#define PR_MAX_ATTEMPTS 6u
#define PR_REQUEST 1u
#define PR_REPLY 2u
#define PR_TIMING_MAGIC 0x314d3852u
#define PR_TIMING_DETAIL 384u

typedef struct {
    uint8_t kind, source, destination, sender[8], data[PR_DATA];
    uint32_t session, sequence;
} pr_message_t;

typedef struct {
    uint8_t node, id[8], active, attempts, reply_pending;
    uint32_t session, sequence, retry_at, reply_at;
    uint32_t confirmed, failed, requests, replies, duplicates, reply_drops;
    uint32_t began_at, first_tx_at, completed_at;
    uint32_t seen_session[4], seen_sequence[4];
    pr_message_t pending, reply;
} pr_link_t;

int pr_encode(uint8_t mpdu[PR_MPDU], const pr_message_t *message);
int pr_decode(pr_message_t *message, const uint8_t *mpdu, uint32_t size);
int pr_metadata(const uint8_t packet[PR_PACKET]);
void pr_init(pr_link_t *link, uint8_t node, const uint8_t id[8], uint32_t session);
int pr_begin(pr_link_t *link, uint8_t destination, const uint8_t data[PR_DATA], uint32_t now);
/* 1: new request; 2: matching reply; 3: duplicate request; 0: rejected. */
int pr_receive(pr_link_t *link, const uint8_t *mpdu, uint32_t size, uint32_t now);
int pr_next(pr_link_t *link, uint32_t now, pr_message_t *message);

#endif

#ifndef CLEAN_RX_MAC_SOAK_H
#define CLEAN_RX_MAC_SOAK_H

#include "mac.h"
#include "phy_rates.h"

#define CLEAN_SOAK_MAGIC 0x314b534du
#define CLEAN_SOAK_STATUS_MAGIC 0x334b534du
#define CLEAN_SOAK_DETAIL 520u
#define CLEAN_SOAK_MAX_FRAMES 100000u
#define CLEAN_SOAK_MAX_US 7200000000ULL
#define CLEAN_SOAK_HOST_SECONDS 7230u
#define CLEAN_SOAK_POLL_US 250000u
#define CLEAN_SOAK_POLLS (CLEAN_SOAK_HOST_SECONDS * 4u)
#define CLEAN_SOAK_PHYS 8u
#define CLEAN_SOAK_PAYLOADS 7u
#define CLEAN_SOAK_FRAGMENT_DETAIL (CLEAN_SOAK_DETAIL + 64u)
#define CLEAN_SOAK_FRAGMENT_MAGIC 0x3147534du
#define CLEAN_SOAK_PROGRESS_DETAIL 720u
#define CLEAN_SOAK_ACK_DETAIL 800u
#define CLEAN_SOAK_PROGRESS_MAGIC 0x3150534du
#define CLEAN_SOAK_NO_RESULT 0xffffffffu
#define CLEAN_SOAK_RETRY_US 2000000u
#define CLEAN_SOAK_CLOSE_US (CLEAN_SOAK_RETRY_US + 100000u)
#define CLEAN_SOAK_SNAPSHOT_FROZEN 1u
#define CLEAN_SOAK_SNAPSHOT_MAILBOX 2u
#define CLEAN_SOAK_SNAPSHOT_FULL 4u
#define CLEAN_SOAK_FAULT_DETAIL 1920u

enum {
    CLEAN_SOAK_SETUP = 1u, CLEAN_SOAK_READY, CLEAN_SOAK_SEND,
    CLEAN_SOAK_RECEIVE, CLEAN_SOAK_FINISHED, CLEAN_SOAK_CLOSING
};

typedef struct {
    uint32_t magic, ready_us, first_send_us, first_result_us, first_rx_us;
    uint32_t phase, index, fragment, attempts, result;
    uint32_t ref_before, tick_cycles_before, ref_after, tick_cycles_after, sys_khz_q5;
    uint32_t snapshot_flags, snapshot_stop_error, phy_start_handoffs, completion_handoffs;
    uint32_t closing_duplicates;
} clean_soak_progress_t;

_Static_assert(sizeof(clean_soak_progress_t) == 80u &&
               CLEAN_SOAK_PROGRESS_DETAIL + sizeof(clean_soak_progress_t) <= CLEAN_SOAK_ACK_DETAIL &&
               CLEAN_SOAK_ACK_DETAIL + 32u <= 1000u,
               "Soak progress and ACK bytes must fit before transmit status");

_Static_assert(CLEAN_SOAK_FAULT_DETAIL + 2u * CLEAN_RX_FAULT_WORDS <= 1960u,
               "RX fault report must not overlap fixture inputs");

static const char *const clean_soak_phy_names[CLEAN_SOAK_PHYS] = {
    "ofdm6", "1-long", "2-long", "5.5-long", "11-long",
    "2-short", "5.5-short", "11-short"
};

static const char *const clean_soak_payload_names[CLEAN_SOAK_PAYLOADS] = {
    "cycle", "32", "1500", "2304", "cck-boundary", "frag2", "frag16"
};

typedef struct {
    uint32_t magic, fragments_per_msdu, tx_mpdu_acked, rx_mpdu_checked;
    uint32_t reassembled, last_index, fragment_bitmap, retry_bitmap;
    uint32_t fragment_fcs[16], reassembled_fcs;
} clean_soak_fragment_status_t;

_Static_assert(sizeof(clean_soak_fragment_status_t) == 100u &&
               CLEAN_SOAK_FRAGMENT_DETAIL + sizeof(clean_soak_fragment_status_t) <=
                   CLEAN_SOAK_PROGRESS_DETAIL,
               "Fragment evidence must fit before other diagnostic records");

static inline uint32_t clean_soak_fragments(const uint8_t nonce[14]) {
    uint32_t profile = nonce[13] >> 3u;
    return profile == 5u ? 2u : profile == 6u ? 16u : 1u;
}

static inline uint32_t clean_soak_phy(const uint8_t nonce[14]) {
    return nonce[13] & 7u;
}

static inline uint32_t clean_soak_rate(const uint8_t nonce[14]) {
    uint32_t phy = clean_soak_phy(nonce);
    return phy == 0u ? 4u : phy <= 4u ? phy - 1u : phy - 4u;
}

static inline uint32_t clean_soak_short(const uint8_t nonce[14]) {
    return clean_soak_phy(nonce) >= 5u;
}

typedef struct {
    uint32_t magic, target, tx_acked, tx_attempts, tx_timeouts, deferrals;
    uint32_t received, duplicates, corrupt, out_of_order, elapsed_us, failure;
    uint32_t elapsed_hi;
    uint32_t rx_phy_checked, rx_phy_mismatch, rx_phy_last;
} clean_soak_status_t;

_Static_assert(sizeof(clean_soak_status_t) == 64u, "MSK3 status wire layout");

static inline int clean_soak_close_pending(uint32_t started, uint32_t now) {
    return now - started < CLEAN_SOAK_CLOSE_US;
}

static inline uint64_t clean_soak_elapsed(const clean_soak_status_t *s) {
    return ((uint64_t)s->elapsed_hi << 32u) | s->elapsed_us;
}

static inline uint32_t clean_soak_size(uint32_t index) {
    return index % 3u == 0u ? 32u : index % 3u == 1u ? 1500u : CLEAN_MSDU_BYTES;
}

static inline uint32_t clean_soak_payload_size(const uint8_t nonce[14], uint32_t index) {
    uint32_t payload = nonce[13] >> 3u;
    if (payload >= CLEAN_SOAK_PAYLOADS) return 0u;
    if (payload == 4u) {
        static const uint16_t sizes[10] = {
            1500u, 1550u, 1600u, 1650u, 1700u, 1750u, 1800u, 1850u, 1900u, 2304u
        };
        return sizes[index % 10u];
    }
    return payload == 0u ? clean_soak_size(index) :
        payload == 1u ? 32u : payload == 2u ? 1500u : CLEAN_MSDU_BYTES;
}

static inline void clean_soak_address(uint8_t address[6], const uint8_t nonce[14],
                                      uint8_t role) {
    address[0] = 2u;
    for (uint32_t i = 0u; i < 4u; ++i) address[i + 1u] = nonce[i];
    address[5] = role;
}

static inline uint8_t clean_soak_pattern(const uint8_t nonce[14], uint32_t index,
                                         uint32_t at) {
    return (uint8_t)(nonce[at % 14u] ^ (index >> (8u * (at & 3u))) ^ (at * 37u));
}

static inline uint32_t clean_soak_frame(uint8_t *frame, const uint8_t nonce[14],
                                        uint8_t role, uint32_t index) {
    uint8_t own[6], peer[6];
    clean_soak_address(own, nonce, role);
    clean_soak_address(peer, nonce, (uint8_t)(3u - role));
    uint32_t payload = clean_soak_payload_size(nonce, index), length = payload + 28u;
    if (nonce[13] >= CLEAN_SOAK_PHYS * CLEAN_SOAK_PAYLOADS) return 0u;
    uint32_t rate = clean_soak_rate(nonce);
    if (!(rate == 4u ? clean_ofdm_plcp_rate(frame, length, 0u) :
          clean_cck_plcp_rate(frame, length, rate))) return 0u;
    uint8_t *m = frame + 6u;
    for (uint32_t i = 0u; i < 24u; ++i) m[i] = 0u;
    m[0] = 8u; m[1] = 1u;
    uint32_t duration = 10u + (rate == 4u ? clean_ofdm_airtime_us(14u, 0u) :
                              clean_cck_airtime_us(14u, rate, clean_soak_short(nonce)));
    m[2] = (uint8_t)duration; m[3] = (uint8_t)(duration >> 8u);
    for (uint32_t i = 0u; i < 6u; ++i) {
        m[4u + i] = m[16u + i] = peer[i];
        m[10u + i] = own[i];
    }
    uint16_t sequence = (uint16_t)(index << 4u);
    m[22] = (uint8_t)sequence; m[23] = (uint8_t)(sequence >> 8u);
    for (uint32_t i = 0u; i < payload; ++i)
        m[24u + i] = clean_soak_pattern(nonce, index, i);
    for (uint32_t i = 0u; i < 4u; ++i) {
        m[24u + i] = (uint8_t)(CLEAN_SOAK_MAGIC >> (8u * i));
        m[28u + i] = (uint8_t)(index >> (8u * i));
    }
    uint32_t fcs = clean_mac_fcs(m, length - 4u);
    for (uint32_t i = 0u; i < 4u; ++i) m[length - 4u + i] = (uint8_t)(fcs >> (8u * i));
    return length + 6u;
}

/* Each fragment is a complete MPDU/PPDU, not an F2 transport fragment.
 * Reserve the next fragment plus its ACK when More Fragments is set. */
static inline uint32_t clean_soak_fragment_frame(uint8_t *frame, const uint8_t nonce[14],
        uint8_t role, uint32_t index, uint32_t fragment) {
    uint32_t count = clean_soak_fragments(nonce);
    if (fragment >= count) return 0u;
    uint32_t length = clean_soak_frame(frame, nonce, role, index);
    if (!length || count == 1u) return length;
    uint32_t payload = CLEAN_MSDU_BYTES / count, mpdu = payload + 28u;
    uint8_t *m = frame + 6u;
    for (uint32_t i = 0u; i < payload; ++i)
        m[24u + i] = m[24u + fragment * payload + i];
    m[22] |= (uint8_t)fragment;
    uint32_t rate = clean_soak_rate(nonce), short_plcp = clean_soak_short(nonce);
    uint32_t ack_us = rate == 4u ? clean_ofdm_airtime_us(14u, 0u) :
        clean_cck_airtime_us(14u, rate, short_plcp);
    uint32_t duration = 10u + ack_us;
    if (fragment + 1u < count) {
        m[1] |= 4u;
        duration += 20u + ack_us + (rate == 4u ? clean_ofdm_airtime_us(mpdu, 0u) :
            clean_cck_airtime_us(mpdu, rate, short_plcp));
    }
    m[2] = (uint8_t)duration;
    m[3] = (uint8_t)(duration >> 8u);
    if (!(rate == 4u ? clean_ofdm_plcp_rate(frame, mpdu, 0u) :
          clean_cck_plcp_rate(frame, mpdu, rate))) return 0u;
    uint32_t fcs = clean_mac_fcs(m, mpdu - 4u);
    for (uint32_t i = 0u; i < 4u; ++i) m[mpdu - 4u + i] = (uint8_t)(fcs >> (8u * i));
    return mpdu + 6u;
}

/* Bound-peer data reaching this function has already passed the actual MAC
 * duplicate/reassembly adapter. A repeated application index is a failure. */
static inline int clean_soak_receive(clean_soak_status_t *s, const uint8_t *m,
                                     uint32_t length, const uint8_t nonce[14],
                                     uint8_t role) {
    uint8_t own[6], peer[6];
    clean_soak_address(own, nonce, role);
    clean_soak_address(peer, nonce, (uint8_t)(3u - role));
    if (length < 28u) return 0;
    for (uint32_t i = 0u; i < 6u; ++i)
        if (m[4u + i] != own[i] || m[10u + i] != peer[i]) return 0;
    if (m[0] != 8u) return 0;
    if (length < 60u || !clean_mac_valid(m, length) || (m[1] & ~8u) != 1u)
        goto corrupt;
    uint32_t magic = 0u, index = 0u;
    for (uint32_t i = 0u; i < 4u; ++i) {
        magic |= (uint32_t)m[24u + i] << (8u * i);
        index |= (uint32_t)m[28u + i] << (8u * i);
    }
    if (magic != CLEAN_SOAK_MAGIC || index >= s->target ||
        length != clean_soak_payload_size(nonce, index) + 28u ||
        ((uint16_t)m[22] | ((uint16_t)m[23] << 8u)) != (uint16_t)(index << 4u))
        goto corrupt;
    for (uint32_t i = 8u; i < length - 28u; ++i)
        if (m[24u + i] != clean_soak_pattern(nonce, index, i)) goto corrupt;
    if (index != s->received) {
        if (index < s->received) ++s->duplicates;
        else ++s->out_of_order;
        s->failure = 2u;
        return -1;
    }
    ++s->received;
    return 1;
corrupt:
    ++s->corrupt;
    s->failure = 1u;
    return -1;
}

static inline int clean_soak_receive_packet(clean_soak_status_t *s, const uint8_t *packet,
                                            uint32_t length, const uint8_t nonce[14],
                                            uint8_t role) {
    if (length < 18u) return 0;
    int result = clean_soak_receive(s, packet + 18u, length - 18u, nonce, role);
    if (result != 1) return result;
    s->rx_phy_last = packet[15] | ((uint32_t)packet[16] << 8u);
    if (nonce[13] >= CLEAN_SOAK_PHYS * CLEAN_SOAK_PAYLOADS) {
        ++s->rx_phy_mismatch;
        s->failure = 6u;
        return -1;
    }
    uint32_t rate = clean_soak_rate(nonce);
    uint8_t flags = rate == 4u ? CLEAN_RX_OFDM :
        clean_soak_short(nonce) ? CLEAN_RX_SHORT_PREAMBLE : 0u;
    uint8_t signal = rate == 4u ? clean_ofdm_rates[0].signal : clean_cck_rates[rate].signal;
    int rate_matches = rate == 4u ? (packet[15] & 15u) == signal : packet[15] == signal;
    if (!rate_matches || (packet[16] & 0xf0u) != flags) {
        ++s->rx_phy_mismatch;
        s->failure = 6u;
        return -1;
    }
    if (packet[17] != (clean_soak_fragments(nonce) > 1u ? 1u : 0u)) {
        ++s->rx_phy_mismatch;
        s->failure = 6u;
        return -1;
    }
    ++s->rx_phy_checked;
    return 1;
}

#endif

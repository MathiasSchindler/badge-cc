#ifndef CLEAN_STATION_LIFECYCLE_H
#define CLEAN_STATION_LIFECYCLE_H

#include "../peer/lifecycle.h"
#include "station.h"

#define CLEAN_LIFECYCLE_MAGIC 0x314c5342u
#define CLEAN_LIFECYCLE_DETAIL 520u
#define CLEAN_LIFECYCLE_EVENTS 32u
#define CLEAN_LIFECYCLE_SCAN_US 400000u
#define CLEAN_LIFECYCLE_BEACON_US 1500000u
#define CLEAN_LIFECYCLE_ECHO_US 1000000u

enum {
    CLEAN_LIFE_DISCOVER, CLEAN_LIFE_AUTHENTICATE, CLEAN_LIFE_ASSOCIATE,
    CLEAN_LIFE_CONNECTED, CLEAN_LIFE_BACKOFF, CLEAN_LIFE_STOPPED,
    CLEAN_LIFE_FAILED
};
enum {
    CLEAN_LIFE_NONE, CLEAN_LIFE_SCAN, CLEAN_LIFE_AUTH_FAILURE,
    CLEAN_LIFE_ASSOC_FAILURE, CLEAN_LIFE_BEACON_LOSS, CLEAN_LIFE_DEPARTURE,
    CLEAN_LIFE_CHANNEL_CHANGE, CLEAN_LIFE_DATA_LOSS, CLEAN_LIFE_CANCEL,
    CLEAN_LIFE_DEADLINE, CLEAN_LIFE_RADIO_ERROR, CLEAN_LIFE_UNSUPPORTED
};

typedef struct {
    uint32_t at, state, reason, epoch, channel;
} clean_lifecycle_event_t;

typedef struct {
    uint32_t magic, state, reason, epoch, channel, aid;
    uint32_t connections, data_connections, disconnects, failures;
    uint32_t auth_failures, assoc_failures, beacon_losses, departures, channel_changes;
    uint32_t scans, beacons, tx_attempts, tx_timeouts, echoes, echo_timeouts;
    uint32_t rejected_echoes, last_status, cleanup, elapsed_lo, elapsed_hi, events, tx_busy;
    clean_lifecycle_event_t event[CLEAN_LIFECYCLE_EVENTS];
} clean_lifecycle_report_t;
_Static_assert(CLEAN_LIFECYCLE_DETAIL + sizeof(clean_lifecycle_report_t) <= 1616u,
               "Lifecycle mode evidence must precede controller failure storage");

typedef struct {
    clean_lifecycle_report_t report;
    uint32_t phase_at, beacon_at, echo_at, send_at, sequence, beacon_timeout_us;
    uint8_t pending, verified, consecutive_echo_failures, needs_reset;
} clean_lifecycle_t;

static inline uint32_t clean_lifecycle_channel(const uint8_t *m, uint32_t n,
                                              uint32_t current) {
    uint32_t channel = current, announced = 0u, seen = 0u, csa = 0u;
    if (n < 40u) return 0u;
    for (uint32_t at = 36u; at < n - 4u;) {
        if (n - 4u - at < 2u) return 0u;
        uint32_t id = m[at++], size = m[at++];
        if (size > n - 4u - at) return 0u;
        if (id == 3u) {
            if (seen++ || size != 1u || m[at] < 1u || m[at] > 11u) return 0u;
            channel = m[at];
        }
        if (id == 37u) {
            if (csa++ || size != 3u || m[at] > 1u || m[at + 1u] < 1u ||
                m[at + 1u] > 11u) return 0u;
            if (m[at + 2u] <= 1u) announced = m[at + 1u];
        }
        at += size;
    }
    return announced ? announced : channel;
}

static inline uint32_t clean_lifecycle_frame(uint8_t frame[92], const uint8_t own[6],
        const uint8_t bssid[6], const uint8_t session[6], uint32_t epoch,
        uint32_t sequence, uint16_t mac_sequence) {
    badger_bytes_zero(frame, 92u);
    clean_station_header(frame + 6u, 8u, mac_sequence, own, bssid);
    frame[7] = 1u;
    static const uint8_t llc[8] = {0xaau,0xaau,3u,0u,0u,0u,0x88u,0xb5u};
    badger_bytes_copy(frame + 30u, llc, sizeof(llc));
    clean_lifecycle_payload(frame + 38u, session, epoch, sequence, 0u);
    badger_bytes_store_u32(frame + 86u, diag_crc32(frame + 6u, 80u));
    (void)clean_ofdm_plcp_rate(frame, 84u, 0u);
    return 90u;
}

static inline int clean_lifecycle_response(const uint8_t *m, uint32_t n,
        const uint8_t own[6], const uint8_t bssid[6]) {
    static const uint8_t llc[8] = {0xaau,0xaau,3u,0u,0u,0u,0x88u,0xb5u};
    return n == 84u && m[0] == 8u && (m[1] & ~8u) == 2u &&
        clean_frame_valid(m, n) && badger_bytes_equal(m + 4u, own, 6u) &&
        badger_bytes_equal(m + 10u, bssid, 6u) &&
        badger_bytes_equal(m + 16u, bssid, 6u) &&
        badger_bytes_equal(m + 24u, llc, sizeof(llc));
}

static inline void clean_lifecycle_state(clean_lifecycle_t *s, uint32_t now,
                                         uint32_t state, uint32_t reason) {
    clean_lifecycle_report_t *r = &s->report;
    r->state = state; r->reason = reason;
    s->phase_at = now;
    clean_lifecycle_event_t *e = &r->event[r->events % CLEAN_LIFECYCLE_EVENTS];
    e->at = now; e->state = state; e->reason = reason;
    e->epoch = r->epoch; e->channel = r->channel;
    ++r->events;
}

static inline int clean_lifecycle_init(clean_lifecycle_t *s, uint32_t now,
                                       uint32_t channel) {
    badger_bytes_zero((uint8_t *)s, sizeof(*s));
    s->report.magic = CLEAN_LIFECYCLE_MAGIC;
    if (channel < 1u || channel > 11u) {
        clean_lifecycle_state(s, now, CLEAN_LIFE_FAILED, CLEAN_LIFE_UNSUPPORTED);
        return 0;
    }
    s->report.channel = channel;
    s->beacon_timeout_us = CLEAN_LIFECYCLE_BEACON_US;
    clean_lifecycle_state(s, now, CLEAN_LIFE_DISCOVER, CLEAN_LIFE_NONE);
    return 1;
}

static inline void clean_lifecycle_disconnect(clean_lifecycle_t *s, uint32_t now,
                                              uint32_t reason, uint32_t status) {
    clean_lifecycle_report_t *r = &s->report;
    if (r->state == CLEAN_LIFE_STOPPED || r->state == CLEAN_LIFE_FAILED) return;
    if (r->state == CLEAN_LIFE_CONNECTED) ++r->disconnects;
    else ++r->failures;
    r->last_status = status;
    if (reason == CLEAN_LIFE_AUTH_FAILURE) ++r->auth_failures;
    if (reason == CLEAN_LIFE_ASSOC_FAILURE) ++r->assoc_failures;
    if (reason == CLEAN_LIFE_BEACON_LOSS) ++r->beacon_losses;
    if (reason == CLEAN_LIFE_DEPARTURE) ++r->departures;
    if (reason == CLEAN_LIFE_CHANNEL_CHANGE) ++r->channel_changes;
    r->aid = 0u;
    s->pending = s->verified = s->consecutive_echo_failures = 0u;
    s->needs_reset = 1u;
    clean_lifecycle_state(s, now, CLEAN_LIFE_BACKOFF, reason);
}

static inline int clean_lifecycle_found(clean_lifecycle_t *s, uint32_t now) {
    if (s->report.state != CLEAN_LIFE_DISCOVER) return 0;
    ++s->report.epoch;
    clean_lifecycle_state(s, now, CLEAN_LIFE_AUTHENTICATE, CLEAN_LIFE_NONE);
    return 1;
}

static inline int clean_lifecycle_authenticated(clean_lifecycle_t *s, uint32_t now) {
    if (s->report.state != CLEAN_LIFE_AUTHENTICATE) return 0;
    clean_lifecycle_state(s, now, CLEAN_LIFE_ASSOCIATE, CLEAN_LIFE_NONE);
    return 1;
}

static inline int clean_lifecycle_associated(clean_lifecycle_t *s, uint32_t now,
                                            uint16_t aid) {
    if (s->report.state != CLEAN_LIFE_ASSOCIATE || !aid || aid > 2007u) return 0;
    s->report.aid = aid;
    ++s->report.connections;
    s->beacon_at = now;
    s->send_at = now - 500000u;
    s->pending = s->verified = s->consecutive_echo_failures = 0u;
    clean_lifecycle_state(s, now, CLEAN_LIFE_CONNECTED, CLEAN_LIFE_NONE);
    return 1;
}

static inline int clean_lifecycle_tick(clean_lifecycle_t *s, uint32_t now) {
    clean_lifecycle_report_t *r = &s->report;
    if (r->state == CLEAN_LIFE_BACKOFF && now - s->phase_at >= 250000u) {
        clean_lifecycle_state(s, now, CLEAN_LIFE_DISCOVER, r->reason);
        return 1;
    }
    if (r->state == CLEAN_LIFE_DISCOVER && now - s->phase_at >= CLEAN_LIFECYCLE_SCAN_US) {
        r->channel = r->channel == 11u ? 1u : r->channel + 1u;
        ++r->scans;
        clean_lifecycle_state(s, now, CLEAN_LIFE_DISCOVER, CLEAN_LIFE_SCAN);
        return 1;
    }
    if (r->state == CLEAN_LIFE_CONNECTED) {
        if (now - s->beacon_at >= s->beacon_timeout_us) {
            clean_lifecycle_disconnect(s, now, CLEAN_LIFE_BEACON_LOSS, 0u);
        } else if (s->pending && now - s->echo_at >= CLEAN_LIFECYCLE_ECHO_US) {
            ++r->echo_timeouts;
            s->pending = 0u;
            if (++s->consecutive_echo_failures == 3u)
                clean_lifecycle_disconnect(s, now, CLEAN_LIFE_DATA_LOSS, 0u);
        }
    }
    return 0;
}

static inline int clean_lifecycle_echo(clean_lifecycle_t *s, uint32_t now,
        const uint8_t *p, uint32_t n, const uint8_t session[6]) {
    if (s->report.state != CLEAN_LIFE_CONNECTED || !s->pending ||
        !clean_lifecycle_payload_valid(p, n, session, s->report.epoch, s->sequence, 1u)) {
        ++s->report.rejected_echoes;
        return 0;
    }
    s->pending = 0u;
    s->consecutive_echo_failures = 0u;
    ++s->report.echoes;
    if (!s->verified) { ++s->report.data_connections; s->verified = 1u; }
    s->send_at = now;
    return 1;
}

static inline void clean_lifecycle_stop(clean_lifecycle_t *s, uint32_t now,
                                        uint32_t reason, uint32_t error) {
    s->report.last_status = error;
    s->report.aid = 0u;
    s->pending = 0u;
    clean_lifecycle_state(s, now, error ? CLEAN_LIFE_FAILED : CLEAN_LIFE_STOPPED, reason);
}

#endif

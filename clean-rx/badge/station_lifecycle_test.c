#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "station.h"
#include "station_lifecycle.h"

static void connect(clean_lifecycle_t *s, uint32_t now) {
    assert(clean_lifecycle_found(s, now));
    assert(clean_lifecycle_authenticated(s, now + 1u));
    assert(clean_lifecycle_associated(s, now + 2u, 17u));
}

int main(void) {
    const uint8_t own[6] = {2u, 1u, 2u, 3u, 4u, 5u};
    const uint8_t ap[6] = {2u, 6u, 7u, 8u, 9u, 10u};
    uint8_t frame[92], beacon[64] = {0};
    assert(clean_lifecycle_frame(frame, own, ap, own, 7u, 9u, 13u) == 90u);
    assert(clean_frame_valid(frame + 6u, 84u) && frame[7] == 1u);
    assert(clean_lifecycle_payload_valid(frame + 38u, 48u, own, 7u, 9u, 0u));
    assert(!clean_lifecycle_response(frame + 6u, 84u, own, ap));
    frame[7] = 2u;
    memcpy(frame + 10u, own, 6u); memcpy(frame + 16u, ap, 6u);
    badger_bytes_store_u32(frame + 86u, diag_crc32(frame + 6u, 80u));
    assert(clean_lifecycle_response(frame + 6u, 84u, own, ap));
    assert(!clean_lifecycle_response(frame + 6u, 83u, own, ap));
    for (uint32_t i = 0u; i < 84u; ++i) {
        frame[6u + i] ^= 1u;
        assert(!clean_lifecycle_response(frame + 6u, 84u, own, ap));
        frame[6u + i] ^= 1u;
    }
    const uint8_t bound[] = {4u, 10u, 16u, 24u, 1u};
    for (uint32_t i = 0u; i < sizeof(bound); ++i) {
        frame[6u + bound[i]] ^= 1u;
        badger_bytes_store_u32(frame + 86u, diag_crc32(frame + 6u, 80u));
        assert(!clean_lifecycle_response(frame + 6u, 84u, own, ap));
        frame[6u + bound[i]] ^= 1u;
    }
    badger_bytes_store_u32(frame + 86u, diag_crc32(frame + 6u, 80u));
    assert(clean_lifecycle_response(frame + 6u, 84u, own, ap));
    assert(clean_lifecycle_channel(beacon, 40u, 1u) == 1u);
    const uint8_t ies[] = {3u,1u,6u,37u,3u,1u,11u,1u};
    memcpy(beacon + 36u, ies, sizeof(ies));
    assert(clean_lifecycle_channel(beacon, 48u, 1u) == 11u);
    beacon[43] = 2u;
    assert(clean_lifecycle_channel(beacon, 48u, 1u) == 6u);
    beacon[43] = 1u; beacon[42] = 0u;
    assert(!clean_lifecycle_channel(beacon, 48u, 1u));
    beacon[42] = 11u;
    beacon[44] = 3u; beacon[45] = 1u; beacon[46] = 2u;
    assert(!clean_lifecycle_channel(beacon, 51u, 1u));
    beacon[44] = 37u; beacon[45] = 3u;
    assert(!clean_lifecycle_channel(beacon, 53u, 1u));
    beacon[44] = 99u; beacon[45] = 3u;
    assert(!clean_lifecycle_channel(beacon, 50u, 1u));
    beacon[38] = 12u;
    assert(!clean_lifecycle_channel(beacon, 48u, 1u));
    clean_lifecycle_t s;
    assert(!clean_lifecycle_init(&s, 0u, 0u));
    assert(s.report.state == CLEAN_LIFE_FAILED);
    assert(clean_lifecycle_init(&s, 0xfffffff0u, 11u));
    assert(!clean_lifecycle_tick(&s, 0xfffffff0u + CLEAN_LIFECYCLE_SCAN_US - 1u));
    assert(clean_lifecycle_tick(&s, 0xfffffff0u + CLEAN_LIFECYCLE_SCAN_US));
    assert(s.report.channel == 1u && s.report.scans == 1u);
    assert(!clean_lifecycle_associated(&s, 1u, 17u));
    assert(!clean_lifecycle_authenticated(&s, 1u));
    connect(&s, 0xfffffff0u);
    assert(s.report.connections == 1u && s.report.data_connections == 0u);
    assert(!clean_lifecycle_tick(&s, s.beacon_at + CLEAN_LIFECYCLE_BEACON_US - 1u));
    assert(s.report.state == CLEAN_LIFE_CONNECTED);
    assert(!clean_lifecycle_tick(&s, s.beacon_at + CLEAN_LIFECYCLE_BEACON_US));
    assert(s.report.state == CLEAN_LIFE_BACKOFF && s.report.beacon_losses == 1u);
    assert(!s.report.aid && !s.pending && s.needs_reset);
    assert(!clean_lifecycle_tick(&s, s.phase_at + 249999u));
    assert(clean_lifecycle_tick(&s, s.phase_at + 250000u));
    connect(&s, 1000000u);
    const uint8_t session[6] = {1u, 2u, 3u, 4u, 5u, 6u};
    uint8_t p[CLEAN_LIFECYCLE_PAYLOAD_BYTES];
    s.pending = 1u; s.sequence = 4u;
    clean_lifecycle_payload(p, session, s.report.epoch, s.sequence, 1u);
    for (uint32_t i = 0u; i < sizeof(p); ++i) {
        p[i] ^= 1u;
        assert(!clean_lifecycle_echo(&s, 1000100u, p, sizeof(p), session));
        p[i] ^= 1u;
        assert(s.pending);
    }
    assert(!clean_lifecycle_echo(&s, 1000100u, p, sizeof(p) - 1u, session));
    assert(clean_lifecycle_echo(&s, 1000100u, p, sizeof(p), session));
    assert(s.report.data_connections == 1u && !s.pending);
    assert(!clean_lifecycle_echo(&s, 1000101u, p, sizeof(p), session));
    for (uint32_t i = 0u; i < 5u; ++i) {
        clean_lifecycle_disconnect(&s, 2000000u + i, CLEAN_LIFE_DEPARTURE, 3u);
        assert(!s.report.aid && !s.pending);
        assert(clean_lifecycle_tick(&s, s.phase_at + 250000u));
        connect(&s, 3000000u + i);
        s.pending = 1u;
        assert(!clean_lifecycle_echo(&s, 3000100u + i, p, sizeof(p), session));
        clean_lifecycle_payload(p, session, s.report.epoch, s.sequence, 1u);
        assert(clean_lifecycle_echo(&s, 3000100u + i, p, sizeof(p), session));
    }
    assert(s.report.connections == 7u && s.report.data_connections == 6u);
    assert(s.report.departures == 5u && s.report.disconnects == 6u);
    for (uint32_t state = CLEAN_LIFE_DISCOVER; state <= CLEAN_LIFE_BACKOFF; ++state) {
        s.report.state = state; s.pending = 1u;
        clean_lifecycle_stop(&s, 20u, CLEAN_LIFE_CANCEL, 0u);
        assert(s.report.state == CLEAN_LIFE_STOPPED && !s.pending && !s.report.aid);
    }
    assert(clean_lifecycle_init(&s, 0u, 1u));
    for (uint32_t i = 0u; i < 100u; ++i) {
        clean_lifecycle_state(&s, i, CLEAN_LIFE_DISCOVER, CLEAN_LIFE_SCAN);
        assert(s.report.event[(s.report.events - 1u) % CLEAN_LIFECYCLE_EVENTS].at == i);
    }
    assert(clean_lifecycle_found(&s, 101u));
    clean_lifecycle_disconnect(&s, 102u, CLEAN_LIFE_AUTH_FAILURE, 17u);
    assert(s.report.auth_failures == 1u && s.report.failures == 1u &&
           !s.report.connections && s.report.last_status == 17u);
    assert(clean_lifecycle_init(&s, 0u, 1u));
    connect(&s, 0u);
    for (uint32_t i = 0u; i < 3u; ++i) {
        uint32_t now = 1000000u * (i + 1u);
        s.pending = 1u; s.echo_at = now - 1000000u; s.beacon_at = now - 2u;
        assert(!clean_lifecycle_tick(&s, now - 1u) && s.pending);
        assert(!clean_lifecycle_tick(&s, now));
        assert(s.report.echo_timeouts == i + 1u && !s.pending);
    }
    assert(s.report.state == CLEAN_LIFE_BACKOFF && s.report.reason == CLEAN_LIFE_DATA_LOSS);
    assert(clean_lifecycle_init(&s, 0u, 1u));
    connect(&s, 0u);
    s.beacon_timeout_us = 3000000u;
    assert(!clean_lifecycle_tick(&s, s.beacon_at + 2999999u));
    assert(s.report.state == CLEAN_LIFE_CONNECTED);
    assert(!clean_lifecycle_tick(&s, s.beacon_at + 3000000u));
    assert(s.report.state == CLEAN_LIFE_BACKOFF);
    clean_lifecycle_stop(&s, 4000000u, CLEAN_LIFE_UNSUPPORTED, 1u);
    assert(s.report.state == CLEAN_LIFE_FAILED && s.report.reason == CLEAN_LIFE_UNSUPPORTED &&
           !s.report.aid && !s.pending);
    assert(clean_lifecycle_init(&s, 0u, 1u));
    connect(&s, 0u);
    s.report.channel = 6u;
    clean_lifecycle_disconnect(&s, 100u, CLEAN_LIFE_CHANNEL_CHANGE, 0u);
    assert(s.report.state == CLEAN_LIFE_BACKOFF && s.report.channel_changes == 1u &&
           s.needs_reset && !s.report.aid);
    assert(clean_lifecycle_tick(&s, 250100u) && s.report.channel == 6u);
    puts("B1 lifecycle: guarded phases, wrap-safe scan/beacon/backoff, five recovery epochs, "
         "exact echo binding, bounded journal and cancellation gates pass; NOT RF");
    return 0;
}

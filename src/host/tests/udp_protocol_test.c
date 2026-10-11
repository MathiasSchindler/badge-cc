#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../sdk/wifi_handover/udp_protocol.h"

static udp_message_t ping(uint8_t sender, uint8_t target) {
    return (udp_message_t){
        .type = UDP_PING, .session = 0x12345678u,
        .token = UINT64_C(0x0102030405060708),
        .sender = sender, .origin = sender, .target = target,
        .epoch = 13, .run = 19, .sequence = 1, .stamp_us = 0xfedcba98u
    };
}

static udp_message_t echo(const udp_message_t *request) {
    udp_message_t m = *request;
    m.type = UDP_PONG;
    m.sender = request->target;
    m.target = request->sender;
    return m;
}

static void test_wire(void) {
    uint8_t bytes[UDP_PROTOCOL_SIZE + 1];
    udp_message_t m = ping(1, 2), decoded;
    assert(udp_protocol_encode(bytes, &m));
    const uint8_t expected[] = {
        0x57, 0x48, 1, UDP_PING, 0x12, 0x34, 0x56, 0x78,
        1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 1,
        0, 0, 0, 13, 0, 0, 0, 19, 0xfe, 0xdc, 0xba, 0x98,
        1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    assert(sizeof(expected) == UDP_PROTOCOL_SIZE);
    assert(memcmp(bytes, expected, sizeof(expected)) == 0);
    assert(udp_protocol_decode(&decoded, bytes, UDP_PROTOCOL_SIZE));
    assert(decoded.token == m.token && decoded.stamp_us == m.stamp_us);
    assert(decoded.run == m.run && decoded.epoch == m.epoch);
    for (size_t n = 0; n < UDP_PROTOCOL_SIZE; ++n)
        assert(!udp_protocol_decode(&decoded, bytes, n));
    assert(!udp_protocol_decode(&decoded, bytes, sizeof(bytes)));
    assert(!udp_protocol_decode(&decoded, NULL, UDP_PROTOCOL_SIZE));
    assert(!udp_protocol_decode(NULL, bytes, UDP_PROTOCOL_SIZE));
    const unsigned invalid_offsets[] = {0, 1, 2, 3, 32, 33, 34, 35, 36, 38, 40, 42};
    for (unsigned i = 0; i < sizeof(invalid_offsets) / sizeof(invalid_offsets[0]); ++i) {
        memcpy(bytes, expected, sizeof(expected));
        bytes[invalid_offsets[i]] = 255;
        assert(!udp_protocol_decode(&decoded, bytes, UDP_PROTOCOL_SIZE));
    }
    memcpy(bytes, expected, sizeof(expected));
    memset(bytes + 4, 0, 4);
    assert(!udp_protocol_decode(&decoded, bytes, UDP_PROTOCOL_SIZE));
    memcpy(bytes, expected, sizeof(expected));
    memset(bytes + 8, 0, 8);
    assert(!udp_protocol_decode(&decoded, bytes, UDP_PROTOCOL_SIZE));
    m.origin = 3;
    assert(!udp_protocol_encode(bytes, &m));

    m = (udp_message_t){.type = UDP_HELLO, .session = 1, .token = 2,
                       .sender = 1, .origin = 1, .run = 3};
    assert(udp_protocol_encode(bytes, &m));
    m.epoch = 1;
    assert(!udp_protocol_encode(bytes, &m));
    m.type = UDP_WELCOME;
    m.sender = 0;
    m.target = 1;
    assert(udp_protocol_encode(bytes, &m));
    m.type = UDP_INPUT;
    m.sender = 1;
    m.target = 0;
    m.run = 0;
    m.positions[1] = 1000;
    assert(udp_protocol_encode(bytes, &m));
    m.positions[1] = 1001;
    assert(!udp_protocol_encode(bytes, &m));
    m.positions[1] = 500;
    m.positions[0] = 1;
    assert(!udp_protocol_encode(bytes, &m));
    m.type = UDP_STATE;
    m.sender = 0;
    m.target = 1;
    m.origin = 0;
    m.online_mask = 15;
    m.positions[0] = 0;
    m.positions[2] = 1000;
    m.positions[3] = 257;
    assert(udp_protocol_encode(bytes, &m));
    assert(bytes[40] == 3 && bytes[41] == 232);
    assert(udp_protocol_decode(&decoded, bytes, UDP_PROTOCOL_SIZE));
    assert(decoded.positions[3] == 257);
    m.online_mask = 14;
    assert(!udp_protocol_encode(bytes, &m));
}

static void test_endpoints(void) {
    uint64_t tokens[EXP_MAX_PLAYERS] = {1, UINT64_C(0x0102030405060708), 3, 4};
    udp_message_t m = ping(1, 2);
    assert(udp_protocol_ip(0) == 0xc0a80401u);
    assert(udp_protocol_ip(3) == 0xc0a80404u);
    assert(udp_protocol_ip(4) == 0);
    assert(udp_protocol_endpoint(&m, m.session, 0, tokens,
                                 udp_protocol_ip(1), UDP_PROTOCOL_PORT));
    assert(!udp_protocol_endpoint(&m, m.session, 0, tokens,
                                  udp_protocol_ip(2), UDP_PROTOCOL_PORT));
    assert(!udp_protocol_endpoint(&m, m.session, 0, tokens,
                                  udp_protocol_ip(1), UDP_PROTOCOL_PORT + 1));
    assert(!udp_protocol_endpoint(&m, m.session + 1, 0, tokens,
                                  udp_protocol_ip(1), UDP_PROTOCOL_PORT));
    tokens[1]++;
    assert(!udp_protocol_endpoint(&m, m.session, 0, tokens,
                                  udp_protocol_ip(1), UDP_PROTOCOL_PORT));
    tokens[1]--;
    assert(udp_protocol_relay(&m, tokens[2], 55));
    assert(m.sender == 1 && m.origin == 1 && m.target == 2 && m.epoch == 55);
    assert(udp_protocol_endpoint(&m, m.session, 2, tokens,
                                 udp_protocol_ip(0), UDP_PROTOCOL_PORT));
    assert(!udp_protocol_endpoint(&m, m.session, 2, tokens,
                                  udp_protocol_ip(1), UDP_PROTOCOL_PORT));
    assert(!udp_protocol_endpoint(&m, m.session, 3, tokens,
                                  udp_protocol_ip(0), UDP_PROTOCOL_PORT));
    udp_message_t pong = echo(&m);
    assert(udp_protocol_endpoint(&pong, pong.session, 0, tokens,
                                 udp_protocol_ip(2), UDP_PROTOCOL_PORT));
    assert(udp_protocol_relay(&pong, tokens[1], 13));
    assert(udp_protocol_endpoint(&pong, pong.session, 1, tokens,
                                 udp_protocol_ip(0), UDP_PROTOCOL_PORT));
    assert(pong.sender == 2 && pong.origin == 1 && pong.target == 1);
    m = ping(1, 0);
    assert(!udp_protocol_relay(&m, 2, 3));
    m = ping(0, 1);
    m.token = tokens[1];
    assert(udp_protocol_endpoint(&m, m.session, 1, tokens,
                                 udp_protocol_ip(0), UDP_PROTOCOL_PORT));
    pong = echo(&m);
    assert(udp_protocol_endpoint(&pong, pong.session, 0, tokens,
                                 udp_protocol_ip(1), UDP_PROTOCOL_PORT));
}

static void test_peer_sequences(void) {
    assert(udp_sequence_newer(0, UINT32_MAX));
    assert(udp_sequence_newer(7, UINT32_MAX - 2));
    assert(!udp_sequence_newer(UINT32_MAX, 0));
    assert(!udp_sequence_newer(4, 4));
    assert(!udp_sequence_newer(0x80000000u, 0));
    udp_peer_t p = {0};
    assert(!udp_peer_input(&p, 7, 1, 0));
    assert(udp_peer_hello(&p, 10, 20, 0) == 20);
    assert(!p.online && !p.sequence_valid);
    assert(udp_peer_hello(&p, 10, 21, 250) == 20);
    assert(udp_peer_input(&p, 20, UINT32_MAX, 300));
    assert(p.online && p.epoch == 20 && p.nonce == 10);
    assert(udp_peer_hello(&p, 10, 22, 350) == 20);
    assert(!udp_peer_input(&p, 20, UINT32_MAX, 360));
    assert(udp_peer_input(&p, 20, 0, 370));
    assert(!udp_peer_input(&p, 20, UINT32_MAX - 1, 380));
    assert(udp_peer_hello(&p, 11, 30, 400) == 30);
    assert(udp_peer_input(&p, 20, 1, 410)); /* Old connection still works until commit. */
    assert(udp_peer_input(&p, 30, 0, 420));
    assert(p.epoch == 30 && p.nonce == 11);
    assert(udp_peer_hello(&p, 10, 31, 430) == 31); /* Delayed old HELLO. */
    assert(!udp_peer_input(&p, 20, 2, 440));       /* Old epoch cannot reactivate. */
    assert(udp_peer_input(&p, 30, 1, 450));
    assert(udp_peer_hello(&p, 11, 32, 460) == 30);
    assert(!udp_peer_input(&p, 30, 1, 470));
    assert(!udp_peer_expire(&p, 3449));
    assert(udp_peer_expire(&p, 3450));
    assert(!p.online && p.epoch == 30);
    assert(udp_peer_input(&p, 30, 2, 3500)); /* Stable-slot reconnect, sequence retained. */
    assert(!udp_peer_input(&p, 31, 0, 6000)); /* Expired challenge. */

    memset(&p, 0, sizeof(p));
    assert(udp_peer_hello(&p, 1, 2, UINT32_MAX - 100) == 2);
    assert(udp_peer_input(&p, 2, 1, UINT32_MAX - 10));
    assert(!udp_peer_expire(&p, 2000));
    assert(udp_peer_expire(&p, 3000));
}

static void test_relay(void) {
    udp_relay_t route = {0};
    udp_message_t m = ping(1, 2), pong = echo(&m);
    assert(udp_relay_ping(&route, &m, 100));
    assert(!udp_relay_ping(&route, &m, 101));
    pong.sender = 3;
    assert(!udp_relay_reply(&route, &pong, 102));
    pong = echo(&m);
    pong.run++;
    assert(!udp_relay_reply(&route, &pong, 102));
    pong = echo(&m);
    pong.sequence++;
    assert(!udp_relay_reply(&route, &pong, 102));
    pong = echo(&m);
    pong.stamp_us++;
    assert(!udp_relay_reply(&route, &pong, 102));
    pong = echo(&m);
    pong.origin = pong.target = 3;
    assert(!udp_relay_reply(&route, &pong, 102));
    pong = echo(&m);
    assert(udp_relay_reply(&route, &pong, 102));
    assert(!udp_relay_reply(&route, &pong, 103));
    m.sequence++;
    assert(udp_relay_ping(&route, &m, UINT32_MAX - 10));
    pong = echo(&m);
    assert(!udp_relay_reply(&route, &pong, 990));
    m.run++;
    m.sequence = 1;
    assert(udp_relay_ping(&route, &m, 1000));
    pong = echo(&m);
    pong.type = UDP_BENCH_ERROR;
    assert(udp_relay_reply(&route, &pong, 1001));
    m.run--;
    assert(!udp_relay_ping(&route, &m, 1002));
}

static udp_message_t pending_echo(const udp_benchmark_t *b, uint8_t local_slot) {
    udp_message_t m = ping(local_slot, b->metrics.target);
    m.run = b->generation;
    m.sequence = b->pending_sequence;
    m.stamp_us = b->pending_us;
    return echo(&m);
}

static void test_statistics(void) {
    static udp_benchmark_t b;
    uint32_t now = UINT32_MAX - 100u;
    udp_benchmark_start(&b, 2, 88, now);
    for (uint32_t i = 0; i < EXP_BENCH_SAMPLES; ++i) {
        assert(udp_benchmark_due(&b, now));
        udp_benchmark_sent(&b, now);
        assert(!udp_benchmark_due(&b, now));
        assert(b.metrics.sent == i + 1);
        udp_message_t m = pending_echo(&b, 1);
        now += EXP_BENCH_SAMPLES - i;
        assert(udp_benchmark_echo(&b, &m, 1, now));
        assert(!udp_benchmark_echo(&b, &m, 1, now));
        if (i + 1 < EXP_BENCH_SAMPLES) {
            assert(b.metrics.median_us == 0); /* No per-tick sorting. */
            assert(!udp_benchmark_due(&b, now + UDP_BENCH_GAP_US - 1));
        }
        now += UDP_BENCH_GAP_US;
    }
    assert(!b.metrics.running && !b.outstanding);
    assert(b.metrics.sent == 1000 && b.metrics.received == 1000);
    assert(b.metrics.lost == 0 && b.metrics.local_errors == 0);
    assert(b.metrics.min_us == 1 && b.metrics.median_us == 500);
    assert(b.metrics.p95_us == 950 && b.metrics.p99_us == 990);
    assert(b.metrics.max_us == 1000 && b.metrics.latest_us == 1);

    udp_benchmark_start(&b, 0, 89, 0);
    now = 0;
    const uint32_t samples[] = {8, 1, 4, 3, 2};
    for (unsigned i = 0; i < 5; ++i) {
        udp_benchmark_sent(&b, now);
        udp_message_t m = pending_echo(&b, 1);
        now += samples[i];
        assert(udp_benchmark_echo(&b, &m, 1, now));
        now += UDP_BENCH_GAP_US;
    }
    udp_benchmark_cancel(&b);
    assert(b.metrics.median_us == 3 && b.metrics.p95_us == 8 && b.metrics.p99_us == 8);
    assert(b.metrics.latest_us == 2);
}

static void test_loss_errors_and_generation(void) {
    static udp_benchmark_t b;
    udp_benchmark_start(&b, 1, 7, 0);
    udp_benchmark_local_error(&b);
    assert(!b.metrics.running && b.metrics.local_errors == 1);
    assert(b.metrics.sent == 0 && b.metrics.lost == 0);
    udp_benchmark_local_error(&b);
    assert(b.metrics.local_errors == 1);
    udp_benchmark_start(&b, 1, 8, 0);
    udp_benchmark_sent(&b, 0);
    udp_message_t old = pending_echo(&b, 0);
    udp_benchmark_cancel(&b);
    assert(b.metrics.sent == 1 && b.metrics.received == 0 && b.metrics.lost == 0);
    udp_benchmark_start(&b, 1, 9, 0);
    udp_benchmark_sent(&b, 0);
    assert(!udp_benchmark_echo(&b, &old, 0, 10));
    udp_message_t reply = pending_echo(&b, 0);
    reply.sender = 2;
    assert(!udp_benchmark_echo(&b, &reply, 0, 10));
    reply = pending_echo(&b, 0);
    reply.sequence++;
    assert(!udp_benchmark_echo(&b, &reply, 0, 10));
    reply = pending_echo(&b, 0);
    reply.stamp_us++;
    assert(!udp_benchmark_echo(&b, &reply, 0, 10));
    reply = pending_echo(&b, 0);
    reply.type = UDP_BENCH_ERROR;
    assert(udp_benchmark_remote_error(&b, &reply, 0, 10));
    assert(!b.metrics.running && b.metrics.local_errors == 1 && b.metrics.lost == 0);

    udp_benchmark_start(&b, 2, 10, UINT32_MAX - 10);
    udp_benchmark_sent(&b, UINT32_MAX - 10);
    reply = pending_echo(&b, 1);
    udp_benchmark_tick(&b, 999988);
    assert(b.outstanding && b.metrics.lost == 0);
    assert(!udp_benchmark_echo(&b, &reply, 1, 999989)); /* Exactly 1 second. */
    assert(b.metrics.lost == 1 && b.metrics.received == 0 && !b.outstanding);
    assert(!udp_benchmark_due(&b, 1019988));
    assert(udp_benchmark_due(&b, 1019989));
    udp_benchmark_sent(&b, 1019989);
    assert(!udp_benchmark_echo(&b, &reply, 1, 1019990));
    reply = pending_echo(&b, 1);
    assert(udp_benchmark_echo(&b, &reply, 1, 1019999));
    assert(b.metrics.sent == 2 && b.metrics.received == 1 && b.metrics.lost == 1);

    udp_benchmark_start(&b, 1, 11, 0);
    uint32_t now = 0;
    for (uint32_t i = 0; i < EXP_BENCH_SAMPLES; ++i) {
        assert(udp_benchmark_due(&b, now));
        udp_benchmark_sent(&b, now);
        now += UDP_BENCH_TIMEOUT_US;
        udp_benchmark_tick(&b, now);
        now += UDP_BENCH_GAP_US;
    }
    assert(!b.metrics.running && b.metrics.sent == 1000);
    assert(b.metrics.lost == 1000 && b.metrics.received == 0);
    assert(b.metrics.min_us == 0 && b.metrics.p99_us == 0);
}

int main(void) {
    test_wire();
    test_endpoints();
    test_peer_sequences();
    test_relay();
    test_statistics();
    test_loss_errors_and_generation();
    puts("udp_protocol_test: all tests passed");
    return 0;
}

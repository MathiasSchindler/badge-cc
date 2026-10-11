#include "udp_protocol.h"

#include <stdlib.h>
#include <string.h>

static uint32_t read32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void write32(uint8_t *p, uint32_t n) {
    p[0] = (uint8_t)(n >> 24);
    p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8);
    p[3] = (uint8_t)n;
}

static bool valid_message(const udp_message_t *m) {
    if (!m || !m->session || !m->token ||
        m->sender >= EXP_MAX_PLAYERS || m->target >= EXP_MAX_PLAYERS ||
        m->origin >= EXP_MAX_PLAYERS || m->sender == m->target ||
        m->online_mask > 15u)
        return false;
    for (unsigned i = 0; i < EXP_MAX_PLAYERS; ++i) {
        if (m->positions[i] > EXP_POSITION_MAX) return false;
        if (m->type != UDP_STATE &&
            (m->type != UDP_INPUT || i != m->sender) && m->positions[i])
            return false;
    }
    if (m->type != UDP_STATE && m->online_mask) return false;
    switch (m->type) {
    case UDP_HELLO:
        return m->sender && !m->target && m->origin == m->sender &&
               !m->epoch && m->run && !m->sequence && !m->stamp_us;
    case UDP_WELCOME:
        return !m->sender && m->target && m->origin == m->target &&
               m->epoch && m->run && !m->sequence && !m->stamp_us;
    case UDP_INPUT:
        return m->sender && !m->target && m->origin == m->sender &&
               m->epoch && !m->run && !m->stamp_us;
    case UDP_STATE:
        return !m->sender && m->target && !m->origin &&
               m->epoch && !m->run && !m->stamp_us && (m->online_mask & 1u);
    case UDP_PING:
    case UDP_PONG:
    case UDP_BENCH_ERROR:
        return m->epoch && m->run && m->sequence >= 1u &&
               m->sequence <= EXP_BENCH_SAMPLES &&
               (m->type == UDP_PING ? m->sender == m->origin :
                                     m->target == m->origin);
    default:
        return false;
    }
}

bool udp_protocol_encode(uint8_t out[UDP_PROTOCOL_SIZE], const udp_message_t *m) {
    if (!out || !valid_message(m)) return false;
    out[0] = 0x57;
    out[1] = 0x48;
    out[2] = 1;
    out[3] = m->type;
    write32(out + 4, m->session);
    write32(out + 8, (uint32_t)(m->token >> 32));
    write32(out + 12, (uint32_t)m->token);
    write32(out + 16, m->sequence);
    write32(out + 20, m->epoch);
    write32(out + 24, m->run);
    write32(out + 28, m->stamp_us);
    out[32] = m->sender;
    out[33] = m->target;
    out[34] = m->origin;
    out[35] = m->online_mask;
    for (unsigned i = 0; i < EXP_MAX_PLAYERS; ++i) {
        out[36 + 2 * i] = (uint8_t)(m->positions[i] >> 8);
        out[37 + 2 * i] = (uint8_t)m->positions[i];
    }
    return true;
}

bool udp_protocol_decode(udp_message_t *out, const uint8_t *data, size_t size) {
    if (!out || !data || size != UDP_PROTOCOL_SIZE ||
        data[0] != 0x57 || data[1] != 0x48 || data[2] != 1)
        return false;
    udp_message_t m = {0};
    m.type = data[3];
    m.session = read32(data + 4);
    m.token = ((uint64_t)read32(data + 8) << 32) | read32(data + 12);
    m.sequence = read32(data + 16);
    m.epoch = read32(data + 20);
    m.run = read32(data + 24);
    m.stamp_us = read32(data + 28);
    m.sender = data[32];
    m.target = data[33];
    m.origin = data[34];
    m.online_mask = data[35];
    for (unsigned i = 0; i < EXP_MAX_PLAYERS; ++i)
        m.positions[i] = (uint16_t)(((uint16_t)data[36 + 2 * i] << 8) |
                                   data[37 + 2 * i]);
    if (!valid_message(&m)) return false;
    *out = m;
    return true;
}

uint32_t udp_protocol_ip(uint8_t slot) {
    return slot < EXP_MAX_PLAYERS ? 0xc0a80401u + slot : 0;
}

bool udp_sequence_newer(uint32_t sequence, uint32_t previous) {
    uint32_t delta = sequence - previous;
    return delta != 0 && delta < 0x80000000u;
}

bool udp_protocol_endpoint(const udp_message_t *m, uint32_t session,
                           uint8_t local_slot, const uint64_t tokens[EXP_MAX_PLAYERS],
                           uint32_t source_ip, uint16_t source_port) {
    if (!tokens || !valid_message(m) || local_slot >= EXP_MAX_PLAYERS ||
        m->session != session || source_port != UDP_PROTOCOL_PORT)
        return false;
    if (local_slot == 0) {
        return m->sender != 0 && source_ip == udp_protocol_ip(m->sender) &&
               tokens[m->sender] && m->token == tokens[m->sender] &&
               (m->type == UDP_HELLO || m->type == UDP_INPUT ||
                m->type == UDP_PING || m->type == UDP_PONG ||
                m->type == UDP_BENCH_ERROR);
    }
    return source_ip == udp_protocol_ip(0) && m->target == local_slot &&
           tokens[local_slot] && m->token == tokens[local_slot] &&
           (m->type == UDP_WELCOME || m->type == UDP_STATE ||
            m->type == UDP_PING || m->type == UDP_PONG ||
            m->type == UDP_BENCH_ERROR);
}

bool udp_protocol_relay(udp_message_t *m, uint64_t token, uint32_t epoch) {
    if (!valid_message(m) || !token || !epoch || !m->target ||
        (m->type != UDP_PING && m->type != UDP_PONG && m->type != UDP_BENCH_ERROR))
        return false;
    m->token = token;
    m->epoch = epoch;
    return true;
}

uint32_t udp_peer_hello(udp_peer_t *p, uint32_t nonce, uint32_t challenge,
                        uint32_t now_ms) {
    if (!p || !nonce || !challenge) return 0;
    if (p->epoch && p->nonce == nonce) return p->epoch;
    if (p->pending_nonce != nonce || !p->pending_epoch ||
        now_ms - p->pending_ms >= UDP_HANDSHAKE_MS) {
        p->pending_nonce = nonce;
        p->pending_epoch = challenge;
    }
    p->pending_ms = now_ms;
    return p->pending_epoch;
}

bool udp_peer_input(udp_peer_t *p, uint32_t epoch, uint32_t sequence,
                    uint32_t now_ms) {
    if (!p || !epoch) return false;
    if (epoch != p->epoch) {
        if (epoch != p->pending_epoch ||
            now_ms - p->pending_ms >= UDP_HANDSHAKE_MS)
            return false;
        p->epoch = epoch;
        p->nonce = p->pending_nonce;
        p->pending_epoch = 0;
        p->pending_nonce = 0;
        p->sequence_valid = false;
    }
    if (p->sequence_valid && !udp_sequence_newer(sequence, p->sequence))
        return false;
    p->sequence = sequence;
    p->sequence_valid = true;
    p->last_seen_ms = now_ms;
    p->online = true;
    return true;
}

bool udp_peer_expire(udp_peer_t *p, uint32_t now_ms) {
    if (p->online && now_ms - p->last_seen_ms >= UDP_PEER_STALE_MS) {
        p->online = false;
        return true;
    }
    return false;
}

bool udp_relay_ping(udp_relay_t *r, const udp_message_t *m, uint32_t now_ms) {
    if (!r || !valid_message(m) || m->type != UDP_PING || !m->sender || !m->target)
        return false;
    if (r->valid && (m->run == r->generation ?
        !udp_sequence_newer(m->sequence, r->sequence) :
        !udp_sequence_newer(m->run, r->generation)))
        return false;
    *r = (udp_relay_t){
        .generation = m->run, .sequence = m->sequence, .stamp_us = m->stamp_us,
        .started_ms = now_ms, .target = m->target, .origin = m->origin,
        .valid = true, .outstanding = true
    };
    return true;
}

bool udp_relay_reply(udp_relay_t *r, const udp_message_t *m, uint32_t now_ms) {
    if (!r || !valid_message(m) || !r->outstanding ||
        (m->type != UDP_PONG && m->type != UDP_BENCH_ERROR) ||
        m->sender != r->target || m->origin != r->origin || m->run != r->generation ||
        m->sequence != r->sequence || m->stamp_us != r->stamp_us ||
        now_ms - r->started_ms >= UDP_BENCH_TIMEOUT_US / 1000u)
        return false;
    r->outstanding = false;
    return true;
}

static int compare_sample(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a;
    uint32_t y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static void finish(udp_benchmark_t *b) {
    b->metrics.running = false;
    b->outstanding = false;
    uint32_t count = b->metrics.received;
    if (!count) return;
    qsort(b->samples, count, sizeof(b->samples[0]), compare_sample);
    b->metrics.min_us = b->samples[0];
    b->metrics.median_us = b->samples[(count * 50u + 99u) / 100u - 1u];
    b->metrics.p95_us = b->samples[(count * 95u + 99u) / 100u - 1u];
    b->metrics.p99_us = b->samples[(count * 99u + 99u) / 100u - 1u];
    b->metrics.max_us = b->samples[count - 1u];
}

void udp_benchmark_start(udp_benchmark_t *b, uint8_t target,
                         uint32_t generation, uint32_t now_us) {
    memset(b, 0, sizeof(*b));
    b->metrics.target = target;
    b->metrics.running = generation != 0 && target < EXP_MAX_PLAYERS;
    b->generation = generation;
    b->next_us = now_us;
}

void udp_benchmark_tick(udp_benchmark_t *b, uint32_t now_us) {
    if (!b->metrics.running) return;
    if (b->outstanding && now_us - b->pending_us >= UDP_BENCH_TIMEOUT_US) {
        ++b->metrics.lost;
        b->outstanding = false;
        b->next_us = now_us + UDP_BENCH_GAP_US;
        if (b->metrics.sent == EXP_BENCH_SAMPLES) finish(b);
    }
}

bool udp_benchmark_due(const udp_benchmark_t *b, uint32_t now_us) {
    return b->metrics.running && !b->outstanding &&
           b->metrics.sent < EXP_BENCH_SAMPLES &&
           now_us - b->next_us < 0x80000000u;
}

void udp_benchmark_sent(udp_benchmark_t *b, uint32_t stamp_us) {
    if (!udp_benchmark_due(b, stamp_us)) return;
    b->pending_sequence = ++b->metrics.sent;
    b->pending_us = stamp_us;
    b->outstanding = true;
}

bool udp_benchmark_echo(udp_benchmark_t *b, const udp_message_t *m,
                        uint8_t local_slot, uint32_t now_us) {
    udp_benchmark_tick(b, now_us);
    if (!valid_message(m) || !b->metrics.running || !b->outstanding ||
        m->type != UDP_PONG || m->sender != b->metrics.target ||
        m->target != local_slot || m->origin != local_slot ||
        m->run != b->generation || m->sequence != b->pending_sequence ||
        m->stamp_us != b->pending_us)
        return false;
    uint32_t elapsed = now_us - b->pending_us;
    b->samples[b->metrics.received++] = elapsed;
    b->metrics.latest_us = elapsed;
    if (b->metrics.received == 1 || elapsed < b->metrics.min_us)
        b->metrics.min_us = elapsed;
    if (elapsed > b->metrics.max_us) b->metrics.max_us = elapsed;
    b->outstanding = false;
    b->next_us = now_us + UDP_BENCH_GAP_US;
    if (b->metrics.sent == EXP_BENCH_SAMPLES) finish(b);
    return true;
}

bool udp_benchmark_remote_error(udp_benchmark_t *b, const udp_message_t *m,
                                uint8_t local_slot, uint32_t now_us) {
    udp_benchmark_tick(b, now_us);
    if (!valid_message(m) || !b->metrics.running || !b->outstanding ||
        m->type != UDP_BENCH_ERROR ||
        (m->sender != b->metrics.target && m->sender != 0) ||
        m->target != local_slot || m->origin != local_slot ||
        m->run != b->generation || m->sequence != b->pending_sequence ||
        m->stamp_us != b->pending_us)
        return false;
    udp_benchmark_local_error(b);
    return true;
}

void udp_benchmark_local_error(udp_benchmark_t *b) {
    if (!b->metrics.running) return;
    ++b->metrics.local_errors;
    finish(b);
}

void udp_benchmark_cancel(udp_benchmark_t *b) {
    if (b->metrics.running) finish(b);
}

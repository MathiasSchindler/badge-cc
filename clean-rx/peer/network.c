#include "network.h"

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8u | p[1]; }
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24u | (uint32_t)p[1] << 16u | be16(p + 2u);
}
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8u); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) {
    put16(p, v >> 16u); put16(p + 2u, v);
}
static uint32_t sum(uint32_t s, const uint8_t *p, uint32_t size) {
    while (size >= 2u) { s += be16(p); p += 2u; size -= 2u; }
    if (size) s += (uint32_t)*p << 8u;
    while (s >> 16u) s = (s & 65535u) + (s >> 16u);
    return s;
}
static uint32_t ip(uint8_t role) { return 0xc0000201u + role; }
static int mac_valid(const uint8_t *p) {
    return !(p[0] & 1u) && (badger_bytes_load_u32(p) | p[4] | p[5]);
}
static int push(clean_network_queue_t *q, const uint8_t *p, uint32_t size) {
    if (q->count == CLEAN_NETWORK_SLOTS) return 0;
    uint32_t at = (q->head + q->count) % CLEAN_NETWORK_SLOTS;
    badger_bytes_copy(q->frame[at], p, size); q->length[at] = size; ++q->count;
    return 1;
}
static void pop(clean_network_queue_t *q) {
    q->head = (q->head + 1u) % CLEAN_NETWORK_SLOTS; --q->count;
}
static void event(clean_network_t *n, uint32_t kind, uint32_t now) {
    clean_network_report_t *r = &n->report;
    r->events[r->event_count++ % CLEAN_NETWORK_EVENTS] =
        (kind << 30u) | ((now - n->started) / 1000u);
}
static int enqueue(clean_network_t *n, const uint8_t *p, uint32_t size) {
    if (!push(&n->tx, p, size)) { ++n->report.tx_full; return 0; }
    if (n->tx.count > n->report.tx_high) n->report.tx_high = n->tx.count;
    return 1;
}
static uint32_t payload_size(uint32_t sequence) {
    return sequence & 1u ? 48u : CLEAN_NETWORK_PAYLOAD_MAX;
}
static uint8_t payload_byte(const uint8_t session[6], uint32_t epoch,
                            uint32_t sequence, uint8_t origin, uint32_t i) {
    return (uint8_t)(i * 29u + session[i % 6u] + sequence + epoch + origin);
}
static void payload(uint8_t *p, uint32_t size, const uint8_t session[6], uint32_t epoch, uint32_t sequence,
                    uint8_t origin, uint8_t reply) {
    static const uint8_t magic[8] = {'U','2','6','B','2','D','A','T'};
    badger_bytes_copy(p, magic, 8u); badger_bytes_copy(p + 8u, session, 6u);
    badger_bytes_store_u32(p + 14u, epoch);
    badger_bytes_store_u32(p + 18u, sequence);
    p[22] = origin; p[23] = reply;
    for (uint32_t i = 24u; i < size; ++i)
        p[i] = payload_byte(session, epoch, sequence, origin, i);
}
static uint32_t hash(const uint8_t *p, uint32_t size) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0u; i < size; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}
static void ethernet(uint8_t *p, clean_network_t *n, uint32_t type) {
    badger_bytes_copy(p, n->report.peer, 6u);
    badger_bytes_copy(p + 6u, n->report.mac, 6u); put16(p + 12u, type);
}
static void arp(clean_network_t *n, uint8_t p[60], uint8_t response) {
    badger_bytes_zero(p, 60u); ethernet(p, n, 0x806u);
    if (!response) for (uint32_t i = 0u; i < 6u; ++i) p[i] = 255u;
    put16(p + 14u, 1u); put16(p + 16u, 0x800u);
    p[18] = 6u; p[19] = 4u; put16(p + 20u, response ? 2u : 1u);
    badger_bytes_copy(p + 22u, n->report.mac, 6u); put32(p + 28u, ip(n->report.role));
    if (response) badger_bytes_copy(p + 32u, n->report.peer, 6u);
    put32(p + 38u, ip(n->report.role ^ 1u));
}
static uint32_t udp(clean_network_t *n, uint8_t *p, uint32_t sequence,
                uint8_t origin, uint8_t response) {
    uint32_t size = payload_size(sequence), length = 42u + size;
    badger_bytes_zero(p, length); ethernet(p, n, 0x800u);
    uint8_t *v = p + 14u, *u = v + 20u;
    v[0] = 0x45u; put16(v + 2u, 28u + size); put16(v + 4u, sequence);
    put16(v + 6u, 0x4000u); v[8] = 64u; v[9] = 17u;
    put32(v + 12u, ip(n->report.role)); put32(v + 16u, ip(n->report.role ^ 1u));
    put16(v + 10u, ~sum(0u, v, 20u));
    put16(u, CLEAN_NETWORK_PORT); put16(u + 2u, CLEAN_NETWORK_PORT); put16(u + 4u, 8u + size);
    payload(u + 8u, size, n->report.session, n->report.epoch, sequence, origin, response);
    uint32_t c = (~sum(sum(25u + size, v + 12u, 8u), u, 8u + size)) & 65535u;
    put16(u + 6u, c ? c : 65535u);
    return length;
}
void clean_network_init(clean_network_t *n, uint8_t role, const uint8_t mac[6],
                        const uint8_t peer[6], const uint8_t session[6],
                        uint32_t epoch, uint32_t seconds, uint32_t now) {
    badger_bytes_zero((uint8_t *)n, sizeof(*n));
    n->report.magic = CLEAN_NETWORK_MAGIC; n->report.role = role; n->report.seconds = seconds;
    n->report.metrics_magic = CLEAN_NETWORK_METRICS_MAGIC;
    if (role > 1u || !mac_valid(mac) || seconds < 10u || seconds > 3900u ||
        (role && (!mac_valid(peer) || !epoch))) { n->report.error = 1u; return; }
    badger_bytes_copy(n->report.mac, mac, 6u); badger_bytes_copy(n->report.peer, peer, 6u);
    badger_bytes_copy(n->report.session, session, 6u);
    n->report.epoch = epoch; n->report.bound = role;
    n->started = n->last_service = now; n->arp_at = now - 500000u;
    n->report.state = 1u;
}
void clean_network_reset(clean_network_t *n, uint32_t epoch, uint32_t now) {
    n->report.abandoned += !!n->pending + n->tx.count + n->rx.count;
    n->tx.count = n->tx.head = n->rx.count = n->rx.head = 0u;
    n->pending = n->attempts = n->peer_sequence = n->sequence = 0u;
    n->report.epoch = epoch; n->neighbor = 0u;
    n->last_service = now; n->arp_at = now - 500000u; n->send_at = now - 200000u;
}
int clean_network_input(clean_network_t *n, const uint8_t *p, uint32_t size) {
    if (size < 14u || size > CLEAN_NETWORK_MTU) { ++n->report.corrupt; return 0; }
    if (!push(&n->rx, p, size)) { ++n->report.rx_full; return 0; }
    if (n->rx.count > n->report.rx_high) n->report.rx_high = n->rx.count;
    return 1;
}
static int receive(clean_network_t *n, const uint8_t *p, uint32_t size, uint32_t now) {
    clean_network_report_t *r = &n->report;
    uint32_t type = be16(p + 12u);
    if (type != 0x806u && type != 0x800u) return 1;
    if (!mac_valid(p + 6u) ||
        (mac_valid(r->peer) && !badger_bytes_equal(p + 6u, r->peer, 6u))) return 1;
    uint8_t broadcast[6] = {255u,255u,255u,255u,255u,255u};
    if (!badger_bytes_equal(p, r->mac, 6u) && !badger_bytes_equal(p, broadcast, 6u)) return 1;
    if (type == 0x806u) {
        if (size < 42u || be16(p + 14u) != 1u || be16(p + 16u) != 0x800u ||
            p[18] != 6u || p[19] != 4u || (be16(p + 20u) != 1u && be16(p + 20u) != 2u) ||
            !badger_bytes_equal(p + 6u, p + 22u, 6u) ||
            be32(p + 28u) != ip(r->role ^ 1u) || be32(p + 38u) != ip(r->role) ||
            (be16(p + 20u) == 2u && !badger_bytes_equal(p + 32u, r->mac, 6u))) {
            ++r->corrupt; return 1;
        }
        if (be16(p + 20u) == 1u && n->tx.count == CLEAN_NETWORK_SLOTS) {
            ++r->tx_full; return 0;
        }
        badger_bytes_copy(r->peer, p + 6u, 6u); n->neighbor = 1u; ++r->arp_rx;
        if (be16(p + 20u) == 1u) {
            uint8_t reply[60]; arp(n, reply, 1u);
            if (enqueue(n, reply, sizeof(reply))) ++r->arp_tx;
        }
        return 1;
    }
    const uint8_t *v = p + 14u, *u = p + 34u, *body = p + 42u;
    if ((size != 90u && size != CLEAN_NETWORK_MTU) ||
        v[0] != 0x45u || be16(v + 2u) != size - 14u ||
        (be16(v + 6u) & ~0x4000u) || !v[8] || v[9] != 17u ||
        be32(v + 12u) != ip(r->role ^ 1u) || be32(v + 16u) != ip(r->role) ||
        sum(0u, v, 20u) != 65535u || be16(u) != CLEAN_NETWORK_PORT ||
        be16(u + 2u) != CLEAN_NETWORK_PORT || be16(u + 4u) != size - 34u ||
        !be16(u + 6u) || sum(sum(17u + size - 34u, v + 12u, 8u), u, size - 34u) != 65535u) {
        ++r->corrupt; return 1;
    }
    uint32_t epoch = badger_bytes_load_u32(body + 14u), seq = badger_bytes_load_u32(body + 18u);
    if (!epoch || !seq || payload_size(seq) != size - 42u || body[22] > 1u || body[23] > 1u ||
        body[22] != (uint8_t)(r->role ^ !body[23])) { ++r->corrupt; return 1; }
    uint8_t expected[24];
    payload(expected, sizeof(expected), body + 8u, epoch, seq, body[22], body[23]);
    if (!badger_bytes_equal(body, expected, sizeof(expected))) { ++r->corrupt; return 1; }
    for (uint32_t i = sizeof(expected); i < size - 42u; ++i)
        if (body[i] != payload_byte(body + 8u, epoch, seq, body[22], i)) {
            ++r->corrupt; return 1;
        }
    if (!r->role && n->neighbor && !body[23] &&
        (!r->bound || (epoch > r->epoch && badger_bytes_equal(r->session, body + 8u, 6u)))) {
        if (r->bound) {
            r->abandoned += !!n->pending + n->tx.count;
            n->pending = n->sequence = n->peer_sequence = n->tx.count = n->tx.head = 0u;
        } else n->started = now;
        r->bound = 1u; r->epoch = epoch;
        badger_bytes_copy(r->session, body + 8u, 6u);
        n->send_at = now;
    }
    if (!r->bound || epoch != r->epoch || !badger_bytes_equal(r->session, body + 8u, 6u)) {
        ++r->stale; return 1;
    }
    if (body[23]) {
        if (n->pending && seq == n->pending) {
            uint32_t latency = now - n->send_at;
            if (latency > r->latency_max_us) r->latency_max_us = latency;
            ++r->ack_attempts[n->attempts - 1u];
            uint32_t before = r->latency_total_low;
            r->latency_total_low += latency;
            r->latency_total_high += r->latency_total_low < before;
            ++r->latency_histogram[latency <= 10000u ? 0u : latency <= 250000u ? 1u :
                                    latency <= 1000000u ? 2u : 3u];
            ++r->latency_samples;
            if (!(seq & 1u)) ++r->large_acknowledged;
            n->pending = 0u; ++r->acknowledged;
        } else ++r->stale;
        return 1;
    }
    if (n->tx.count == CLEAN_NETWORK_SLOTS) { ++r->tx_full; return 0; }
    uint8_t *reply = n->scratch;
    uint32_t length = udp(n, reply, seq, body[22], 1u);
    if (!enqueue(n, reply, length)) return 0;
    ++r->replies;
    if (seq > n->peer_sequence) {
        n->peer_sequence = seq; ++r->delivered; r->rx_hash += hash(body, size - 42u);
        if (!(seq & 1u)) ++r->large_delivered;
    } else ++r->duplicates;
    return 1;
}
void clean_network_step(clean_network_t *n, uint32_t now) {
    clean_network_report_t *r = &n->report;
    if (r->error || r->state != 1u) return;
    uint32_t gap = now - n->last_service;
    if (gap > r->service_max_us) r->service_max_us = gap;
    if (gap >= 250000u && r->bound) { ++r->service_gaps; event(n, 2u, now); }
    n->last_service = now; r->elapsed_us = now - n->started;
    if (n->rx.count && receive(n, n->rx.frame[n->rx.head], n->rx.length[n->rx.head], now)) {
        pop(&n->rx); ++r->rx_packets;
    }
    if (!n->neighbor && now - n->arp_at >= 500000u && r->role) {
        uint8_t request[60]; arp(n, request, 0u);
        if (enqueue(n, request, sizeof(request))) { ++r->arp_tx; n->arp_at = now; }
    }
    if (!r->bound || !n->neighbor) return;
    if (n->pending && now - n->pending_at >= 250000u) {
        if (n->attempts == 4u) { ++r->lost; n->pending = 0u; }
        else {
            uint8_t *request = n->scratch;
            uint32_t length = udp(n, request, n->pending, r->role, 0u);
            if (enqueue(n, request, length)) {
                ++n->attempts; ++r->retries; n->pending_at = now;
                event(n, 1u, now);
            }
        }
    }
    uint8_t idle = r->elapsed_us % 20000000u >= 15000000u;
    if (idle && !n->idle) ++r->idle_periods;
    n->idle = idle;
    uint32_t drain = r->role ? 3000000u : 4000000u;
    if (!n->pending && !idle && r->elapsed_us < r->seconds * 1000000u - drain &&
        now - n->send_at >= 200000u) {
        uint8_t *request = n->scratch;
        uint32_t length = udp(n, request, n->sequence + 1u, r->role, 0u);
        if (enqueue(n, request, length)) {
            n->pending = ++n->sequence; n->attempts = 1u; n->pending_at = n->send_at = now;
            ++r->generated; r->tx_hash += hash(request + 42u, length - 42u);
            if (!(n->sequence & 1u)) ++r->large_generated;
        }
    }
}
const uint8_t *clean_network_output(const clean_network_t *n, uint32_t *size) {
    *size = n->tx.count ? n->tx.length[n->tx.head] : 0u;
    return n->tx.count ? n->tx.frame[n->tx.head] : 0;
}
void clean_network_sent(clean_network_t *n) {
    if (!n->tx.count) { n->report.error = 2u; return; }
    pop(&n->tx); ++n->report.tx_packets;
}
void clean_network_radio_timeout(clean_network_t *n, uint32_t now) {
    ++n->report.radio_timeouts;
    n->report.elapsed_us=now-n->started;
    event(n,3u,now);
}
int clean_network_valid(const clean_network_report_t *r) {
    return r->magic == CLEAN_NETWORK_MAGIC && r->state == 2u && !r->error &&
        r->bound && r->generated && r->generated == r->acknowledged && r->delivered &&
        r->large_generated && r->large_generated == r->large_acknowledged &&
        r->large_delivered && r->latency_samples == r->acknowledged &&
        !r->lost && !r->corrupt && !r->abandoned && !r->rx_full &&
        r->tx_high <= CLEAN_NETWORK_SLOTS && r->rx_high <= CLEAN_NETWORK_SLOTS;
}

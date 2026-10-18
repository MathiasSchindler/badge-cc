#include "block_ack.h"
#include "../../src/shared/bytes.h"

static uint16_t load16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static void store16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}

static uint16_t distance(uint16_t sequence, uint16_t start) {
    return (uint16_t)((sequence - start) & 4095u);
}

static int addresses(const clean_ba_rx_t *s, const uint8_t *m) {
    return badger_bytes_equal(m + 4u, s->own, 6u) &&
           badger_bytes_equal(m + 10u, s->peer, 6u);
}

static void finish(uint8_t *frame, uint32_t bytes) {
    badger_bytes_store_u32(frame + bytes - 4u, clean_mac_fcs(frame, bytes - 4u));
}

void clean_ba_init(clean_ba_rx_t *s, const uint8_t own[6], const uint8_t peer[6]) {
    badger_bytes_zero((uint8_t *)s, sizeof(*s));
    badger_bytes_copy(s->own, own, 6u);
    badger_bytes_copy(s->peer, peer, 6u);
}

int clean_ba_close(clean_ba_rx_t *s, uint32_t tid) {
    if (tid >= CLEAN_BA_TIDS) return CLEAN_BA_INVALID;
    for (uint32_t i = 0u; i < CLEAN_BA_SLOTS; ++i)
        if (s->slot[i].used && s->slot[i].tid == tid)
            badger_bytes_zero((uint8_t *)&s->slot[i], sizeof(s->slot[i]));
    badger_bytes_zero((uint8_t *)&s->tid[tid], sizeof(s->tid[tid]));
    return CLEAN_BA_ACCEPTED;
}

int clean_ba_action(clean_ba_rx_t *s, const uint8_t *m, uint32_t n) {
    if (!m || n < 30u || !clean_mac_valid(m, n)) return CLEAN_BA_INVALID;
    if (m[0] != 0xd0u || (m[1] & ~8u) || !addresses(s, m) ||
        !badger_bytes_equal(m + 16u, s->peer, 6u) || (m[22] & 15u) || m[24] != 3u)
        return CLEAN_BA_IGNORED;
    if (m[25] == 2u) {
        if (n != 34u) return CLEAN_BA_INVALID;
        uint16_t parameters = load16(m + 26u);
        if (parameters & 0x07ffu) return CLEAN_BA_INVALID;
        /* Initiator=0 tears down our TX agreement, not this RX agreement. */
        if (!(parameters & 0x0800u)) return CLEAN_BA_IGNORED;
        uint32_t tid = parameters >> 12u;
        if (s->tid[tid].active) {
            /* Poll flushes admitted MPDUs before releasing agreement storage. */
            s->tid[tid].closing = 1u;
            return CLEAN_BA_ACCEPTED;
        }
        return clean_ba_close(s, tid);
    }
    if (m[25] != 0u) return CLEAN_BA_IGNORED;
    if (n != 37u) return CLEAN_BA_INVALID;
    uint16_t parameters = load16(m + 27u), start = load16(m + 31u);
    uint32_t tid = (parameters >> 2u) & 15u, window = parameters >> 6u;
    if (!(parameters & 2u) || (start & 15u)) return CLEAN_BA_INVALID;
    clean_ba_tid_t *t = &s->tid[tid];
    uint16_t timeout = load16(m + 29u);
    uint8_t negotiated = (uint8_t)(!window || window > CLEAN_BA_WINDOW ?
                                   CLEAN_BA_WINDOW : window);
    if (t->pending || t->active) {
        int matches = !t->closing && t->dialog == m[26] &&
            t->pending_start == (start >> 4u) &&
            t->window == negotiated && t->timeout_tu == timeout;
        return matches ? (t->active ? CLEAN_BA_ACCEPTED : CLEAN_BA_PREPARED) :
            CLEAN_BA_CONFLICT;
    }
    t->pending = 1u;
    t->dialog = m[26];
    t->pending_start = start >> 4u;
    t->window = negotiated;
    t->timeout_tu = timeout;
    t->timeout_us = (uint32_t)timeout * 1024u;
    /* Do not negotiate A-MSDU within A-MPDU in this bounded recipient. */
    t->amsdu = 0u;
    return CLEAN_BA_PREPARED;
}

int clean_ba_activate(clean_ba_rx_t *s, uint32_t tid, uint32_t now) {
    if (tid >= CLEAN_BA_TIDS || !s->tid[tid].pending || s->tid[tid].active)
        return CLEAN_BA_INVALID;
    clean_ba_tid_t *t = &s->tid[tid];
    t->head = t->bitmap_start = t->pending_start;
    t->last_rx = now;
    t->pending = 0u;
    t->active = 1u;
    return CLEAN_BA_ACCEPTED;
}

uint32_t clean_ba_addba_response(const clean_ba_rx_t *s, uint32_t tid,
                                uint16_t sequence, uint8_t response[37]) {
    if (tid >= CLEAN_BA_TIDS || sequence > 4095u ||
        !s->tid[tid].active || s->tid[tid].closing) return 0u;
    const clean_ba_tid_t *t = &s->tid[tid];
    badger_bytes_zero(response, 37u);
    response[0] = 0xd0u;
    badger_bytes_copy(response + 4u, s->peer, 6u);
    badger_bytes_copy(response + 10u, s->own, 6u);
    badger_bytes_copy(response + 16u, s->peer, 6u);
    store16(response + 22u, (uint16_t)(sequence << 4u));
    response[24] = 3u;
    response[25] = 1u;
    response[26] = t->dialog;
    store16(response + 29u, (uint16_t)(2u | (tid << 2u) | ((uint32_t)t->window << 6u)));
    store16(response + 31u, t->timeout_tu);
    finish(response, 37u);
    return 37u;
}

static clean_ba_slot_t *find(clean_ba_rx_t *s, uint32_t tid, uint16_t sequence) {
    for (uint32_t i = 0u; i < CLEAN_BA_SLOTS; ++i)
        if (s->slot[i].used && s->slot[i].tid == tid && s->slot[i].sequence == sequence)
            return &s->slot[i];
    return 0;
}

static int release(clean_ba_rx_t *s, uint32_t tid, uint16_t until, uint32_t now,
                    clean_ba_sink_t sink, void *context) {
    clean_ba_tid_t *t = &s->tid[tid];
    while (t->head != until) {
        clean_ba_slot_t *slot = find(s, tid, t->head);
        if (slot) {
            int result = sink(context, slot->mpdu, slot->length, now);
            if (result <= 0) return result < 0 ? CLEAN_BA_SINK_ERROR : CLEAN_BA_BUSY;
            badger_bytes_zero((uint8_t *)slot, sizeof(*slot));
            ++s->delivered;
        } else ++s->skipped;
        t->head = (uint16_t)((t->head + 1u) & 4095u);
    }
    return CLEAN_BA_ACCEPTED;
}

static int drain(clean_ba_rx_t *s, uint32_t tid, uint32_t now,
                  clean_ba_sink_t sink, void *context) {
    while (find(s, tid, s->tid[tid].head)) {
        int result = release(s, tid, (uint16_t)((s->tid[tid].head + 1u) & 4095u), now,
                             sink, context);
        if (result != CLEAN_BA_ACCEPTED) return result;
    }
    return CLEAN_BA_ACCEPTED;
}

static void bitmap_advance(clean_ba_tid_t *t, uint16_t start) {
    uint16_t shift = distance(start, t->bitmap_start);
    if (shift >= 2048u) return;
    for (uint32_t bit = 0u; bit < 64u; ++bit) {
        uint32_t source = bit + shift;
        uint8_t value = source < 64u ?
            (uint8_t)((t->bitmap[source / 8u] >> (source % 8u)) & 1u) : 0u;
        uint8_t mask = (uint8_t)(1u << (bit % 8u));
        t->bitmap[bit / 8u] = (uint8_t)((t->bitmap[bit / 8u] & ~mask) |
                                      (value ? mask : 0u));
    }
    t->bitmap_start = start;
}

static void received(clean_ba_tid_t *t, uint16_t sequence) {
    uint16_t bit = distance(sequence, t->bitmap_start);
    if (bit >= 64u) {
        bitmap_advance(t, (uint16_t)((sequence - 63u) & 4095u));
        bit = distance(sequence, t->bitmap_start);
    }
    if (bit < 64u) t->bitmap[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
}

static int check_timeout(clean_ba_rx_t *s, uint32_t tid, uint32_t now,
                          clean_ba_sink_t sink, void *context) {
    const clean_ba_tid_t *t = &s->tid[tid];
    if (t->timeout_us && now - t->last_rx >= t->timeout_us) {
        int result = clean_ba_poll(s, now, sink, context);
        return result == CLEAN_BA_ACCEPTED ? CLEAN_BA_NO_AGREEMENT : result;
    }
    return CLEAN_BA_ACCEPTED;
}

int clean_ba_receive(clean_ba_rx_t *s, const uint8_t *m, uint32_t n,
                      uint32_t now, clean_ba_sink_t sink, void *context) {
    if (!sink || !m || n < 30u || !clean_mac_valid(m, n)) return CLEAN_BA_INVALID;
    if (m[0] != 0x88u || (m[1] & 0x87u) != 2u || !addresses(s, m) ||
        (m[22] & 15u) || (m[24] & 0x80u) || m[25] ||
        ((m[24] & 0x60u) != 0u && (m[24] & 0x60u) != 0x60u))
        return CLEAN_BA_INVALID;
    uint32_t tid = m[24] & 15u;
    clean_ba_tid_t *t = &s->tid[tid];
    if (!t->active || t->closing) return CLEAN_BA_NO_AGREEMENT;
    int timeout_result = check_timeout(s, tid, now, sink, context);
    if (timeout_result != CLEAN_BA_ACCEPTED) return timeout_result;
    uint16_t sequence = load16(m + 22u) >> 4u;
    uint16_t bit = distance(sequence, t->bitmap_start);
    if ((bit < 64u && (t->bitmap[bit / 8u] & (1u << (bit % 8u)))) ||
        find(s, tid, sequence)) {
        ++s->duplicates;
        t->last_rx = now;
        return CLEAN_BA_DUPLICATE;
    }
    uint16_t delta = distance(sequence, t->head);
    if (delta >= 2048u) { ++s->old; return CLEAN_BA_OLD; }
    if (delta >= t->window) {
        int result = release(s, tid, (uint16_t)((sequence - t->window + 1u) & 4095u), now,
                             sink, context);
        if (result != CLEAN_BA_ACCEPTED) { ++s->pressure; return result; }
    }
    clean_ba_slot_t *slot = 0;
    for (uint32_t i = 0u; i < CLEAN_BA_SLOTS; ++i)
        if (!s->slot[i].used) { slot = &s->slot[i]; break; }
    if (!slot) { ++s->pressure; return CLEAN_BA_BUSY; }
    badger_bytes_copy(slot->mpdu, m, n);
    slot->sequence = sequence;
    slot->length = (uint16_t)n;
    slot->tid = (uint8_t)tid;
    slot->received_us = now;
    slot->used = 1u;
    received(t, sequence);
    t->last_rx = now;
    ++s->accepted;
    /* An admitted frame remains acknowledged even if the delivery sink stalls. */
    int result = drain(s, tid, now, sink, context);
    return result == CLEAN_BA_SINK_ERROR ? result : CLEAN_BA_ACCEPTED;
}

int clean_ba_poll(clean_ba_rx_t *s, uint32_t now, clean_ba_sink_t sink, void *context) {
    if (!sink) return CLEAN_BA_INVALID;
    for (uint32_t tid = 0u; tid < CLEAN_BA_TIDS; ++tid) {
        clean_ba_tid_t *t = &s->tid[tid];
        if (!t->active) continue;
        int result = drain(s, tid, now, sink, context);
        if (result != CLEAN_BA_ACCEPTED) return result;
        uint16_t furthest = 0u;
        for (uint32_t i = 0u; i < CLEAN_BA_SLOTS; ++i) {
            const clean_ba_slot_t *slot = &s->slot[i];
            if (slot->used && slot->tid == tid && now - slot->received_us >= CLEAN_BA_GAP_US) {
                uint16_t delta = distance(slot->sequence, t->head);
                if (delta < 2048u && delta + 1u > furthest) furthest = (uint16_t)(delta + 1u);
            }
        }
        int expired = t->timeout_us && now - t->last_rx >= t->timeout_us;
        if (expired || t->closing) {
            /* Flush already admitted MPDUs before retiring the agreement. */
            for (uint32_t i = 0u; i < CLEAN_BA_SLOTS; ++i)
                if (s->slot[i].used && s->slot[i].tid == tid) {
                    uint16_t delta = distance(s->slot[i].sequence, t->head);
                    if (delta < 2048u && delta + 1u > furthest) furthest = (uint16_t)(delta + 1u);
                }
        }
        if (furthest) {
            result = release(s, tid, (uint16_t)((t->head + furthest) & 4095u), now, sink, context);
            if (result != CLEAN_BA_ACCEPTED) return result;
            result = drain(s, tid, now, sink, context);
            if (result != CLEAN_BA_ACCEPTED) return result;
        }
        if (expired || t->closing) {
            (void)clean_ba_close(s, tid);
            if (expired) ++s->expired;
        }
    }
    return CLEAN_BA_ACCEPTED;
}

int clean_ba_bar(clean_ba_rx_t *s, const uint8_t *m, uint32_t n,
                  uint32_t now, clean_ba_sink_t sink, void *context) {
    if (!sink || !m || n != 24u || !clean_mac_valid(m, n) ||
        m[0] != 0x84u || m[1] || !addresses(s, m)) return CLEAN_BA_INVALID;
    uint16_t control = load16(m + 16u), start = load16(m + 18u);
    if ((control & 0x0fffu) != 4u || (start & 15u)) return CLEAN_BA_INVALID;
    uint32_t tid = control >> 12u;
    clean_ba_tid_t *t = &s->tid[tid];
    if (!t->active || t->closing) return CLEAN_BA_NO_AGREEMENT;
    int timeout_result = check_timeout(s, tid, now, sink, context);
    if (timeout_result != CLEAN_BA_ACCEPTED) return timeout_result;
    start >>= 4u;
    if (distance(start, t->head) < 2048u) {
        int result = release(s, tid, start, now, sink, context);
        if (result != CLEAN_BA_ACCEPTED) return result;
    }
    bitmap_advance(t, start);
    t->last_rx = now;
    return drain(s, tid, now, sink, context);
}

uint32_t clean_ba_response(const clean_ba_rx_t *s, uint32_t tid, uint8_t response[32]) {
    if (tid >= CLEAN_BA_TIDS || !s->tid[tid].active || s->tid[tid].closing) return 0u;
    badger_bytes_zero(response, 32u);
    response[0] = 0x94u;
    badger_bytes_copy(response + 4u, s->peer, 6u);
    badger_bytes_copy(response + 10u, s->own, 6u);
    store16(response + 16u, (uint16_t)(4u | (tid << 12u)));
    store16(response + 18u, (uint16_t)(s->tid[tid].bitmap_start << 4u));
    badger_bytes_copy(response + 20u, s->tid[tid].bitmap, 8u);
    finish(response, 32u);
    return 32u;
}

uint8_t clean_ampdu_delimiter_crc(const uint8_t bytes[2]) {
    uint8_t crc = 255u;
    for (uint32_t i = 0u; i < 2u; ++i)
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            uint8_t feedback = (uint8_t)((crc ^ (bytes[i] >> bit)) & 1u);
            crc = (uint8_t)((crc >> 1u) ^ (feedback ? 0xe0u : 0u));
        }
    return (uint8_t)~crc;
}

int clean_ampdu_begin(clean_ampdu_stream_t *s, uint32_t bytes) {
    badger_bytes_zero((uint8_t *)s, sizeof(*s));
    if (bytes < 4u || bytes > CLEAN_AMPDU_BYTES) {
        s->failed = 1u;
        return CLEAN_BA_INVALID;
    }
    s->expected = bytes;
    return CLEAN_BA_ACCEPTED;
}

static int stream_error(clean_ampdu_stream_t *s) {
    s->failed = 1u;
    return CLEAN_BA_INVALID;
}

int clean_ampdu_feed(clean_ampdu_stream_t *s, const uint8_t *bytes, uint32_t n,
                      uint32_t *used, clean_ba_sink_t sink, void *context, uint32_t now) {
    if (!used) return CLEAN_BA_INVALID;
    *used = 0u;
    if (s->failed || !s->expected || !sink || (!bytes && n) ||
        n > s->expected - s->consumed) return stream_error(s);
    while (*used < n || (s->length && s->have == s->length)) {
        if (s->length && s->have == s->length) {
            if (clean_mac_valid(s->mpdu, s->length)) {
                int result = sink(context, s->mpdu, s->length, now);
                if (!result) return CLEAN_BA_BUSY;
                if (result < 0) { s->failed = 1u; return CLEAN_BA_SINK_ERROR; }
                ++s->frames;
            } else ++s->bad_fcs;
            s->padding = (uint8_t)((4u - (s->length & 3u)) & 3u);
            if (s->consumed == s->expected) s->padding = 0u;
            s->length = s->have = 0u;
            continue;
        }
        uint8_t byte = bytes[(*used)++];
        ++s->consumed;
        if (s->padding) { --s->padding; continue; }
        if (s->length) { s->mpdu[s->have++] = byte; continue; }
        s->delimiter[s->delimiter_bytes++] = byte;
        if (s->delimiter_bytes != 4u) continue;
        s->delimiter_bytes = 0u;
        if ((s->delimiter[0] & 15u) || s->delimiter[3] != 0x4eu ||
            clean_ampdu_delimiter_crc(s->delimiter) != s->delimiter[2])
            return stream_error(s);
        s->length = load16(s->delimiter) >> 4u;
        if (!s->length) { ++s->null_delimiters; continue; }
        if (s->length < 14u || s->length > CLEAN_MPDU_BYTES ||
            s->length > s->expected - s->consumed) return stream_error(s);
    }
    return CLEAN_BA_ACCEPTED;
}

int clean_ampdu_end(const clean_ampdu_stream_t *s) {
    return !s->failed && s->expected && s->consumed == s->expected &&
        !s->length && !s->have && !s->padding && !s->delimiter_bytes ?
        CLEAN_BA_ACCEPTED : CLEAN_BA_INVALID;
}

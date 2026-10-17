#include "mac.h"

static uint16_t u16(const volatile uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint16_t difs(const clean_dcf_t *s) {
    return (uint16_t)(10u + s->aifsn * s->slot_us);
}

static void draw(clean_dcf_t *s, uint32_t now) {
    uint32_t x = s->random;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    s->random = x;
    s->slots = (uint16_t)(x & s->cw);
    s->sampled_us = now;
    s->idle_us = 0u;
    s->idle = 0u;
    s->ifs_us = difs(s);
}

void clean_dcf_init(clean_dcf_t *s, uint32_t now, uint32_t seed, uint8_t short_slot) {
    s->random = seed ? seed : 1u;
    s->slot_us = short_slot ? 9u : 20u;
    s->cw = s->cw_min = 15u; /* ERP/OFDM; DSSS-only DCF is a separate profile. */
    s->cw_max = CLEAN_MAC_CW_MAX;
    s->aifsn = 2u;
    s->nav_valid = 0u;
    s->nav_until = now;
    draw(s, now);
}

void clean_dcf_nav(clean_dcf_t *s, uint32_t now, uint16_t duration) {
    if (!duration || (duration & 0x8000u)) return;
    uint32_t remaining = s->nav_until - now;
    if (!s->nav_valid || remaining >= 0x80000000u || duration > remaining)
        s->nav_until = now + duration;
    s->nav_valid = 1u;
    s->idle = 0u;
    s->idle_us = 0u;
}

void clean_dcf_bad_rx(clean_dcf_t *s, uint32_t now) {
    s->ifs_us = (uint16_t)(difs(s) + 10u + 304u);
    s->sampled_us = now;
    s->idle = 0u;
    s->idle_us = 0u;
}

void clean_dcf_good_rx(clean_dcf_t *s) {
    s->ifs_us = difs(s);
}

int clean_dcf_poll(clean_dcf_t *s, uint32_t now, int busy) {
    uint32_t elapsed = now - s->sampled_us;
    s->sampled_us = now;
    if (s->nav_valid) {
        uint32_t remaining = s->nav_until - now;
        if (remaining && remaining < 0x80000000u) busy = 1;
        else s->nav_valid = 0u;
    }
    /* Never count a long, unobserved interval as idle radio time. */
    if (busy || !s->idle || elapsed > s->slot_us) {
        s->idle = (uint8_t)!busy;
        s->idle_us = 0u;
        return 0;
    }
    s->idle_us += elapsed;
    if (s->idle_us < s->ifs_us) return 0;
    uint32_t slots = (s->idle_us - s->ifs_us) / s->slot_us;
    if (slots >= s->slots) {
        s->slots = 0u;
        s->idle_us = s->ifs_us;
        return 1;
    }
    s->slots = (uint16_t)(s->slots - slots);
    s->idle_us -= slots * s->slot_us;
    return 0;
}

void clean_dcf_complete(clean_dcf_t *s, uint32_t now, int acked) {
    s->cw = acked ? s->cw_min :
        (s->cw >= s->cw_max ? s->cw_max : (uint16_t)(2u * s->cw + 1u));
    draw(s, now);
}

void clean_dcf_new_frame(clean_dcf_t *s, uint32_t now) {
    if (s->cw == s->cw_min) return;
    uint16_t ifs = s->ifs_us;
    s->cw = s->cw_min;
    draw(s, now);
    s->ifs_us = ifs;
}

void clean_dcf_slot(clean_dcf_t *s, uint32_t now, uint8_t short_slot) {
    uint8_t slot = short_slot ? 9u : 20u;
    if (s->slot_us == slot) return;
    uint16_t extra = (uint16_t)(s->ifs_us - difs(s));
    s->slot_us = slot;
    s->ifs_us = (uint16_t)(difs(s) + extra);
    s->sampled_us = now;
    s->idle = 0u;
    s->idle_us = 0u;
}

int clean_edca_decode(clean_edca_ac_t ac[4], const volatile uint8_t bytes[16]) {
    clean_edca_ac_t decoded[4];
    uint32_t seen = 0u;
    for (uint32_t i = 0u; i < 4u; ++i) {
        const volatile uint8_t *p = bytes + 4u*i;
        uint32_t index = (p[0] >> 5u) & 3u, bit = 1u << index;
        if ((seen & bit) || (p[0] & 0x80u) || (p[0] & 15u) < 2u ||
            (p[1] & 15u) > (p[1] >> 4u)) return 0;
        seen |= bit;
        decoded[index].aifsn = p[0] & 15u;
        decoded[index].acm = (p[0] >> 4u) & 1u;
        decoded[index].ecw_min = p[1] & 15u;
        decoded[index].ecw_max = p[1] >> 4u;
        decoded[index].txop_32us = u16(p + 2u);
    }
    for (uint32_t i = 0u; i < 4u; ++i) ac[i] = decoded[i];
    return 1;
}

int clean_edca_valid(const clean_edca_ac_t ac[4]) {
    for (uint32_t i = 0u; i < 4u; ++i)
        if (ac[i].aifsn < 2u || ac[i].aifsn > 15u || ac[i].acm > 1u ||
            ac[i].ecw_min > ac[i].ecw_max || ac[i].ecw_max > 10u) return 0;
    return 1;
}

int clean_edca_configure(clean_edca_t *s, const clean_edca_ac_t ac[4],
                         uint32_t now, uint32_t seed, uint8_t short_slot) {
    if (short_slot > 1u || !clean_edca_valid(ac)) return 0;
    for (uint32_t i = 0u; i < 4u; ++i) {
        s->parameters[i] = ac[i];
        clean_dcf_t *d = &s->ac[i];
        clean_dcf_init(d, now, seed ^ (0x9e3779b9u * (i + 1u)), short_slot);
        d->aifsn = ac[i].aifsn;
        d->cw = d->cw_min = (uint16_t)((1u << ac[i].ecw_min) - 1u);
        d->cw_max = (uint16_t)((1u << ac[i].ecw_max) - 1u);
        draw(d, now);
    }
    s->selected = s->tid = 0u;
    s->enabled = 1u;
    return 1;
}

int clean_edca_select(clean_edca_t *s, uint32_t tid, uint32_t now) {
    static const uint8_t mapping[8] = {0u,1u,1u,0u,2u,2u,3u,3u};
    static const uint8_t lower[4] = {1u,1u,0u,2u};
    if (!s->enabled || tid >= 8u) return -1;
    uint32_t ac = mapping[tid];
    while (s->parameters[ac].acm) {
        if (ac == 1u) return -1;
        ac = lower[ac];
    }
    if (ac != s->selected) {
        s->ac[ac].sampled_us = now;
        s->ac[ac].idle = 0u;
        s->ac[ac].idle_us = 0u;
    }
    s->selected = (uint8_t)ac;
    s->tid = (uint8_t)tid;
    return (int)ac;
}

int clean_edca_txop_valid(const clean_edca_t *s, uint32_t exchange_us) {
    if (!s->enabled || !exchange_us) return 0;
    uint32_t limit = s->parameters[s->selected].txop_32us;
    return !limit || exchange_us <= limit * 32u;
}

uint32_t clean_mac_fcs(const volatile uint8_t *bytes, uint32_t length) {
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0u; i < length; ++i) {
        crc ^= bytes[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
}

int clean_mac_valid(const volatile uint8_t *m, uint32_t n) {
    if (n < 14u || n > CLEAN_MPDU_BYTES) return 0;
    uint32_t fcs = (uint32_t)u16(m + n - 4u) | ((uint32_t)u16(m + n - 2u) << 16u);
    return clean_mac_fcs(m, n - 4u) == fcs;
}

int clean_mac_retry(volatile uint8_t *m, uint32_t n) {
    if (n < 28u || !clean_mac_valid(m, n) || (m[0] & 7u) || (m[0] & 12u) == 12u)
        return 0;
    m[1] |= 8u;
    uint32_t crc = clean_mac_fcs(m, n - 4u);
    for (uint32_t i = 0u; i < 4u; ++i) m[n - 4u + i] = (uint8_t)(crc >> (8u * i));
    return 1;
}

int clean_mac_nav_duration(const volatile uint8_t *m, uint32_t n,
                           const uint8_t own[6]) {
    if (!clean_mac_valid(m, n) || (m[0] & 3u)) return -1;
    uint32_t type = (m[0] >> 2u) & 3u;
    if (type == 3u) return -1;
    if (type == 1u) {
        if (m[0] == 0xb4u) { if (n != 20u) return -1; } /* RTS */
        else if (m[0] == 0xc4u || m[0] == 0xd4u) { if (n != 14u) return -1; }
        else return 0; /* PS-Poll Duration/ID and CF-End do not extend NAV. */
    } else if (n < 28u) return -1;
    uint32_t equal = 1u;
    for (uint32_t i = 0u; i < 6u; ++i) if (m[4u + i] != own[i]) equal = 0u;
    uint16_t duration = u16(m + 2u);
    return equal || (duration & 0x8000u) ? 0 : (int)duration;
}

void clean_mac_rx_init(clean_mac_rx_t *s) {
    uint8_t *p = (uint8_t *)s;
    for (uint32_t i = 0u; i < sizeof(*s); ++i) p[i] = 0u;
}

static int rx_error(clean_mac_rx_t *s, int result) {
    if (result == CLEAN_MAC_INVALID) ++s->malformed;
    else if (result == CLEAN_MAC_UNSUPPORTED) ++s->unsupported;
    else ++s->fragment_errors;
    return result;
}

void clean_mac_rx_expire(clean_mac_rx_t *s, uint32_t now) {
    if (s->active && now - s->started_us >= CLEAN_MAC_RX_LIFETIME_US) {
        clean_mac_history_t *h = &s->history[s->sequence & (CLEAN_MAC_RX_HISTORY - 1u)];
        if (!h->delivered && h->sequence == s->sequence && h->subtype == s->frame[0] &&
            h->protected_frame == (s->frame[1] & 64u)) h->valid = 0u;
        s->active = 0u;
        s->length = 0u;
        for (uint32_t i = 0u; i < sizeof(s->frame); ++i) s->frame[i] = 0u;
        ++s->expired;
    }
}

static int receive(clean_mac_rx_t *s, const uint8_t *m, uint32_t n, uint32_t now,
                    int authenticated, int qos) {
    clean_mac_rx_expire(s, now);
    uint32_t header = qos ? 26u : 24u;
    if (!clean_mac_valid(m, n) || n < 28u || (m[0] & 3u))
        return rx_error(s, CLEAN_MAC_INVALID);
    if (qos ? (m[0] != 0x88u && m[0] != 0xc8u) :
       (m[0] != 0u && m[0] != 0x10u && m[0] != 0x20u && m[0] != 0x30u &&
        m[0] != 0x40u && m[0] != 0x50u && m[0] != 0x80u && m[0] != 0x90u &&
        m[0] != 0xa0u && m[0] != 0xb0u && m[0] != 0xc0u && m[0] != 0xd0u &&
        m[0] != 8u && m[0] != 0x48u))
        return rx_error(s, CLEAN_MAC_UNSUPPORTED);
    if (n < header + 4u) return rx_error(s, CLEAN_MAC_INVALID);
    if ((m[1] & 3u) == 3u || (m[1] & 128u) || (m[4] & 1u))
        return rx_error(s, CLEAN_MAC_UNSUPPORTED);
    if (!(m[0] & 12u) && (m[1] & 3u)) return rx_error(s, CLEAN_MAC_INVALID);
    if ((m[1] & 64u) && !authenticated) return CLEAN_MAC_PROTECTED;
    if (authenticated && (!(m[1] & 64u) || m[0] != (qos ? 0x88u : 8u)))
        return rx_error(s, CLEAN_MAC_INVALID);
    uint16_t seq = (uint16_t)(u16(m + 22u) >> 4u);
    uint8_t frag = m[22] & 15u, more = m[1] & 4u;
    uint32_t payload = n - header - 4u;
    int aggregate = qos && (m[24] & 128u);
    if (qos && ((m[24] & 0x60u) >= 0x40u || (aggregate && authenticated)))
        return rx_error(s, CLEAN_MAC_UNSUPPORTED);
    if ((m[0] == 0x48u || m[0] == 0xc8u) && (payload || frag || more || aggregate))
        return rx_error(s, CLEAN_MAC_INVALID);
    if (aggregate) {
        clean_amsdu_t assembly;
        if (frag || more || !clean_amsdu_open(&assembly, m + header, payload))
            return rx_error(s, CLEAN_MAC_INVALID);
    } else if (payload > CLEAN_MSDU_BYTES || (more && (!payload || frag == 15u)))
        return rx_error(s, CLEAN_MAC_INVALID);
    clean_mac_history_t *h = &s->history[seq & (CLEAN_MAC_RX_HISTORY - 1u)];
    uint8_t protected_frame = m[1] & 64u;
    /* Retry exchanges can outlive the assembly timeout. Delivered identities
     * persist until replaced by another sequence or a fresh, non-Retry frame. */
    if ((m[1] & 8u) && h->valid && h->sequence == seq && h->subtype == m[0] &&
        h->protected_frame == protected_frame &&
        (h->fragments & (1u << frag))) {
        ++s->duplicates;
        return CLEAN_MAC_DUPLICATE;
    }
    if (frag) {
        if (!s->active || s->sequence != seq || s->next_fragment != frag)
            return rx_error(s, CLEAN_MAC_FRAGMENT_ORDER);
        if ((m[0] != s->frame[0]) || ((m[1] ^ s->frame[1]) & 0xc3u))
            return rx_error(s, CLEAN_MAC_FRAGMENT_ORDER);
        if (qos && ((m[24] ^ s->frame[24]) & 0x8fu))
            return rx_error(s, CLEAN_MAC_FRAGMENT_ORDER);
        for (uint32_t i = 4u; i < 22u; ++i)
            if (m[i] != s->frame[i]) return rx_error(s, CLEAN_MAC_FRAGMENT_ORDER);
        if (payload > CLEAN_MSDU_BYTES - (s->length - header))
            return rx_error(s, CLEAN_MAC_INVALID);
        for (uint32_t i = 0u; i < payload; ++i) s->frame[s->length + i] = m[header + i];
        s->length += payload;
    } else {
        if (more && s->active) return rx_error(s, CLEAN_MAC_FRAGMENT_BUSY);
        if (more) {
            for (uint32_t i = 0u; i < n - 4u; ++i) s->frame[i] = m[i];
            s->length = n - 4u;
            s->active = 1u;
            s->sequence = seq;
            s->started_us = now;
        }
    }
    if (!h->valid || h->sequence != seq || h->subtype != m[0] ||
        h->protected_frame != protected_frame || (!frag && !(m[1] & 8u))) {
        h->fragments = 0u;
        h->sequence = seq;
        h->subtype = m[0];
        h->protected_frame = protected_frame;
        h->valid = 1u;
        h->delivered = 0u;
    }
    h->fragments |= (uint16_t)(1u << frag);
    if (more) {
        s->next_fragment = (uint8_t)(frag + 1u);
        ++s->fragments;
        return CLEAN_MAC_FRAGMENT;
    }
    if (frag) {
        s->active = 0u;
        s->frame[1] &= (uint8_t)~12u;
        s->frame[22] &= 0xf0u;
        uint32_t crc = clean_mac_fcs(s->frame, s->length);
        for (uint32_t i = 0u; i < 4u; ++i) s->frame[s->length + i] = (uint8_t)(crc >> (8u * i));
        s->length += 4u;
    }
    h->delivered = 1u;
    ++s->delivered;
    return CLEAN_MAC_DELIVER;
}

int clean_mac_rx(clean_mac_rx_t *s, const uint8_t *m, uint32_t n, uint32_t now) {
    return receive(s, m, n, now, 0, 0);
}

int clean_mac_rx_authenticated(clean_mac_rx_t *s, const uint8_t *m, uint32_t n,
                               uint32_t now) {
    return receive(s, m, n, now, 1, 0);
}

void clean_qos_rx_init(clean_qos_rx_t *s) {
    for (uint32_t i = 0u; i < 16u; ++i) clean_mac_rx_init(&s->tid[i]);
}

void clean_qos_rx_expire(clean_qos_rx_t *s, uint32_t now) {
    for (uint32_t i = 0u; i < 16u; ++i) clean_mac_rx_expire(&s->tid[i], now);
}

static int qos_receive(clean_qos_rx_t *s, const uint8_t *m, uint32_t n,
                        uint32_t now, int authenticated) {
    clean_qos_rx_expire(s, now);
    if (n < 30u || !clean_mac_valid(m, n)) return CLEAN_MAC_INVALID;
    if (m[0] != 0x88u && m[0] != 0xc8u) return CLEAN_MAC_UNSUPPORTED;
    return receive(&s->tid[m[24] & 15u], m, n, now, authenticated, 1);
}

int clean_qos_rx(clean_qos_rx_t *s, const uint8_t *m, uint32_t n, uint32_t now) {
    return qos_receive(s, m, n, now, 0);
}

int clean_qos_rx_authenticated(clean_qos_rx_t *s, const uint8_t *m,
                               uint32_t n, uint32_t now) {
    return qos_receive(s, m, n, now, 1);
}

int clean_amsdu_open(clean_amsdu_t *s, const volatile uint8_t *body, uint32_t length) {
    s->body = 0;
    s->length = s->offset = 0u;
    if (!body || length < 15u || length > CLEAN_AMSDU_BYTES) return 0;
    uint32_t offset = 0u;
    while (offset < length) {
        uint32_t remaining = length - offset;
        if (remaining < 15u) return 0;
        const volatile uint8_t *m = body + offset;
        uint32_t size = ((uint32_t)m[12] << 8u) | m[13];
        if (!size || size > CLEAN_MSDU_BYTES || size > remaining - 14u || (m[6] & 1u))
            return 0;
        /* An RFC1042 LLC header must not masquerade as the first DA. */
        if (m[0] == 0xaau && m[1] == 0xaau && m[2] == 3u &&
            !m[3] && !m[4] && !m[5]) return 0;
        uint32_t used = 14u + size;
        if (used == remaining) break;
        uint32_t padded = (used + 3u) & ~3u;
        if (padded >= remaining) return 0;
        offset += padded;
    }
    s->body = body;
    s->length = length;
    return 1;
}

int clean_amsdu_next(clean_amsdu_t *s, clean_amsdu_subframe_t *subframe) {
    if (s->offset == s->length) return 0;
    const volatile uint8_t *m = s->body + s->offset;
    subframe->destination = m;
    subframe->source = m + 6u;
    subframe->payload = m + 14u;
    subframe->length = (uint16_t)(((uint32_t)m[12] << 8u) | m[13]);
    uint32_t used = 14u + subframe->length;
    s->offset += used == s->length - s->offset ? used : ((used + 3u) & ~3u);
    return 1;
}

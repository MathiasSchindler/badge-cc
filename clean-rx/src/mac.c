#include "mac.h"

static uint16_t u16(const volatile uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint16_t difs(const clean_dcf_t *s) {
    return (uint16_t)(10u + 2u * s->slot_us);
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
        (s->cw >= CLEAN_MAC_CW_MAX ? CLEAN_MAC_CW_MAX : (uint16_t)(2u * s->cw + 1u));
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

static int receive(clean_mac_rx_t *s, const uint8_t *m, uint32_t n, uint32_t now,
                    int authenticated) {
    if (s->active && now - s->started_us >= CLEAN_MAC_RX_LIFETIME_US) {
        clean_mac_history_t *h = &s->history[s->sequence & (CLEAN_MAC_RX_HISTORY - 1u)];
        if (!h->delivered && h->sequence == s->sequence && h->subtype == s->frame[0] &&
            h->protected_frame == (s->frame[1] & 64u)) h->valid = 0u;
        s->active = 0u;
        ++s->expired;
    }
    if (!clean_mac_valid(m, n) || n < 28u || (m[0] & 3u))
        return rx_error(s, CLEAN_MAC_INVALID);
    if (m[0] != 0u && m[0] != 0x10u && m[0] != 0x20u && m[0] != 0x30u &&
        m[0] != 0x40u && m[0] != 0x50u && m[0] != 0x80u && m[0] != 0x90u &&
        m[0] != 0xa0u && m[0] != 0xb0u && m[0] != 0xc0u && m[0] != 0xd0u &&
        m[0] != 8u && m[0] != 0x48u)
        return rx_error(s, CLEAN_MAC_UNSUPPORTED);
    if ((m[1] & 3u) == 3u || (m[1] & 128u) || (m[4] & 1u))
        return rx_error(s, CLEAN_MAC_UNSUPPORTED);
    if (!(m[0] & 12u) && (m[1] & 3u)) return rx_error(s, CLEAN_MAC_INVALID);
    if ((m[1] & 64u) && !authenticated) return CLEAN_MAC_PROTECTED;
    if (authenticated && (!(m[1] & 64u) || m[0] != 8u))
        return rx_error(s, CLEAN_MAC_INVALID);
    uint16_t seq = (uint16_t)(u16(m + 22u) >> 4u);
    uint8_t frag = m[22] & 15u, more = m[1] & 4u;
    uint32_t payload = n - 28u;
    if (m[0] == 0x48u && (payload || frag || more))
        return rx_error(s, CLEAN_MAC_INVALID);
    if (payload > CLEAN_MSDU_BYTES || (more && (!payload || frag == 15u)))
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
        for (uint32_t i = 4u; i < 22u; ++i)
            if (m[i] != s->frame[i]) return rx_error(s, CLEAN_MAC_FRAGMENT_ORDER);
        if (payload > CLEAN_MSDU_BYTES - (s->length - 24u))
            return rx_error(s, CLEAN_MAC_INVALID);
        for (uint32_t i = 0u; i < payload; ++i) s->frame[s->length + i] = m[24u + i];
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
    return receive(s, m, n, now, 0);
}

int clean_mac_rx_authenticated(clean_mac_rx_t *s, const uint8_t *m, uint32_t n,
                               uint32_t now) {
    return receive(s, m, n, now, 1);
}

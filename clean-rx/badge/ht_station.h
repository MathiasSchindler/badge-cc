#ifndef CLEAN_RX_HT_STATION_H
#define CLEAN_RX_HT_STATION_H

#include "../../src/shared/bytes.h"

#define CLEAN_HT_PEER_DETAIL 1040u
#define CLEAN_HT_PEER_BYTES 44u
#define CLEAN_HT_PEER_MAGIC 0x32505448u

_Static_assert(CLEAN_HT_PEER_DETAIL >= 1000u + 40u &&
               CLEAN_HT_PEER_DETAIL + CLEAN_HT_PEER_BYTES <= 1088u,
               "HT peer report must not overwrite association or unicast evidence");

typedef struct {
    uint8_t aifsn, acm, ecw_min, ecw_max;
    uint16_t txop_32us;
} clean_wmm_ac_t;

typedef struct {
    clean_wmm_ac_t ac[4];
    uint8_t present, information, parameter_set, uapsd;
} clean_wmm_t;

typedef struct {
    clean_wmm_t wmm;
    uint16_t capability, protection;
    uint8_t capabilities, operation, invalid, primary, secondary, width40;
    uint8_t rx_mcs, basic_mcs, unsupported_basic, ampdu_exponent, mpdu_spacing;
} clean_ht_peer_t;

enum {
    CLEAN_HT_FALLBACK_NONE = 0,
    CLEAN_HT_FALLBACK_NO_HT = 1u,
    CLEAN_HT_FALLBACK_MALFORMED = 2u,
    CLEAN_HT_FALLBACK_CHANNEL = 4u,
    CLEAN_HT_FALLBACK_MCS = 8u,
    CLEAN_HT_FALLBACK_WMM = 16u,
    CLEAN_HT_FALLBACK_MAC = 32u,
    CLEAN_HT_FALLBACK_PROTECTION = 64u
};

/* Parse optional peer IEs without changing the existing legacy association. */
static inline void clean_ht_peer_ie(clean_ht_peer_t *s, uint32_t id,
                                     const uint8_t *p, uint32_t size) {
    if (id == 45u) {
        if (s->capabilities || size != 26u) { s->invalid = 1u; return; }
        s->capabilities = 1u;
        s->capability = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
        s->ampdu_exponent = p[2] & 3u;
        s->mpdu_spacing = (p[2] >> 2u) & 7u;
        s->rx_mcs = p[3];
    } else if (id == 61u) {
        if (s->operation || size != 22u) { s->invalid = 1u; return; }
        s->operation = 1u;
        s->primary = p[0]; s->secondary = p[1] & 3u;
        s->width40 = (p[1] >> 2u) & 1u;
        s->protection = (uint16_t)((uint16_t)p[2] | ((uint16_t)p[3] << 8u));
        s->basic_mcs = p[6];
        for (uint32_t i = 7u; i < 22u; ++i)
            s->unsupported_basic |= p[i];
        if (s->secondary == 2u || (!s->secondary && s->width40)) s->invalid = 1u;
    } else if (id == 221u && size >= 4u &&
               p[0] == 0u && p[1] == 0x50u && p[2] == 0xf2u && p[3] == 2u) {
        if (size == 7u && p[4] == 0u && p[5] == 1u && !(p[6] & 0x70u) &&
            !s->wmm.present && !s->wmm.information) {
            s->wmm.information = 1u;
            s->wmm.parameter_set = p[6] & 15u;
            s->wmm.uapsd = p[6] >> 7u;
            return;
        }
        if (size != 24u || p[4] != 1u || p[5] != 1u || p[7] || (p[6] & 0x70u) ||
            s->wmm.present || s->wmm.information) { s->invalid = 1u; return; }
        clean_wmm_t wmm;
        badger_bytes_zero((uint8_t *)&wmm, sizeof(wmm));
        uint32_t seen = 0u;
        for (uint32_t i = 0u; i < 4u; ++i) {
            const uint8_t *ac = p + 8u + 4u*i;
            uint32_t index = (ac[0] >> 5u) & 3u;
            uint32_t bit = 1u << index;
            if ((seen & bit) || (ac[0] & 0x80u) || (ac[0] & 15u) < 2u ||
                (ac[1] & 15u) > (ac[1] >> 4u)) { s->invalid = 1u; return; }
            seen |= bit;
            wmm.ac[index].aifsn = ac[0] & 15u;
            wmm.ac[index].acm = (ac[0] >> 4u) & 1u;
            wmm.ac[index].ecw_min = ac[1] & 15u;
            wmm.ac[index].ecw_max = ac[1] >> 4u;
            wmm.ac[index].txop_32us = (uint16_t)((uint16_t)ac[2] | ((uint16_t)ac[3] << 8u));
        }
        wmm.present = 1u; wmm.parameter_set = p[6] & 15u; wmm.uapsd = p[6] >> 7u;
        badger_bytes_copy((uint8_t *)&s->wmm, (const uint8_t *)&wmm, sizeof(wmm));
    }
}

static inline uint32_t clean_ht_peer_fallback(const clean_ht_peer_t *s, uint32_t channel) {
    uint32_t reason = 0u;
    if (s->invalid || (s->width40 && !(s->capability & 2u)))
        reason |= CLEAN_HT_FALLBACK_MALFORMED;
    if (!s->capabilities || !s->operation) reason |= CLEAN_HT_FALLBACK_NO_HT;
    if (channel < 1u || channel > 11u || s->primary != channel)
        reason |= CLEAN_HT_FALLBACK_CHANNEL;
    if (!s->rx_mcs || s->unsupported_basic || (s->basic_mcs & ~s->rx_mcs))
        reason |= CLEAN_HT_FALLBACK_MCS;
    if (!s->wmm.present || s->wmm.ac[0].acm) reason |= CLEAN_HT_FALLBACK_WMM;
    /* No association advertisement until actual EDCA/BA/receive-size support. */
    reason |= CLEAN_HT_FALLBACK_MAC;
    if ((s->protection & 3u) || (s->protection & 0x10u))
        reason |= CLEAN_HT_FALLBACK_PROTECTION;
    return reason;
}

static inline void clean_ht_peer_report(const clean_ht_peer_t *s, uint32_t channel,
                                         uint8_t detail[2048]) {
    uint8_t *p = detail + CLEAN_HT_PEER_DETAIL;
    badger_bytes_zero(p, CLEAN_HT_PEER_BYTES);
    badger_bytes_store_u32(p, CLEAN_HT_PEER_MAGIC);
    badger_bytes_store_u32(p + 4u, clean_ht_peer_fallback(s, channel));
    p[8] = s->rx_mcs; p[9] = s->basic_mcs; p[10] = s->width40; p[11] = s->primary;
    p[12] = (uint8_t)s->capability; p[13] = (uint8_t)(s->capability >> 8u);
    p[14] = (uint8_t)s->protection; p[15] = (uint8_t)(s->protection >> 8u);
    p[16] = s->wmm.present; p[17] = s->wmm.parameter_set; p[18] = s->wmm.uapsd;
    for (uint32_t i = 0u; i < 4u; ++i) {
        p[20u + 6u*i] = s->wmm.ac[i].aifsn;
        p[21u + 6u*i] = s->wmm.ac[i].acm;
        p[22u + 6u*i] = s->wmm.ac[i].ecw_min;
        p[23u + 6u*i] = s->wmm.ac[i].ecw_max;
        p[24u + 6u*i] = (uint8_t)s->wmm.ac[i].txop_32us;
        p[25u + 6u*i] = (uint8_t)(s->wmm.ac[i].txop_32us >> 8u);
    }
}

#endif

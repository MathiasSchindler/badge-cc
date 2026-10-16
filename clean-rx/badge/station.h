#ifndef CLEAN_RX_STATION_H
#define CLEAN_RX_STATION_H

#include "frames.h"

typedef struct {
    uint8_t bssid[6], ssid[32], ssid_length, secured;
    uint8_t rsn[128], rsn_length;
    uint16_t capability, rates, basic_rates;
} clean_station_ap_t;

enum {
    CLEAN_AP_INVALID = -1, CLEAN_AP_UNSUPPORTED = -2,
    CLEAN_AP_OPEN = 1, CLEAN_AP_PSK_CCMP = 2
};

static const uint8_t clean_station_rates[12] = {
    2u, 4u, 11u, 22u, 12u, 18u, 24u, 36u, 48u, 72u, 96u, 108u
};

static inline uint16_t clean_station_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static inline int clean_station_suite(const uint8_t *p, uint8_t type) {
    return p[0] == 0u && p[1] == 15u && p[2] == 172u && p[3] == type;
}

static inline int clean_station_rsn(const uint8_t *p, uint32_t n) {
    if (n < 18u || clean_station_u16(p) != 1u) return CLEAN_AP_INVALID;
    int supported = clean_station_suite(p + 2u, 4u);
    uint32_t at = 6u, count = clean_station_u16(p + at);
    at += 2u;
    if (!count || count > (n - at) / 4u) return CLEAN_AP_INVALID;
    int ccmp = 0;
    for (uint32_t i = 0u; i < count; ++i, at += 4u)
        ccmp |= clean_station_suite(p + at, 4u);
    if (n - at < 2u) return CLEAN_AP_INVALID;
    count = clean_station_u16(p + at);
    at += 2u;
    if (!count || count > (n - at) / 4u) return CLEAN_AP_INVALID;
    int psk = 0;
    for (uint32_t i = 0u; i < count; ++i, at += 4u)
        psk |= clean_station_suite(p + at, 2u);
    uint16_t caps = 0u;
    if (at != n) {
        if (n - at < 2u) return CLEAN_AP_INVALID;
        caps = clean_station_u16(p + at);
        at += 2u;
    }
    if (at != n) {
        if (n - at < 2u) return CLEAN_AP_INVALID;
        count = clean_station_u16(p + at);
        at += 2u;
        if (count > (n - at) / 16u) return CLEAN_AP_INVALID;
        at += count * 16u;
        if (at != n && n - at != 4u) return CLEAN_AP_INVALID;
    }
    return supported && ccmp && psk && !(caps & 0x0040u) ?
           CLEAN_AP_PSK_CCMP : CLEAN_AP_UNSUPPORTED;
}

static inline int clean_station_beacon(const uint8_t *m, uint32_t n,
                                       const uint8_t bssid[6], clean_station_ap_t *ap) {
    if (n < 40u || m[0] != 0x80u || m[1] != 0u ||
        !clean_frame_valid(m, n) || (bssid[0] & 1u) ||
        !badger_bytes_equal(m + 10u, bssid, 6u) ||
        !badger_bytes_equal(m + 16u, bssid, 6u)) return CLEAN_AP_INVALID;
    clean_station_ap_t parsed;
    badger_bytes_zero((uint8_t *)&parsed, sizeof(parsed));
    parsed.capability = clean_station_u16(m + 34u);
    if ((parsed.capability & 3u) != 1u) return CLEAN_AP_UNSUPPORTED;
    badger_bytes_copy(parsed.bssid, bssid, 6u);
    int ssid = 0, rates = 0, rsn = 0, security = CLEAN_AP_OPEN;
    for (uint32_t at = 36u; at < n - 4u;) {
        if (n - 4u - at < 2u) return CLEAN_AP_INVALID;
        uint32_t id = m[at], size = m[at + 1u];
        at += 2u;
        if (size > n - 4u - at) return CLEAN_AP_INVALID;
        if (id == 0u) {
            if (ssid++ || !size || size > 32u) return CLEAN_AP_INVALID;
            parsed.ssid_length = (uint8_t)size;
            badger_bytes_copy(parsed.ssid, m + at, size);
        } else if (id == 1u || id == 50u) {
            if (!size || (id == 1u && (rates++ || size > 8u))) return CLEAN_AP_INVALID;
            for (uint32_t i = 0u; i < size; ++i) {
                uint8_t rate = m[at + i];
                uint32_t j = 0u;
                while (j < 12u && clean_station_rates[j] != (rate & 127u)) ++j;
                if (j == 12u) {
                    if (rate & 128u) return CLEAN_AP_UNSUPPORTED;
                } else {
                    parsed.rates |= (uint16_t)(1u << j);
                    if (rate & 128u) parsed.basic_rates |= (uint16_t)(1u << j);
                }
            }
        } else if (id == 48u) {
            if (rsn++) return CLEAN_AP_INVALID;
            if (size+2u>sizeof(parsed.rsn)) return CLEAN_AP_UNSUPPORTED;
            parsed.rsn_length=(uint8_t)(size+2u);
            badger_bytes_copy(parsed.rsn,m+at-2u,size+2u);
            security = clean_station_rsn(m + at, size);
            if (security < 0) return security;
        }
        at += size;
    }
    if (!ssid || !rates || !parsed.basic_rates) return CLEAN_AP_INVALID;
    if (!(parsed.rates & (1u << 4u)) ||
        (!!(parsed.capability & 16u) != !!rsn)) return CLEAN_AP_UNSUPPORTED;
    parsed.secured = (uint8_t)(security == CLEAN_AP_PSK_CCMP);
    badger_bytes_copy((uint8_t *)ap, (const uint8_t *)&parsed, sizeof(parsed));
    return security;
}

static inline void clean_station_header(uint8_t *m, uint8_t fc, uint16_t sequence,
                                        const uint8_t mac[6], const uint8_t bssid[6]) {
    badger_bytes_zero(m, 24u);
    m[0] = fc;
    m[2] = 0x3cu; /* SIFS + a 6 Mbps ACK, including ERP extension. */
    badger_bytes_copy(m + 4u, bssid, 6u);
    badger_bytes_copy(m + 10u, mac, 6u);
    badger_bytes_copy(m + 16u, bssid, 6u);
    m[22] = (uint8_t)(sequence << 4u);
    m[23] = (uint8_t)(sequence >> 4u);
}

static inline uint32_t clean_station_auth(uint8_t frame[160], uint16_t sequence,
                                         const uint8_t mac[6], const clean_station_ap_t *ap) {
    badger_bytes_zero(frame, 160u);
    clean_station_header(frame + 6u, 0xb0u, sequence, mac, ap->bssid);
    frame[32] = 1u;
    (void)clean_ofdm_plcp_rate(frame, 34u, 0u);
    badger_bytes_store_u32(frame + 36u, diag_crc32(frame + 6u, 30u));
    return 40u;
}

static inline uint32_t clean_station_associate(uint8_t frame[160], uint16_t sequence,
                                              const uint8_t mac[6], const clean_station_ap_t *ap) {
    badger_bytes_zero(frame, 160u);
    uint8_t *m = frame + 6u;
    clean_station_header(m, 0u, sequence, mac, ap->bssid);
    uint16_t caps = (uint16_t)(1u | (ap->secured ? 16u : 0u) |
                              (ap->capability & (32u | 1024u)));
    m[24] = (uint8_t)caps; m[25] = (uint8_t)(caps >> 8u);
    m[26] = 10u;
    uint32_t at = 28u;
    m[at++] = 0u; m[at++] = ap->ssid_length;
    badger_bytes_copy(m + at, ap->ssid, ap->ssid_length);
    at += ap->ssid_length;
    uint8_t advertised[12];
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < 12u; ++i)
        if (ap->rates & (1u << i))
            advertised[count++] = (uint8_t)(clean_station_rates[i] |
                ((ap->basic_rates & (1u << i)) ? 128u : 0u));
    uint32_t first = count < 8u ? count : 8u;
    m[at++] = 1u; m[at++] = (uint8_t)first;
    badger_bytes_copy(m + at, advertised, first);
    at += first;
    if (count > first) {
        m[at++] = 50u; m[at++] = (uint8_t)(count - first);
        badger_bytes_copy(m + at, advertised + first, count - first);
        at += count - first;
    }
    if (ap->secured) {
        static const uint8_t rsn[22] = {
            48u,20u,1u,0u,0u,15u,172u,4u,1u,0u,0u,15u,172u,4u,
            1u,0u,0u,15u,172u,2u,0u,0u
        };
        badger_bytes_copy(m + at, rsn, sizeof(rsn));
        at += sizeof(rsn);
    }
    badger_bytes_store_u32(m + at, diag_crc32(m, at));
    (void)clean_ofdm_plcp_rate(frame, at + 4u, 0u);
    return at + 10u;
}

static inline int clean_station_response(const uint8_t *m, uint32_t n,
                                         const uint8_t mac[6], const uint8_t bssid[6],
                                         uint8_t association, uint16_t *aid) {
    if (n < 34u || !clean_frame_valid(m, n) || (m[1] & 0xc7u) ||
        (m[22] & 15u) || !badger_bytes_equal(m + 4u, mac, 6u) ||
        !badger_bytes_equal(m + 10u, bssid, 6u) ||
        !badger_bytes_equal(m + 16u, bssid, 6u)) return -1;
    if (m[0] != (association ? 0x10u : 0xb0u)) return -1;
    if (!association && (clean_station_u16(m + 24u) != 0u ||
                         clean_station_u16(m + 26u) != 2u)) return -1;
    uint16_t status = clean_station_u16(m + (association ? 26u : 28u));
    if (status || !association) return status;
    uint16_t raw_aid = clean_station_u16(m + 28u);
    if ((raw_aid & 0xc000u) != 0xc000u || !(raw_aid & 0x3fffu) ||
        (raw_aid & 0x3fffu) > 2007u) return -1;
    int supported_rates = 0;
    for (uint32_t at = 30u; at < n - 4u;) {
        if (n - 4u - at < 2u || m[at + 1u] > n - 4u - at - 2u) return -1;
        if (m[at] == 1u) {
            if (supported_rates++ || !m[at + 1u] || m[at + 1u] > 8u) return -1;
        }
        at += 2u + m[at + 1u];
    }
    if (!supported_rates || !(clean_station_u16(m + 24u) & 1u)) return -1;
    *aid = (uint16_t)(raw_aid & 0x3fffu);
    return 0;
}

static inline int clean_station_departure(const uint8_t *m, uint32_t n,
                                          const uint8_t mac[6], const uint8_t bssid[6]) {
    if (n < 30u || !clean_frame_valid(m, n) ||
        (m[0] != 0xc0u && m[0] != 0xa0u) || (m[1] & 0xc7u) || (m[22] & 15u) ||
        !badger_bytes_equal(m + 4u, mac, 6u) ||
        !badger_bytes_equal(m + 10u, bssid, 6u) ||
        !badger_bytes_equal(m + 16u, bssid, 6u)) return -1;
    return clean_station_u16(m + 24u);
}

static inline int clean_station_evidence(const uint8_t detail[2048]) {
    uint32_t auth_size = badger_bytes_load_u32(detail + 596u);
    uint32_t assoc_size = badger_bytes_load_u32(detail + 796u);
    uint16_t aid = 0u;
    return (detail[2000u] == 16u || detail[2000u] == 18u) && badger_bytes_load_u32(detail + 8u) == 0u &&
           badger_bytes_load_u32(detail + 4u) == 8u &&
           badger_bytes_load_u32(detail + 520u) == 0x31545341u &&
           badger_bytes_load_u32(detail + 524u) == 4u &&
           auth_size <= 192u && assoc_size <= 192u &&
           clean_station_response(detail + 600u, auth_size, detail + 1972u,
                                   detail + 1980u, 0u, &aid) == 0 &&
           clean_station_response(detail + 800u, assoc_size, detail + 1972u,
                                   detail + 1980u, 1u, &aid) == 0 &&
           aid == badger_bytes_load_u32(detail + 536u);
}

static inline int clean_station_eapol_m1(const uint8_t *m, uint32_t n,
                                        const uint8_t mac[6], const uint8_t bssid[6]) {
    if (n < 135u || !clean_frame_valid(m, n) ||
        (m[0] != 0x08u && m[0] != 0x88u) || (m[1] & 0xc7u) != 2u ||
        (m[22] & 15u) || !badger_bytes_equal(m + 4u, mac, 6u) ||
        !badger_bytes_equal(m + 10u, bssid, 6u) ||
        !badger_bytes_equal(m + 16u, bssid, 6u)) return 0;
    uint32_t header = m[0] == 0x88u ? 26u : 24u;
    if (n < header + 111u || (header == 26u && (m[24] & 128u))) return 0;
    static const uint8_t snap[8] = {170u,170u,3u,0u,0u,0u,136u,142u};
    if (!badger_bytes_equal(m + header, snap, sizeof(snap))) return 0;
    const uint8_t *e = m + header + 8u;
    uint32_t body = ((uint32_t)e[2] << 8u) | e[3];
    if ((e[0] != 1u && e[0] != 2u) || e[1] != 3u || body < 95u ||
        body > n - header - 16u || e[4] != 2u ||
        e[5] != 0u || e[6] != 0x8au || e[7] != 0u || e[8] != 16u ||
        (((uint32_t)e[97] << 8u) | e[98]) != body - 95u) return 0;
    for (uint32_t i = 81u; i < 97u; ++i) if (e[i]) return 0;
    return 1;
}

static inline int clean_station_eapol_evidence(const uint8_t detail[2048]) {
    uint32_t size = badger_bytes_load_u32(detail + 1088u);
    return clean_station_evidence(detail) && badger_bytes_load_u32(detail + 572u) == CLEAN_AP_PSK_CCMP &&
           badger_bytes_load_u32(detail + 576u) != 0u && size >= 18u && size <= 528u &&
           clean_station_eapol_m1(detail + 1122u, size - 18u, detail + 1972u, detail + 1980u);
}

#endif

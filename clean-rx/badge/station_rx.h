#ifndef CLEAN_BADGE_STATION_RX_H
#define CLEAN_BADGE_STATION_RX_H

#include "station.h"
#include "../src/mac.h"

#define CLEAN_RX_REASSEMBLED 1u
#define CLEAN_STATION_MAC_DETAIL 1680u
#define CLEAN_STATION_MAC_MAGIC 0x3143414du
#define CLEAN_STATION_PROTECTED_DETAIL 1776u
#define CLEAN_STATION_TRACE_DETAIL 1808u

_Static_assert(CLEAN_STATION_MAC_DETAIL + 32u + sizeof(clean_mac_radio_status_t) <=
               CLEAN_STATION_PROTECTED_DETAIL, "MAC diagnostic regions overlap");
_Static_assert(CLEAN_STATION_PROTECTED_DETAIL + 32u <= CLEAN_STATION_TRACE_DETAIL &&
               CLEAN_STATION_TRACE_DETAIL + sizeof(clean_mac_trace_status_t) <= 1960u,
               "MAC trace must fit before peer diagnostics");

static inline void clean_station_rx_report_at(const clean_mac_rx_t *s, uint8_t *detail,
                                               uint32_t offset) {
    const uint32_t words[] = {CLEAN_STATION_MAC_MAGIC, s->delivered, s->duplicates,
        s->fragments, s->malformed, s->unsupported, s->expired, s->fragment_errors};
    for (uint32_t i = 0u; i < sizeof(words) / sizeof(words[0]); ++i)
        badger_bytes_store_u32(detail + offset + 4u * i, words[i]);
}

static inline void clean_station_rx_report(const clean_mac_rx_t *s, uint8_t *detail) {
    clean_station_rx_report_at(s, detail, CLEAN_STATION_MAC_DETAIL);
}

static inline int clean_station_rx(clean_mac_rx_t *s, uint8_t *packet, uint32_t *size,
                                    const uint8_t own[6], const uint8_t peer[6], uint32_t now) {
    if (*size < 46u) return CLEAN_MAC_IGNORED;
    uint8_t *m = packet + 18u;
    uint32_t n = *size - 18u;
    if (!badger_bytes_equal(m + 4u, own, 6u) ||
        !badger_bytes_equal(m + 10u, peer, 6u)) return CLEAN_MAC_IGNORED;
    /* Preserve the existing WPA per-TID path. QoS MAC reassembly is not yet
     * negotiated or implemented by the non-QoS station profile. */
    if (m[0] == 0x88u) return CLEAN_MAC_IGNORED;
    int result = clean_mac_rx(s, m, n, now);
    if (result == CLEAN_MAC_PROTECTED) return result;
    if (result != CLEAN_MAC_DELIVER) {
        *size = 0u;
        return result;
    }
    if (m[22] & 15u) {
        badger_bytes_copy(m, s->frame, s->length);
        *size = 18u + s->length;
        badger_bytes_store_u32(packet, *size | ((*size ^ 0xffffu) << 16u));
        packet[17] = CLEAN_RX_REASSEMBLED;
    }
    return result;
}

#endif

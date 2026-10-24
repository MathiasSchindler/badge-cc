#ifndef CLEAN_RX_FRAMES_H
#define CLEAN_RX_FRAMES_H

#include "../../src/shared/bytes.h"
#include "../../src/tools/diagnostics_logic.h"
#include "../src/tx_diag.h"
#include "../src/phy_rates.h"
#include "../src/ht_contract.h"

static inline void clean_record_raw_packet(uint8_t detail[2048], const uint8_t *packet,
                                           uint32_t size) {
    if (detail[2000u] == CLEAN_HT_AMPDU_PROBE_TX_MODE ||
        detail[2000u] == CLEAN_HT_AMPDU_PROBE_RX_MODE) return;
    uint32_t saved = size < CLEAN_DETAIL_PACKET_BYTES ? size : CLEAN_DETAIL_PACKET_BYTES;
    badger_bytes_store_u32(detail + 1088u, size);
    badger_bytes_zero(detail + 1104u, CLEAN_DETAIL_PACKET_BYTES);
    badger_bytes_copy(detail + 1104u, packet, saved);
}

static inline int clean_frame_valid(const uint8_t *mpdu, uint32_t length) {
    return length >= 14u && length <= CLEAN_MPDU_BYTES &&
           diag_crc32(mpdu, length - 4u) == badger_bytes_load_u32(mpdu + length - 4u);
}

static inline int clean_beacon_find(const uint8_t entries[320], uint32_t count,
                                    const uint8_t bssid[6]) {
    uint8_t nonzero = 0u;
    for (uint32_t i = 0u; i < 6u; ++i) nonzero |= bssid[i];
    if (count > 8u || !nonzero || (bssid[0] & 1u)) return -1;
    for (uint32_t i = 0u; i < count; ++i)
        if (badger_bytes_equal(entries + 40u * i, bssid, 6u)) return (int)i;
    return -1;
}

static inline int clean_beacon_info(const uint8_t *mpdu, uint32_t length,
                                    uint8_t entry[40], uint8_t channel) {
    if (length < 40u || mpdu[0] != 0x80u || mpdu[1] != 0u ||
        !clean_frame_valid(mpdu, length)) return 0;
    for (uint32_t at = 36u; at + 2u <= length - 4u;) {
        uint32_t size = mpdu[at + 1u];
        if (size > length - 4u - at - 2u) return 0;
        if (mpdu[at] == 0u && size <= 32u) {
            badger_bytes_zero(entry, 40u);
            badger_bytes_copy(entry, mpdu + 16u, 6u);
            entry[6] = (uint8_t)size;
            entry[7] = channel;
            badger_bytes_copy(entry + 8u, mpdu + at + 2u, size);
            return 1;
        }
        at += 2u + size;
    }
    return 0;
}

static inline void clean_cck_plcp_service(uint8_t frame[6], uint8_t rate,
                                         uint16_t duration, uint8_t service) {
    frame[0] = rate;
    frame[1] = service;
    frame[2] = (uint8_t)duration;
    frame[3] = (uint8_t)(duration >> 8u);
    uint16_t crc = clean_cck_plcp_crc(frame);
    frame[4] = (uint8_t)crc;
    frame[5] = (uint8_t)(crc >> 8u);
}

static inline void clean_cck_plcp(uint8_t frame[6], uint8_t rate, uint16_t duration) {
    clean_cck_plcp_service(frame, rate, duration, 0x04u);
}

static inline void clean_cck11_plcp(uint8_t frame[6], uint32_t mpdu_length) {
    (void)clean_cck_plcp_rate(frame, mpdu_length, 3u);
}

static inline void clean_ofdm_plcp(uint8_t frame[6]) {
    (void)clean_ofdm_plcp_rate(frame, 88u, 0u);
}

static inline int clean_probe_request(uint8_t frame[96], uint16_t sequence,
                                      const uint8_t *ssid, uint8_t ssid_length) {
    if (ssid_length > 32u || (ssid_length != 0u && ssid == 0)) return 0;
    badger_bytes_zero(frame, 96u);
    clean_cck_plcp(frame, 0x6eu, 64u);
    uint8_t *mpdu = frame + 6u;
    mpdu[0] = 0x40u;
    for (uint32_t i = 0u; i < 6u; ++i) mpdu[4u+i] = mpdu[16u+i] = 255u;
    static const uint8_t mac[6] = {0x02u,0x9bu,0xbfu,0x88u,0xddu,0x7du};
    badger_bytes_copy(mpdu + 10u, mac, 6u);
    mpdu[22] = (uint8_t)(sequence << 4u);
    mpdu[23] = (uint8_t)(sequence >> 4u);
    /* SSID, supported rates, extended rates, bounded vendor padding. */
    mpdu[25] = ssid_length;
    badger_bytes_copy(mpdu + 26u, ssid, ssid_length);
    uint32_t at = 26u + ssid_length;
    mpdu[at] = 1u;
    mpdu[at + 1u] = 8u;
    static const uint8_t rates[8] = {0x82u,0x84u,0x8bu,0x96u,0x0cu,0x12u,0x18u,0x24u};
    badger_bytes_copy(mpdu + at + 2u, rates, 8u);
    at += 10u;
    mpdu[at] = 50u;
    mpdu[at + 1u] = 4u;
    mpdu[at + 2u] = 0x30u; mpdu[at + 3u] = 0x48u;
    mpdu[at + 4u] = 0x60u; mpdu[at + 5u] = 0x6cu;
    at += 6u;
    mpdu[at] = 221u;
    mpdu[at + 1u] = (uint8_t)(82u - at);
    mpdu[at + 2u] = 0x02u;
    badger_bytes_store_u32(mpdu + 84u, diag_crc32(mpdu, 84u));
    return 1;
}

static inline uint32_t clean_auth_request(uint8_t frame[96], uint16_t sequence,
                                         const uint8_t bssid[6], uint8_t short_frame) {
    (void)clean_probe_request(frame, sequence, 0, 0u);
    uint8_t *mpdu = frame + 6u;
    mpdu[0] = 0xb0u;
    badger_bytes_copy(mpdu + 4u, bssid, 6u);
    badger_bytes_copy(mpdu + 16u, bssid, 6u);
    badger_bytes_zero(mpdu + 24u, 60u);
    mpdu[26] = 1u; /* Open-system authentication transaction 1. */
    mpdu[30] = 221u;
    mpdu[31] = 52u;
    mpdu[32] = 0x02u;
    badger_bytes_store_u32(mpdu + 84u, diag_crc32(mpdu, 84u));
    if (short_frame) {
        clean_cck_plcp(frame, 0x0au, 272u);
        badger_bytes_zero(frame + 36u, 60u);
        badger_bytes_store_u32(frame + 36u, diag_crc32(mpdu, 30u));
        return 40u;
    }
    return 94u;
}

static inline void clean_peer_request(uint8_t frame[96], const uint8_t challenge[14],
                                     uint8_t one_mbps) {
    (void)clean_probe_request(frame, (uint16_t)badger_bytes_load_u32(challenge), 0, 0u);
    frame[16] = 2u;
    badger_bytes_copy(frame + 17u, challenge, 5u);
    static const uint8_t tag[4] = {'C','R','X','P'};
    badger_bytes_copy(frame + 53u, tag, sizeof(tag));
    badger_bytes_copy(frame + 57u, challenge, 14u);
    if (one_mbps) clean_cck_plcp(frame, 0x0au, 704u);
    badger_bytes_store_u32(frame + 90u, diag_crc32(frame + 6u, 84u));
}

static inline int clean_peer_reply(const uint8_t *mpdu, uint32_t length,
                                   const uint8_t challenge[14]) {
    if (length != 88u || !clean_frame_valid(mpdu, length)) return 0;
    uint8_t expected[96];
    clean_peer_request(expected, challenge, 0u);
    return badger_bytes_equal(mpdu, expected + 6u, 88u);
}

static inline int clean_peer_received(const uint8_t detail[2048],
                                      const uint8_t challenge[14]) {
    return badger_bytes_load_u32(detail + 8u) == 0u &&
           badger_bytes_load_u32(detail + 4u) == 8u &&
           badger_bytes_load_u32(detail + 32u) == 0x4f505258u &&
           badger_bytes_load_u32(detail + 36u) == 10u &&
           badger_bytes_load_u32(detail + 44u) == 0u &&
           badger_bytes_load_u32(detail + 1092u) == 0u &&
           badger_bytes_load_u32(detail + 1996u) != 0u &&
           badger_bytes_load_u32(detail + 1088u) == 106u &&
           clean_peer_reply(detail + 1122u, 88u, challenge);
}

static inline uint32_t clean_compact_probe_request(uint8_t frame[96], uint16_t sequence,
                                                   const uint8_t *ssid, uint8_t ssid_length,
                                                   const uint8_t bssid[6]) {
    if (!clean_probe_request(frame, sequence, ssid, ssid_length)) return 0u;
    uint32_t total = 52u + ssid_length;
    clean_cck_plcp(frame, 0x0au, (uint16_t)((total - 6u) * 8u));
    badger_bytes_copy(frame + 10u, bssid, 6u);
    badger_bytes_copy(frame + 22u, bssid, 6u);
    badger_bytes_zero(frame + total - 4u, 96u - total + 4u);
    badger_bytes_store_u32(frame + total - 4u, diag_crc32(frame + 6u, total - 10u));
    return total;
}

static inline int clean_ap_reply(const uint8_t *mpdu, uint32_t length,
                                 const uint8_t mac[6], const uint8_t bssid[6],
                                 uint8_t auth) {
    if (length < 28u || !clean_frame_valid(mpdu, length) || (mpdu[1] & 0xc7u) != 0u ||
        !badger_bytes_equal(mpdu + 4u, mac, 6u) ||
        !badger_bytes_equal(mpdu + 10u, bssid, 6u) ||
        !badger_bytes_equal(mpdu + 16u, bssid, 6u)) return 0;
    if (mpdu[0] == 0xc0u || mpdu[0] == 0xa0u) return length >= 30u;
    if (!auth) return length >= 40u && mpdu[0] == 0x50u;
    return length >= 34u && mpdu[0] == 0xb0u && mpdu[24] == 0u && mpdu[25] == 0u &&
           mpdu[26] == 2u && mpdu[27] == 0u;
}

static inline int clean_ack_reply(const uint8_t *mpdu, uint32_t length,
                                  const uint8_t mac[6]) {
    return length == 14u && clean_frame_valid(mpdu, length) &&
           mpdu[0] == 0xd4u && mpdu[1] == 0u &&
           badger_bytes_equal(mpdu + 4u, mac, 6u);
}

static inline int clean_rx_phase(const uint8_t *packet, uint32_t size) {
    if (size < 18u || (packet[12] & 0x7fu) != (uint8_t)CLEAN_RX_PHASE_MAGIC ||
        packet[13] != (uint8_t)(CLEAN_RX_PHASE_MAGIC >> 8u))
        return -1;
    return packet[12] >> 7u;
}

static inline void clean_record_post_tx(uint8_t detail[2048], const uint8_t *packet,
                                       uint32_t size, uint32_t elapsed) {
    uint32_t delivered = badger_bytes_load_u32(detail + 2040u);
    badger_bytes_store_u32(detail + 2040u, delivered + 1u);
    if (clean_rx_phase(packet, size) != 1) return;
    uint32_t post = badger_bytes_load_u32(detail + 2020u);
    if (post == 0u) badger_bytes_store_u32(detail + 2028u, elapsed);
    badger_bytes_store_u32(detail + 2020u, post + 1u);
    if (size < 32u || !clean_frame_valid(packet + 18u, size - 18u)) return;
    uint32_t valid = badger_bytes_load_u32(detail + 2024u);
    badger_bytes_store_u32(detail + 2024u, valid + 1u);
    if (size < 46u || (packet[18] & 0x0fu) != 0u ||
        !badger_bytes_equal(packet + 28u, detail + 1980u, 6u) ||
        !badger_bytes_equal(packet + 34u, detail + 1980u, 6u)) return;
    uint32_t ap = badger_bytes_load_u32(detail + 2032u);
    badger_bytes_store_u32(detail + 2032u, ap + 1u);
    if (packet[18] == 0x80u) {
        uint32_t beacons = badger_bytes_load_u32(detail + 2036u);
        badger_bytes_store_u32(detail + 2036u, beacons + 1u);
    }
}

static inline int clean_tx_confirmed(const uint8_t detail[2048]) {
    uint32_t size = badger_bytes_load_u32(detail + 1088u);
    uint32_t tx_length = badger_bytes_load_u32(detail + 1988u);
    if (size < 18u || size > CLEAN_DETAIL_PACKET_BYTES ||
        (tx_length != 40u && tx_length != 94u && (tx_length < 52u || tx_length > 84u)) ||
        badger_bytes_load_u32(detail + 8u) != 0u ||
        badger_bytes_load_u32(detail + 4u) != 8u ||
        badger_bytes_load_u32(detail + 32u) != 0x4f505258u ||
        badger_bytes_load_u32(detail + 36u) != 10u ||
        badger_bytes_load_u32(detail + 44u) != 0u ||
        badger_bytes_load_u32(detail + 1092u) != 1u ||
        badger_bytes_load_u32(detail + 1096u) != 1u ||
        badger_bytes_load_u32(detail + 40u) != 7u ||
        badger_bytes_load_u32(detail + 76u) != (0x55550000u | tx_length)) return 0;
    const uint8_t *mpdu = detail + 1122u;
    return (badger_bytes_load_u32(detail + 1992u) != 0u &&
            clean_ack_reply(mpdu, size - 18u, detail + 1972u)) ||
           (badger_bytes_load_u32(detail + 1960u) != 0u &&
            (clean_ap_reply(mpdu, size - 18u, detail + 1972u, detail + 1980u, 0u) ||
             clean_ap_reply(mpdu, size - 18u, detail + 1972u, detail + 1980u, 1u)));
}

static inline int clean_auth_response_status(const uint8_t detail[2048]) {
    uint32_t size = badger_bytes_load_u32(detail + 1088u);
    const uint8_t *mpdu = detail + 1122u;
    if (!clean_tx_confirmed(detail) ||
        badger_bytes_load_u32(detail + 1960u) == 0u || mpdu[0] != 0xb0u ||
        !clean_ap_reply(mpdu, size - 18u, detail + 1972u, detail + 1980u, 1u))
        return -1;
    return (int)((uint32_t)mpdu[28] | ((uint32_t)mpdu[29] << 8u));
}

#endif

#ifndef CLEAN_VENDOR_HT_PEER_FRAME_H
#define CLEAN_VENDOR_HT_PEER_FRAME_H

#include "protocol.h"
#include "../../src/shared/bytes.h"
#include "../../src/tools/diagnostics_logic.h"

#define CLEAN_PEER_ETHERNET_BYTES 62u
#define CLEAN_PEER_FRAME_COUNT 8u
#define CLEAN_PEER_RX_MODE 29u
#define CLEAN_PEER_RX_DETAIL 680u
#define CLEAN_PEER_TX_DETAIL 28u
#define CLEAN_PEER_CCK_TX_MODE 11u
#define CLEAN_PEER_ETHERNET_STORAGE 2014u
_Static_assert((CLEAN_PEER_ETHERNET_BYTES + 18u) % 4u == 0u,
               "Vendor SDPCM transfer must not add Ethernet tail padding");

static inline uint32_t clean_peer_ht_rate(uint32_t mcs) {
    return 0x01010000u | mcs;
}

static inline int clean_peer_rate_matches(uint32_t rate, uint32_t mcs) {
    return mcs < 8u && (rate & ~0xc0000000u) == clean_peer_ht_rate(mcs);
}

static inline int clean_peer_cck(const uint8_t nonce[14]) {
    return nonce[13] == 0x81u || nonce[13] == 0x82u;
}

static inline uint32_t clean_peer_ethernet_length(const uint8_t nonce[14]) {
    if (!clean_peer_cck(nonce)) return CLEAN_PEER_ETHERNET_BYTES;
    uint32_t length = (uint32_t)nonce[11] | (uint32_t)nonce[12] << 8u;
    return length >= CLEAN_PEER_ETHERNET_BYTES && length <= CLEAN_PEER_ETHERNET_STORAGE &&
        (length & 3u) == 2u ? length : 0u;
}

static inline int clean_peer_cck_configure(uint8_t nonce[14], uint32_t mbps, uint32_t length) {
    if ((mbps != 1u && mbps != 2u) || length < CLEAN_PEER_ETHERNET_BYTES ||
        length > CLEAN_PEER_ETHERNET_STORAGE || (length & 3u) != 2u) return 0;
    nonce[11] = (uint8_t)length;
    nonce[12] = (uint8_t)(length >> 8u);
    nonce[13] = (uint8_t)(0x80u | mbps);
    return 1;
}

static inline int clean_peer_rx_config_valid(const uint8_t nonce[14]) {
    return nonce[13] < 8u || (clean_peer_cck(nonce) && clean_peer_ethernet_length(nonce));
}

static inline int clean_peer_cck_metadata(const uint8_t packet[18], uint32_t channel,
                                          const uint8_t nonce[14]) {
    return clean_peer_cck(nonce) && channel >= 1u && channel <= 11u &&
        packet[15] == (nonce[13] == 0x81u ? 10u : 20u) &&
        packet[16] == channel && packet[17] == 0u;
}

static inline uint8_t clean_peer_mac_byte(const uint8_t nonce[14], uint32_t index) {
    return index ? nonce[index] : (uint8_t)((nonce[0] & 0xfcu) | 2u);
}

static inline uint8_t clean_peer_payload_byte(const uint8_t nonce[14], uint32_t index,
                                             uint8_t sequence) {
    static const uint8_t magic[4] = {'C','H','T','P'};
    return index < 4u ? magic[index] : index < 18u ? nonce[index - 4u] :
        index == 18u ? sequence : (uint8_t)(index * 29u + sequence);
}

static inline void clean_peer_ethernet(uint8_t *frame,
                                      const uint8_t nonce[14], uint8_t sequence) {
    for (uint32_t i = 0u; i < 6u; ++i) {
        frame[i] = 255u;
        frame[6u + i] = clean_peer_mac_byte(nonce, i);
    }
    frame[12] = 0x88u;
    frame[13] = 0xb5u;
    for (uint32_t i = 14u; i < clean_peer_ethernet_length(nonce); ++i)
        frame[i] = clean_peer_payload_byte(nonce, i - 14u, sequence);
}

/* Returns a sequence number only for the complete, FCS-checked fresh fixture. */
static inline int clean_peer_mpdu_sequence(const uint8_t *mpdu, uint32_t length,
                                           const uint8_t nonce[14]) {
    uint32_t ethernet = clean_peer_ethernet_length(nonce);
    if (!mpdu || !ethernet || (length != ethernet + 22u && length != ethernet + 24u) ||
        (mpdu[0] != 0x08u && mpdu[0] != 0x88u) ||
        (mpdu[1] & ~0x08u) != 2u ||
        diag_crc32(mpdu, length - 4u) != badger_bytes_load_u32(mpdu + length - 4u))
        return -1;
    uint32_t header = mpdu[0] == 0x88u ? 26u : 24u;
    if (length != header + ethernet - 2u || (mpdu[22] & 15u) ||
        (header == 26u && (mpdu[24] & 0x80u))) return -1;
    for (uint32_t i = 0u; i < 6u; ++i)
        if (mpdu[4u + i] != 255u || mpdu[10u + i] != clean_peer_mac_byte(nonce, i) ||
            mpdu[16u + i] != clean_peer_mac_byte(nonce, i)) return -1;
    static const uint8_t snap[8] = {0xaau,0xaau,3u,0u,0u,0u,0x88u,0xb5u};
    if (!badger_bytes_equal(mpdu + header, snap, sizeof(snap))) return -1;
    const uint8_t *body = mpdu + header + 8u;
    uint8_t sequence = body[18];
    if (sequence >= CLEAN_PEER_FRAME_COUNT) return -1;
    for (uint32_t i = 0u; i < ethernet - 14u; ++i)
        if (body[i] != clean_peer_payload_byte(nonce, i, sequence)) return -1;
    return sequence;
}
#endif

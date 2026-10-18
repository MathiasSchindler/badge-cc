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
_Static_assert((CLEAN_PEER_ETHERNET_BYTES + 18u) % 4u == 0u,
               "Vendor SDPCM transfer must not add Ethernet tail padding");

static inline uint32_t clean_peer_ht_rate(uint32_t mcs) {
    return 0x01010000u | mcs;
}

static inline int clean_peer_rate_matches(uint32_t rate, uint32_t mcs) {
    return mcs < 8u && (rate & ~0xc0000000u) == clean_peer_ht_rate(mcs);
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

static inline void clean_peer_ethernet(uint8_t frame[CLEAN_PEER_ETHERNET_BYTES],
                                      const uint8_t nonce[14], uint8_t sequence) {
    for (uint32_t i = 0u; i < 6u; ++i) {
        frame[i] = 255u;
        frame[6u + i] = clean_peer_mac_byte(nonce, i);
    }
    frame[12] = 0x88u;
    frame[13] = 0xb5u;
    for (uint32_t i = 14u; i < CLEAN_PEER_ETHERNET_BYTES; ++i)
        frame[i] = clean_peer_payload_byte(nonce, i - 14u, sequence);
}

/* Returns a sequence number only for the complete, FCS-checked fresh fixture. */
static inline int clean_peer_mpdu_sequence(const uint8_t *mpdu, uint32_t length,
                                           const uint8_t nonce[14]) {
    if (!mpdu || (length != 84u && length != 86u) ||
        (mpdu[0] != 0x08u && mpdu[0] != 0x88u) ||
        (mpdu[1] & ~0x08u) != 2u ||
        diag_crc32(mpdu, length - 4u) != badger_bytes_load_u32(mpdu + length - 4u))
        return -1;
    uint32_t header = mpdu[0] == 0x88u ? 26u : 24u;
    if (length != header + CLEAN_PEER_ETHERNET_BYTES - 2u || (mpdu[22] & 15u) ||
        (header == 26u && (mpdu[24] & 0x80u))) return -1;
    for (uint32_t i = 0u; i < 6u; ++i)
        if (mpdu[4u + i] != 255u || mpdu[10u + i] != clean_peer_mac_byte(nonce, i) ||
            mpdu[16u + i] != clean_peer_mac_byte(nonce, i)) return -1;
    static const uint8_t snap[8] = {0xaau,0xaau,3u,0u,0u,0u,0x88u,0xb5u};
    if (!badger_bytes_equal(mpdu + header, snap, sizeof(snap))) return -1;
    const uint8_t *body = mpdu + header + 8u;
    uint8_t sequence = body[18];
    if (sequence >= CLEAN_PEER_FRAME_COUNT) return -1;
    for (uint32_t i = 0u; i < CLEAN_PEER_ETHERNET_BYTES - 14u; ++i)
        if (body[i] != clean_peer_payload_byte(nonce, i, sequence)) return -1;
    return sequence;
}
#endif

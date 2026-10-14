#ifndef CLEAN_RX_FRAMES_H
#define CLEAN_RX_FRAMES_H

#include "../../src/shared/bytes.h"
#include "../../src/tools/diagnostics_logic.h"

static inline int clean_frame_valid(const uint8_t *mpdu, uint32_t length) {
    return length >= 28u && length <= 504u &&
           diag_crc32(mpdu, length - 4u) == badger_bytes_load_u32(mpdu + length - 4u);
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

static inline int clean_probe_request(uint8_t frame[96], uint16_t sequence,
                                      const uint8_t *ssid, uint8_t ssid_length) {
    if (ssid_length > 32u || (ssid_length != 0u && ssid == 0)) return 0;
    badger_bytes_zero(frame, 96u);
    /* 11 Mbps CCK, 88-byte MPDU including FCS, 64 us PLCP length. */
    frame[0] = 0x6eu;
    frame[1] = 0x04u;
    frame[2] = 64u;
    uint16_t crc = 0xffffu;
    for (uint32_t i = 0u; i < 4u; ++i) {
        uint8_t byte = frame[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            uint16_t mix = (crc ^ ((uint16_t)byte << 8u)) & 0x8000u;
            crc = (uint16_t)((crc << 1u) ^ (mix ? 0x1021u : 0u));
            byte >>= 1u;
        }
    }
    crc = (uint16_t)~crc;
    uint16_t reversed = 0u;
    for (uint32_t bit = 0u; bit < 16u; ++bit)
        if (crc & (1u << bit)) reversed |= (uint16_t)(1u << (15u - bit));
    frame[4] = (uint8_t)reversed;
    frame[5] = (uint8_t)(reversed >> 8u);
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

#endif

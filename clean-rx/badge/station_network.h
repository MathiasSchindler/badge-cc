#ifndef CLEAN_STATION_NETWORK_H
#define CLEAN_STATION_NETWORK_H
#include "station.h"
#include "../peer/network.h"

static inline uint32_t clean_network_mpdu(uint8_t *frame, const uint8_t *ethernet,
        uint32_t length, const uint8_t own[6], const uint8_t bssid[6], uint16_t sequence) {
    if (length < 14u || length > CLEAN_NETWORK_MTU) return 0u;
    uint32_t total = length + 28u;
    badger_bytes_zero(frame, (total + 3u) & ~3u);
    clean_station_header(frame + 6u, 8u, sequence, own, bssid);
    frame[7] = 1u;
    badger_bytes_copy(frame + 22u, ethernet, 6u);
    static const uint8_t llc[6] = {0xaau,0xaau,3u,0u,0u,0u};
    badger_bytes_copy(frame + 30u, llc, sizeof(llc));
    badger_bytes_copy(frame + 36u, ethernet + 12u, length - 12u);
    badger_bytes_store_u32(frame + total - 4u, diag_crc32(frame + 6u, total - 10u));
    (void)clean_ofdm_plcp_rate(frame, total - 6u, 0u);
    return total;
}
static inline uint32_t clean_network_ethernet(uint8_t *ethernet, const uint8_t *m,
        uint32_t length, const uint8_t own[6], const uint8_t bssid[6]) {
    static const uint8_t llc[6] = {0xaau,0xaau,3u,0u,0u,0u};
    if (length < 36u || length > CLEAN_NETWORK_MTU + 22u ||
        m[0] != 8u || (m[1] & ~8u) != 2u || !clean_frame_valid(m, length) ||
        !badger_bytes_equal(m + 4u, own, 6u) ||
        !badger_bytes_equal(m + 10u, bssid, 6u) ||
        !badger_bytes_equal(m + 24u, llc, sizeof(llc))) return 0u;
    badger_bytes_copy(ethernet, m + 4u, 6u);
    badger_bytes_copy(ethernet + 6u, m + 16u, 6u);
    badger_bytes_copy(ethernet + 12u, m + 30u, length - 34u);
    return length - 22u;
}
#endif

#ifndef CLEAN_RX_UNICAST_H
#define CLEAN_RX_UNICAST_H

#include "phy_rates.h"

#define CLEAN_UNICAST_ADDR 0x201e0u
#define CLEAN_UNICAST_MAGIC 0x314b4341u
#define CLEAN_TX_ACK_REQUIRED 0x80000000u
#define CLEAN_ACK_ENABLE 0x630u
#define CLEAN_ACK_WAITING 0x631u
#define CLEAN_ACK_SEEN 0x632u
#define CLEAN_ACK_SENT 0x633u
#define CLEAN_ACK_ERROR 0x634u
#define CLEAN_ACK_TABLE 0x640u
#define CLEAN_ACK_PEER 0x63au
#define CLEAN_ACK_HOOK 314u

enum { CLEAN_UNICAST_IDLE, CLEAN_UNICAST_ACKED, CLEAN_UNICAST_TIMEOUT,
       CLEAN_UNICAST_BUSY };

typedef struct {
    uint32_t magic, result, attempts, ack_received;
    uint32_t ack_sent, ack_error, started_us, finished_us;
    uint32_t ack_txe_status, ack_ifs_status;
} clean_unicast_status_t;

static inline int clean_ack_rate(uint16_t basic, uint32_t received) {
    static const uint8_t rates[12] = {2u,4u,11u,22u,12u,18u,24u,36u,48u,72u,96u,108u};
    if (received >= 12u || !basic || (basic & 0xf000u)) return -1;
    uint32_t first = received < 4u ? 0u : 4u;
    int selected = -1;
    for (uint32_t i = first; i <= received; ++i)
        if ((basic & (1u << i)) && rates[i] <= rates[received]) selected = (int)i;
    if (selected >= 0) return selected;
    uint16_t mandatory = received < 4u ? 3u : 0x150u;
    for (uint32_t i = first; i <= received; ++i)
        if (mandatory & (1u << i)) selected = (int)i;
    return selected;
}

static inline int clean_ack_template(uint8_t plcp[6], uint32_t rate, uint16_t *phyctl1) {
    if (rate >= 12u) return 0;
    if (rate >= 4u) {
        *phyctl1 = clean_ofdm_rates[rate - 4u].phyctl1;
        return clean_ofdm_plcp_rate(plcp, 14u, rate - 4u);
    }
    static const uint8_t signal[4] = {10u,20u,55u,110u};
    static const uint16_t phy[4] = {0x0002u,0x0802u,0x1002u,0x1802u};
    static const uint8_t duration[4] = {112u,56u,21u,11u};
    *phyctl1 = phy[rate];
    plcp[0] = signal[rate];
    plcp[1] = (uint8_t)(4u | (rate == 3u ? 128u : 0u));
    plcp[2] = duration[rate]; plcp[3] = 0u;
    uint16_t crc = 0xffffu;
    for (uint32_t i = 0u; i < 4u; ++i) {
        crc ^= plcp[i];
        for (uint32_t b = 0u; b < 8u; ++b)
            crc = (uint16_t)((crc >> 1u) ^ ((crc & 1u) ? 0x8408u : 0u));
    }
    crc = (uint16_t)~crc;
    plcp[4] = (uint8_t)crc; plcp[5] = (uint8_t)(crc >> 8u);
    return 1;
}

#endif

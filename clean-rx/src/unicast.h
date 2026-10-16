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
#define CLEAN_RESPONSE_POLICY 0x637u
#define CLEAN_CTS_ALLOWED 0x638u
#define CLEAN_ACK_CONSUMED 0x639u
#define CLEAN_RESPONSE_INTERNAL_ACK 4u
#define CLEAN_RESPONSE_FC 0x63du
#define CLEAN_CTS_SENT 0x63eu
#define CLEAN_SHORT_SENT 0x63fu
#define CLEAN_ACK_TABLE 0x640u
#define CLEAN_ACK_AIRTIME 0x670u
#define CLEAN_ACK_KINDS 0x67cu
#define CLEAN_ACK_PEER 0x63au
#define CLEAN_ACK_HOOK 314u
#define CLEAN_MAC_RADIO_ADDR 0x20210u
#define CLEAN_MAC_RADIO_MAGIC 0x3343414du
#define CLEAN_MAC_TRACE_ADDR 0x20250u
#define CLEAN_MAC_TRACE_MAGIC 0x3152434du

enum { CLEAN_UNICAST_IDLE, CLEAN_UNICAST_ACKED, CLEAN_UNICAST_TIMEOUT,
       CLEAN_UNICAST_BUSY };

typedef struct {
    uint32_t magic, result, attempts, ack_received;
    uint32_t ack_sent, ack_error, started_us, finished_us;
    uint32_t ack_txe_status, ack_ifs_status;
} clean_unicast_status_t;

typedef struct {
    uint32_t magic, profile, cw, slots, nav_until, deferrals, control_acks, timeouts;
    uint32_t rx_waits, pending_rx;
    uint32_t cts_responses, short_responses;
    uint32_t policy_pending;
    uint32_t prepared_acks;
    uint32_t address_lo, address_hi;
} clean_mac_radio_status_t;

typedef struct {
    uint32_t magic;
    uint32_t responses[4], published[4], invalid;
} clean_mac_trace_status_t;

static inline uint32_t clean_mac_frame_kind(uint8_t fc) {
    return fc == 0xb0u ? 0u : fc == 0x10u ? 1u : fc == 8u ? 2u : 3u;
}

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

static inline uint32_t clean_ack_airtime_us(uint16_t basic, uint32_t received) {
    int rate = clean_ack_rate(basic, received);
    if (rate < 0) return 0u;
    if (rate >= 4) return clean_ofdm_airtime_us(14u, (uint32_t)rate - 4u);
    static const uint16_t cck[4] = {304u,248u,213u,203u};
    return cck[rate];
}

static inline uint32_t clean_ack_timeout_us(uint32_t received, uint8_t slot_us) {
    if (received >= 12u || (slot_us != 9u && slot_us != 20u)) return 0u;
    /* Accept the slowest legacy response in the modulation family. The PSM
     * reports GoodFCS, not PHY-RXSTART, so include the complete ACK PPDU. */
    uint32_t airtime = received < 4u ? 304u : clean_ofdm_airtime_us(14u, 0u);
    return 10u + slot_us + 20u + airtime;
}

static inline int clean_cck11_valid(const volatile uint8_t *plcp, uint32_t mpdu) {
    if (mpdu < 14u || mpdu > CLEAN_MPDU_BYTES || plcp[0] != 110u) return 0;
    uint32_t duration = (mpdu * 8u + 10u) / 11u;
    uint8_t service = (uint8_t)(4u | (duration * 11u - mpdu * 8u >= 8u ? 128u : 0u));
    if (plcp[1] != service || plcp[2] != (uint8_t)duration || plcp[3] != (uint8_t)(duration >> 8u))
        return 0;
    uint16_t crc = 0xffffu;
    for (uint32_t i = 0u; i < 4u; ++i) {
        crc ^= plcp[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit)
            crc = (uint16_t)((crc >> 1u) ^ ((crc & 1u) ? 0x8408u : 0u));
    }
    crc = (uint16_t)~crc;
    return plcp[4] == (uint8_t)crc && plcp[5] == (uint8_t)(crc >> 8u);
}

#endif

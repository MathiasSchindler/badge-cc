#ifndef CLEAN_VENDOR_HT_PEER_PROTOCOL_H
#define CLEAN_VENDOR_HT_PEER_PROTOCOL_H

#include "../../src/shared/usb_report.h"
#include "../../src/tools/diagnostics_logic.h"

#define CLEAN_PEER_MAGIC 0x31504843u
#define CLEAN_PEER_RECORDS 7u
#define CLEAN_PEER_RECORD_BASE 88u
#define CLEAN_PEER_RECORD_BYTES 24u
#define CLEAN_PEER_TRACE_BYTES 256u
#define CLEAN_PEER_CONTROL_BYTES 1024u
#define CLEAN_PEER_PHY_PROBE_MODE 10u
#define CLEAN_PEER_PHY_WINDOW_MODE 12u
#define CLEAN_PEER_COUNTER_BEFORE 88u
#define CLEAN_PEER_COUNTER_AFTER 120u
#define CLEAN_PEER_COUNTER_VALID 152u
#define CLEAN_PEER_COUNTER_COUNT 8u

static const uint32_t clean_peer_counter_offsets[CLEAN_PEER_COUNTER_COUNT] = {
    4u, 8u, 16u, 32u, 56u, 196u, 432u, 472u
};
static const char *const clean_peer_counter_names[CLEAN_PEER_COUNTER_COUNT] = {
    "txframe", "txbyte", "txerror", "txnobuf", "txphyerr", "txallfrm", "txfail", "txfrmsnt"
};

/* Same public version-10, 848-byte ABI verified by the vendor-burst fixture. */
static inline int clean_peer_counter_decode(const uint8_t *reply, uint32_t length,
                                             uint8_t output[32]) {
    if (length < 848u || usb_report_u32(reply) != 0x0350000au) return 0;
    for (uint32_t i = 0u; i < CLEAN_PEER_COUNTER_COUNT; ++i)
        for (uint32_t j = 0u; j < 4u; ++j)
            output[i * 4u + j] = reply[clean_peer_counter_offsets[i] + j];
    return 1;
}

static inline int clean_peer_tx_accounted(const uint8_t before[32],
                                           const uint8_t after[32], uint32_t sent) {
    if (!sent || usb_report_u32(after) - usb_report_u32(before) != sent ||
        usb_report_u32(after+28u) - usb_report_u32(before+28u) != sent) return 0;
    for (uint32_t i = 2u; i <= 6u; ++i) {
        if (i == 5u) continue; /* txallfrm also includes AP beacons/control frames. */
        if (usb_report_u32(after+4u*i) != usb_report_u32(before+4u*i)) return 0;
    }
    return 1;
}

static const uint16_t clean_peer_phy_registers[CLEAN_PEER_RECORDS] = {
    0x42cu, 0x465u, 0x464u, 0x466u, 0x430u, 0x431u, 0x432u
};
static const char *const clean_peer_phy_names[CLEAN_PEER_RECORDS] = {
    "phy.42c", "phy.465", "phy.464", "phy.466", "phy.430", "phy.431", "phy.432"
};

static inline uint32_t clean_peer_prepare_phy(uint32_t id,
        uint8_t request[CLEAN_PEER_CONTROL_BYTES], uint32_t *command, uint8_t *set) {
    if (id >= CLEAN_PEER_RECORDS) return 0u;
    for (uint32_t i = 0u; i < CLEAN_PEER_CONTROL_BYTES; ++i) request[i] = 0u;
    *command = 94u;
    *set = 0u;
    request[0] = (uint8_t)clean_peer_phy_registers[id];
    request[1] = (uint8_t)(clean_peer_phy_registers[id] >> 8u);
    return 8u;
}

static const char *const clean_peer_variables[CLEAN_PEER_RECORDS] = {
    "nmode", "nrate", "2g_rate", "mimo_bw_cap", "pkteng_stats",
    "pkteng.stop", "monitor.get"
};

static const char *const clean_peer_monitor_operations[CLEAN_PEER_RECORDS] = {
    "monitor.before", "radio.up", "channel.set", "monitor.enable",
    "monitor.enabled", "monitor.disable", "monitor.disabled"
};

static inline uint32_t clean_peer_prepare_monitor(uint32_t id,
                                                  uint8_t request[CLEAN_PEER_CONTROL_BYTES],
                                                  uint32_t *command, uint8_t *set) {
    static const uint32_t commands[CLEAN_PEER_RECORDS] = {107u, 2u, 30u, 108u, 107u, 108u, 107u};
    if (id >= CLEAN_PEER_RECORDS) return 0u;
    for (uint32_t i = 0u; i < CLEAN_PEER_CONTROL_BYTES; ++i) request[i] = 0u;
    *command = commands[id];
    *set = id == 1u || id == 2u || id == 3u || id == 5u;
    request[0] = id == 2u ? 6u : id == 3u ? 1u : 0u;
    return 4u;
}

static inline uint32_t clean_peer_prepare_query(uint32_t id,
                                                uint8_t request[CLEAN_PEER_CONTROL_BYTES],
                                                uint32_t *command, uint8_t *set) {
    if (id >= CLEAN_PEER_RECORDS) return 0u;
    for (uint32_t i = 0u; i < CLEAN_PEER_CONTROL_BYTES; ++i) request[i] = 0u;
    *set = 0u;
    *command = id == 6u ? 107u : 262u;
    if (id == 6u) return 4u;
    const char *name = id == 5u ? "pkteng" : clean_peer_variables[id];
    uint32_t size = 0u;
    do { request[size] = (uint8_t)name[size]; } while (request[size++]);
    if (id == 5u) {
        *command = 263u;
        *set = 1u;
        request[size] = 2u; /* Packet-engine TX stop; never a start request. */
        return size + 32u;
    }
    /* GET_VAR's request length is also the firmware's reply budget. */
    return CLEAN_PEER_CONTROL_BYTES;
}

static inline int clean_peer_report_valid(const uint8_t report[64],
                                          const uint8_t nonce[16],
                                          uint32_t build_id) {
    if (usb_report_u32(report) != 0x46363255u || report[4] != 1u ||
        report[5] || report[6] != 64u || report[7] ||
        usb_report_u32(report + 8u) != build_id ||
        usb_report_u32(report + 12u) != 3u ||
        usb_report_u32(report + 60u) != diag_crc32(report, 60u)) return 0;
    for (uint32_t i = 0u; i < 16u; ++i)
        if (report[16u + i] != nonce[i]) return 0;
    return 1;
}

static inline int clean_peer_trace_valid(const uint8_t trace[CLEAN_PEER_TRACE_BYTES]) {
    if (usb_report_u32(trace) != CLEAN_PEER_MAGIC ||
        usb_report_u32(trace + 16u) != CLEAN_PEER_RECORDS ||
        (usb_report_u32(trace + 20u) < 1u || usb_report_u32(trace + 20u) > 4u) ||
        usb_report_u32(trace + 24u) > CLEAN_PEER_RECORDS) return 0;
    uint32_t completed = usb_report_u32(trace + 24u);
    for (uint32_t i = 0u; i < completed; ++i) {
        const uint8_t *r = trace + CLEAN_PEER_RECORD_BASE + i * CLEAN_PEER_RECORD_BYTES;
        if (usb_report_u32(r) != i || usb_report_u32(r + 4u) > 1u ||
            usb_report_u32(r + 8u) > 9u ||
            usb_report_u32(r + 16u) > 1024u) return 0;
        if (usb_report_u32(r + 4u) &&
            (usb_report_u32(r + 8u) || usb_report_u32(r + 12u))) return 0;
    }
    return 1;
}

static inline int clean_peer_phy_supported(const uint8_t trace[CLEAN_PEER_TRACE_BYTES]) {
    uint32_t mode = usb_report_u32(trace + 20u);
    if (!clean_peer_trace_valid(trace) || (mode != 3u && mode != 4u) ||
        usb_report_u32(trace + 24u) != CLEAN_PEER_RECORDS) return 0;
    if (mode == 4u && (!usb_report_u32(trace + 28u) ||
                       usb_report_u32(trace + 28u) >= 65535u)) return 0;
    for (uint32_t i = 0u; i < CLEAN_PEER_RECORDS; ++i) {
        const uint8_t *r = trace + CLEAN_PEER_RECORD_BASE + i * CLEAN_PEER_RECORD_BYTES;
        if (!usb_report_u32(r + 4u) || usb_report_u32(r + 16u) != 4u ||
            usb_report_u32(r + 20u) > 65535u) return 0;
    }
    return 1;
}

static inline int clean_peer_monitor_supported(const uint8_t trace[CLEAN_PEER_TRACE_BYTES]) {
    if (!clean_peer_trace_valid(trace) || usb_report_u32(trace + 20u) != 2u ||
        usb_report_u32(trace + 24u) != CLEAN_PEER_RECORDS) return 0;
    for (uint32_t i = 0u; i < CLEAN_PEER_RECORDS; ++i) {
        const uint8_t *r = trace + CLEAN_PEER_RECORD_BASE + i * CLEAN_PEER_RECORD_BYTES;
        if (!usb_report_u32(r + 4u)) return 0;
        if (i == 0u || i == 4u || i == 6u)
            if (usb_report_u32(r + 16u) != 4u ||
                usb_report_u32(r + 20u) != (i == 4u ? 1u : 0u)) return 0;
    }
    return 1;
}

_Static_assert(CLEAN_PEER_RECORD_BASE + CLEAN_PEER_RECORDS * CLEAN_PEER_RECORD_BYTES <=
               CLEAN_PEER_TRACE_BYTES, "Peer evidence must fit the shared USB trace");
#endif

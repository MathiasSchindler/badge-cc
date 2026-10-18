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

static const char *const clean_peer_variables[CLEAN_PEER_RECORDS] = {
    "nmode", "nrate", "2g_rate", "mimo_bw_cap", "pkteng_stats",
    "pkteng.stop", "monitor.get"
};

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
        usb_report_u32(trace + 20u) != 1u ||
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

_Static_assert(CLEAN_PEER_RECORD_BASE + CLEAN_PEER_RECORDS * CLEAN_PEER_RECORD_BYTES <=
               CLEAN_PEER_TRACE_BYTES, "Peer evidence must fit the shared USB trace");
#endif

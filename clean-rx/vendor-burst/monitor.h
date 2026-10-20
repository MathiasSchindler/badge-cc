#ifndef CLEAN_VENDOR_MONITOR_H
#define CLEAN_VENDOR_MONITOR_H
#include "protocol.h"
#include "../../src/radio/wifi_control.h"

#define REF_MONITOR_MAGIC 0x314e4f4du
#define REF_MONITOR_CONTROLS 256u
#define REF_MONITOR_CONTROL_BYTES 32u
#define REF_MONITOR_CONTROL_COUNT 10u
#define REF_MONITOR_PACKETS 640u
#define REF_MONITOR_PACKET_BYTES 640u
#define REF_MONITOR_SLOT_BYTES 656u
#define REF_MONITOR_SLOTS 8u
_Static_assert(REF_MONITOR_CONTROLS + REF_MONITOR_CONTROL_COUNT * REF_MONITOR_CONTROL_BYTES <=
    REF_MONITOR_PACKETS && REF_MONITOR_PACKETS + REF_MONITOR_SLOTS * REF_MONITOR_SLOT_BYTES ==
    REF_TRACE_BYTES, "Raw monitor evidence must fit the trace");

static inline void ref_monitor_increment(uint8_t *value) {
    badger_bytes_store_u32(value, badger_bytes_load_u32(value) + 1u);
}

static inline void ref_monitor_packet(uint8_t trace[REF_TRACE_BYTES], const uint8_t nonce[14],
                                      const uint8_t *packet, uint32_t length, uint32_t ticks) {
    ref_monitor_increment(trace + 72u);
    ref_monitor_increment(trace + 128u + 4u * (packet[5] & 15u));
    uint32_t found = 0u;
    for (uint32_t offset = 0u; offset + 14u <= length && !found; ++offset)
        found = badger_bytes_equal(packet + offset, nonce, 14u);
    if (!found) { ref_monitor_increment(trace + 88u); return; }
    ref_monitor_increment(trace + 92u);
    if (length > REF_MONITOR_PACKET_BYTES) { ref_monitor_increment(trace + 84u); return; }
    uint32_t saved = badger_bytes_load_u32(trace + 76u);
    if (saved == REF_MONITOR_SLOTS) { ref_monitor_increment(trace + 80u); return; }
    uint8_t *slot = trace + REF_MONITOR_PACKETS + saved * REF_MONITOR_SLOT_BYTES;
    badger_bytes_store_u32(slot, length);
    badger_bytes_store_u32(slot + 4u, ticks);
    badger_bytes_store_u32(slot + 8u, packet[5] & 15u);
    badger_bytes_store_u32(slot + 12u, diag_crc32(packet, length));
    badger_bytes_copy(slot + 16u, packet, length);
    badger_bytes_store_u32(trace + 76u, saved + 1u);
}

static inline int ref_monitor_contains(const uint8_t *packet, uint32_t length,
                                       const uint8_t *body, uint32_t bytes) {
    if (!bytes || bytes > length) return 0;
    for (uint32_t offset = 0u; offset <= length - bytes; ++offset)
        if (badger_bytes_equal(packet + offset, body, bytes)) return 1;
    return 0;
}

static inline int ref_monitor_slot_valid(const uint8_t slot[REF_MONITOR_SLOT_BYTES]) {
    uint32_t length = badger_bytes_load_u32(slot);
    wifi_control_packet_t packet;
    return length <= REF_MONITOR_PACKET_BYTES &&
        wifi_control_parse_raw(slot + 16u, length, &packet) && packet.size == length &&
        packet.channel != 0u && packet.channel == badger_bytes_load_u32(slot + 8u) &&
        diag_crc32(slot + 16u, length) == badger_bytes_load_u32(slot + 12u);
}

static inline int ref_monitor_qualified(const uint32_t fcs_masks[4]) {
    return fcs_masks[0] && fcs_masks[1] && (fcs_masks[2] & fcs_masks[3]);
}
#endif

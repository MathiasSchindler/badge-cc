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
#define REF_MONITOR_FIXED_SLOTS (4u*CLEAN_HT_STIMULUS_COUNT/3u)
#define REF_MONITOR_PAGES (REF_MONITOR_FIXED_SLOTS/REF_MONITOR_SLOTS)
#define REF_MONITOR_PAGE_BASE 128u
#define REF_MONITOR_PAGE_MAGIC 0x3141524du
#define REF_MONITOR_PAGE_HEADER 16u
#define REF_MONITOR_PAGE_BYTES (REF_MONITOR_PAGE_HEADER+REF_MONITOR_SLOTS*REF_MONITOR_SLOT_BYTES)
_Static_assert(REF_MONITOR_FIXED_SLOTS == 32u &&
    REF_MONITOR_PAGE_BASE+REF_MONITOR_PAGE_BYTES <= REF_TRACE_BYTES,
    "Fixed-source raw monitor pages must fit complete bounded packets");
_Static_assert(REF_MONITOR_CONTROLS + REF_MONITOR_CONTROL_COUNT * REF_MONITOR_CONTROL_BYTES <=
    REF_MONITOR_PACKETS && REF_MONITOR_PACKETS + REF_MONITOR_SLOTS * REF_MONITOR_SLOT_BYTES ==
    REF_TRACE_BYTES, "Raw monitor evidence must fit the trace");

static inline void ref_monitor_increment(uint8_t *value) {
    badger_bytes_store_u32(value, badger_bytes_load_u32(value) + 1u);
}

static inline void ref_monitor_capture(uint8_t trace[REF_TRACE_BYTES], const uint8_t nonce[14],
        const uint8_t *packet, uint32_t length, uint32_t ticks, uint8_t *slots, uint32_t capacity) {
    ref_monitor_increment(trace + 72u);
    ref_monitor_increment(trace + 128u + 4u * (packet[5] & 15u));
    uint32_t found = 0u;
    for (uint32_t offset = 0u; offset + 14u <= length && !found; ++offset)
        found = badger_bytes_equal(packet + offset, nonce, 14u);
    if (!found) { ref_monitor_increment(trace + 88u); return; }
    ref_monitor_increment(trace + 92u);
    if (length > REF_MONITOR_PACKET_BYTES) { ref_monitor_increment(trace + 84u); return; }
    uint32_t saved = badger_bytes_load_u32(trace + 76u);
    if (saved >= capacity) { ref_monitor_increment(trace + 80u); return; }
    uint8_t *slot = slots + saved * REF_MONITOR_SLOT_BYTES;
    badger_bytes_store_u32(slot, length);
    badger_bytes_store_u32(slot + 4u, ticks);
    badger_bytes_store_u32(slot + 8u, packet[5] & 15u);
    badger_bytes_store_u32(slot + 12u, diag_crc32(packet, length));
    badger_bytes_copy(slot + 16u, packet, length);
    badger_bytes_store_u32(trace + 76u, saved + 1u);
}

static inline void ref_monitor_packet(uint8_t trace[REF_TRACE_BYTES], const uint8_t nonce[14],
                                      const uint8_t *packet, uint32_t length, uint32_t ticks) {
    ref_monitor_capture(trace,nonce,packet,length,ticks,trace+REF_MONITOR_PACKETS,REF_MONITOR_SLOTS);
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

static inline int ref_monitor_page_valid(const uint8_t page[REF_MONITOR_PAGE_BYTES],
                                         uint32_t index, uint32_t total) {
    if (index >= REF_MONITOR_PAGES || total > REF_MONITOR_FIXED_SLOTS ||
        badger_bytes_load_u32(page) != REF_MONITOR_PAGE_MAGIC ||
        badger_bytes_load_u32(page+4u) != index ||
        badger_bytes_load_u32(page+8u) != total) return 0;
    uint32_t first = index*REF_MONITOR_SLOTS;
    uint32_t count = total > first ? total-first : 0u;
    if (count > REF_MONITOR_SLOTS) count = REF_MONITOR_SLOTS;
    if (badger_bytes_load_u32(page+12u) != count) return 0;
    for (uint32_t i = 0u; i < REF_MONITOR_SLOTS; ++i) {
        const uint8_t *slot = page+REF_MONITOR_PAGE_HEADER+i*REF_MONITOR_SLOT_BYTES;
        uint32_t used = 0u;
        if (i < count) {
            if (!ref_monitor_slot_valid(slot)) return 0;
            used = 16u+badger_bytes_load_u32(slot);
        }
        for (uint32_t n = used; n < REF_MONITOR_SLOT_BYTES; ++n)
            if (slot[n]) return 0;
    }
    return 1;
}

static inline int ref_monitor_fixed_qualified(const uint32_t masks[4], uint32_t duplicates,
                                               uint32_t invalid) {
    uint32_t expected[4] = {0u};
    for (uint32_t i = 0u; i < CLEAN_HT_STIMULUS_COUNT; ++i) {
        expected[i%3u] |= 1u<<i;
        if (i%3u == 2u) expected[3] |= 1u<<i;
    }
    for (uint32_t i = 0u; i < 4u; ++i)
        if (masks[i] != expected[i]) return 0;
    return !duplicates && !invalid;
}
#endif

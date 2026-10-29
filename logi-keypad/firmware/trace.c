#include "trace.h"

void keypad_trace_init(keypad_trace_t *trace) {
    badger_bytes_zero((uint8_t *)trace, sizeof(*trace));
}

static void finish(uint8_t wire[64]) {
    badger_bytes_store_u32(wire + 60u, diag_crc32(wire, 60u));
}

void keypad_trace_clear_wire(uint8_t wire[64]) {
    badger_bytes_zero(wire, 64u);
    badger_bytes_store_u32(wire, KEYPAD_TRACE_MAGIC);
    wire[6] = KEYPAD_TRACE_CLEAR;
    finish(wire);
}

int keypad_trace_record(uint8_t wire[64], const uint8_t *raw, uint32_t length,
                         uint8_t feature, uint32_t timestamp_ms, uint32_t sequence) {
    if (!raw || length == 0u || length > 32u || !sequence) return 0;
    badger_bytes_zero(wire, 64u);
    badger_bytes_store_u32(wire, KEYPAD_TRACE_MAGIC);
    wire[4] = (uint8_t)length;
    wire[5] = raw[0];
    wire[6] = KEYPAD_TRACE_RECORD;
    wire[7] = 1u;
    badger_bytes_store_u32(wire + 8u, timestamp_ms);
    badger_bytes_store_u32(wire + 12u, sequence);
    badger_bytes_copy(wire + 16u, raw, length);
    wire[48] = feature;
    finish(wire);
    return 1;
}

int keypad_trace_wire_valid(const uint8_t wire[64]) {
    if (badger_bytes_load_u32(wire) != KEYPAD_TRACE_MAGIC ||
        badger_bytes_load_u32(wire + 60u) != diag_crc32(wire, 60u)) return 0;
    if (wire[6] == KEYPAD_TRACE_CLEAR) {
        for (uint32_t i = 4u; i < 60u; ++i) if (i != 6u && wire[i]) return 0;
        return 1;
    }
    if (wire[6] != KEYPAD_TRACE_RECORD || (wire[7] != 1u && wire[7] != 2u) ||
        !wire[4] || wire[4] > 32u || wire[5] != wire[16] ||
        !badger_bytes_load_u32(wire + 12u)) return 0;
    for (uint32_t i = 16u + wire[4]; i < 48u; ++i) if (wire[i]) return 0;
    for (uint32_t i = 49u; i < 60u; ++i) if (wire[i]) return 0;
    return 1;
}

int keypad_trace_accept(keypad_trace_t *trace, const uint8_t wire[64]) {
    if (!keypad_trace_wire_valid(wire)) { trace->error = 1u; return 0; }
    if (wire[6] == KEYPAD_TRACE_CLEAR) {
        keypad_trace_init(trace);
        return 1;
    }
    uint32_t sequence = badger_bytes_load_u32(wire + 12u);
    if (trace->total == 0xffffffffu || sequence != trace->total + 1u) {
        trace->error = 2u;
        return 0;
    }
    keypad_trace_event_t *event = trace->events + trace->next;
    badger_bytes_copy(event->wire, wire, 64u);
    event->key = (int8_t)decode_button(wire[5], wire + 16u, wire[4]);
    event->navigation = (int8_t)decode_navigation(wire[48], wire[5], wire + 16u, wire[4]);
    if (event->key < 0 && event->navigation < 0) ++trace->unknown;
    trace->next = (uint8_t)((trace->next + 1u) % KEYPAD_TRACE_CAPACITY);
    if (trace->count < KEYPAD_TRACE_CAPACITY) ++trace->count;
    trace->total = sequence;
    if (!trace->frozen) trace->selected = sequence;
    uint32_t oldest = trace->total - trace->count + 1u;
    if (trace->selected < oldest) trace->selected = oldest;
    trace->error = 0u;
    return 1;
}

const keypad_trace_event_t *keypad_trace_selected(const keypad_trace_t *trace) {
    if (!trace->count || trace->selected > trace->total ||
        trace->selected < trace->total - trace->count + 1u) return 0;
    uint32_t age = trace->total - trace->selected;
    return trace->events + (trace->next + KEYPAD_TRACE_CAPACITY - 1u - age) % KEYPAD_TRACE_CAPACITY;
}

void keypad_trace_browse(keypad_trace_t *trace, int direction) {
    if (!trace->count) return;
    trace->frozen = 1u;
    uint32_t oldest = trace->total - trace->count + 1u;
    if (direction < 0 && trace->selected > oldest) --trace->selected;
    if (direction > 0 && trace->selected < trace->total) ++trace->selected;
}

void keypad_trace_status(const keypad_trace_t *trace, uint8_t status[64],
                          uint32_t build, uint32_t rejected) {
    badger_bytes_zero(status, 64u);
    badger_bytes_store_u32(status, KEYPAD_STATUS_MAGIC);
    badger_bytes_store_u32(status + 4u, trace->total);
    badger_bytes_store_u32(status + 8u, trace->count);
    badger_bytes_store_u32(status + 12u, trace->total - trace->count);
    badger_bytes_store_u32(status + 16u, trace->unknown);
    badger_bytes_store_u32(status + 20u, trace->selected);
    badger_bytes_store_u32(status + 24u, trace->frozen);
    badger_bytes_store_u32(status + 28u, trace->error);
    badger_bytes_store_u32(status + 32u, build);
    badger_bytes_store_u32(status + 36u, rejected);
    finish(status);
}

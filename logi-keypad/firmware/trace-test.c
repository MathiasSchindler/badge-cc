#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "trace.h"
#include "../../src/usb/live_usb.h"

static live_usb_control_t usb;
static keypad_trace_t trace;
static const uint32_t build = 0x12345678u;

static void setup(uint8_t type, uint8_t request, uint16_t value, uint16_t length) {
    const uint8_t packet[] = {type, request, (uint8_t)value, (uint8_t)(value >> 8u),
                              0u, 0u, (uint8_t)length, (uint8_t)(length >> 8u)};
    live_usb_control_setup(&usb, packet);
}

static void configure(void) {
    setup(0u, 5u, 1u, 0u);
    live_usb_control_complete(&usb, 1u, NULL, 0u);
    setup(0u, 9u, 1u, 0u);
    live_usb_control_complete(&usb, 1u, NULL, 0u);
}

static void receive(const uint8_t wire[64]) {
    setup(0x40u, KEYPAD_TRACE_SUBMIT, 0u, 64u);
    assert(usb.action == USB_ACTION_OUT);
    live_usb_control_complete(&usb, 0u, wire, 64u);
}

static void application(void) {
    if (usb.keypad_ready) {
        usb.keypad_ready = 0u;
        assert(keypad_trace_accept(&trace, usb.keypad_record));
    }
    keypad_trace_status(&trace, usb.keypad_status, build, usb.rejected);
    usb.keypad_status_valid = 1u;
}

int main(void) {
    uint8_t raw[32] = {0x13, 0xff, 2, 0, 0, 1, 1}, wire[64], mutated[64];
    keypad_trace_init(&trace);
    assert(!keypad_trace_selected(&trace));
    assert(!keypad_trace_record(wire, raw, 33u, 0u, 0u, 1u));
    assert(!keypad_trace_record(wire, raw, 0u, 0u, 0u, 1u));
    assert(!keypad_trace_record(wire, raw, 32u, 0u, 0u, 0u));
    assert(keypad_trace_record(wire, raw, 32u, 0u, 123u, 1u));
    assert(keypad_trace_wire_valid(wire));
    for (unsigned i = 0; i < 64u; ++i) {
        memcpy(mutated, wire, 64u); mutated[i] ^= 1u;
        assert(!keypad_trace_wire_valid(mutated));
    }
    assert(keypad_trace_accept(&trace, wire));
    assert(trace.total == 1u && !trace.unknown && trace.selected == 1u);
    assert(keypad_trace_selected(&trace)->key == 1);
    assert(!keypad_trace_accept(&trace, wire) && trace.error == 2u);
    keypad_trace_browse(&trace, -1);
    assert(trace.frozen && trace.selected == 1u);
    for (unsigned i = 2; i <= 70u; ++i) {
        raw[6] = (uint8_t)(i % 10u);
        assert(keypad_trace_record(wire, raw, 32u, 0u, i, i));
        assert(keypad_trace_accept(&trace, wire));
    }
    assert(trace.total == 70u && trace.count == 64u && trace.selected == 7u);
    assert(badger_bytes_load_u32(keypad_trace_selected(&trace)->wire + 12u) == 7u);
    keypad_trace_browse(&trace, -1); assert(trace.selected == 7u);
    for (unsigned i = 0; i < 100u; ++i) keypad_trace_browse(&trace, 1);
    assert(trace.selected == 70u);
    assert(keypad_trace_selected(&trace)->key == 0);
    uint8_t nav[20] = {0x11, 0xff, 0x0b, 0, 1, 0xa1};
    assert(keypad_trace_record(wire, nav, 20u, 0x0bu, 100u, 71u));
    assert(keypad_trace_accept(&trace, wire));
    keypad_trace_browse(&trace, 1);
    assert(keypad_trace_selected(&trace)->navigation == 1);
    nav[3] = 0x32;
    assert(keypad_trace_record(wire, nav, 20u, 0x0bu, 100u, 72u));
    assert(keypad_trace_accept(&trace, wire) && trace.unknown == 1u);
    keypad_trace_status(&trace, mutated, build, 5u);
    assert(badger_bytes_load_u32(mutated) == KEYPAD_STATUS_MAGIC);
    assert(badger_bytes_load_u32(mutated + 4u) == 72u);
    assert(badger_bytes_load_u32(mutated + 8u) == 64u);
    assert(badger_bytes_load_u32(mutated + 12u) == 8u);
    assert(badger_bytes_load_u32(mutated + 60u) == diag_crc32(mutated, 60u));
    keypad_trace_clear_wire(wire);
    assert(keypad_trace_accept(&trace, wire) && trace.total == 0u && trace.count == 0u);
    wire[7] = 1u;
    badger_bytes_store_u32(wire + 60u, diag_crc32(wire, 60u));
    assert(!keypad_trace_wire_valid(wire));
    assert(keypad_trace_record(wire, raw, 32u, 0u, 123u, 1u));
    live_usb_control_init(&usb, build, "C31AAADD5199B2C7");
    setup(0x40u, KEYPAD_TRACE_SUBMIT, 0u, 64u);
    assert(usb.action == USB_ACTION_STALL);
    configure();
    memcpy(mutated, wire, 64u);
    mutated[7] = 2u;
    badger_bytes_store_u32(mutated + 60u, diag_crc32(mutated, 60u));
    assert(keypad_trace_wire_valid(mutated));
    receive(mutated);
    assert(usb.action == USB_ACTION_STALL && !usb.keypad_pending && !usb.keypad_ready);
    setup(0x40u, KEYPAD_TRACE_SUBMIT, 1u, 64u); assert(usb.action == USB_ACTION_STALL);
    setup(0x40u, KEYPAD_TRACE_SUBMIT, 0u, 63u); assert(usb.action == USB_ACTION_STALL);
    receive(wire);
    assert(usb.keypad_pending && !usb.keypad_ready && trace.total == 0u);
    setup(0xc0u, LIVE_USB_INFO, 0u, 12u);
    assert(!usb.keypad_pending && !usb.keypad_ready);
    receive(wire);
    live_usb_control_complete(&usb, 1u, NULL, 0u);
    assert(usb.keypad_ready);
    setup(0x40u, KEYPAD_TRACE_SUBMIT, 0u, 64u);
    assert(usb.action == USB_ACTION_STALL && usb.keypad_ready);
    application();
    assert(trace.total == 1u);
    setup(0xc0u, KEYPAD_TRACE_STATUS, 0u, 64u);
    assert(usb.action == USB_ACTION_IN && !memcmp(usb.data, usb.keypad_status, 64u));
    live_usb_control_complete(&usb, 1u, NULL, 64u);
    live_usb_control_complete(&usb, 0u, NULL, 0u);
    for (unsigned i = 0; i < 64u; ++i) {
        memcpy(mutated, wire, 64u); mutated[i] ^= 1u;
        receive(mutated);
        assert(usb.action == USB_ACTION_STALL && !usb.keypad_pending && !usb.keypad_ready);
    }
    setup(0x40u, KEYPAD_TRACE_SUBMIT, 0u, 64u);
    live_usb_control_complete(&usb, 0u, wire, 63u);
    assert(usb.action == USB_ACTION_STALL && !usb.keypad_pending && !usb.keypad_out);
    receive(wire);
    live_usb_control_reset(&usb);
    assert(!usb.keypad_pending && !usb.keypad_ready && !usb.keypad_status_valid);
    configure(); receive(wire);
    live_usb_control_complete(&usb, 1u, NULL, 0u);
    setup(0u, 9u, 0u, 0u);
    live_usb_control_complete(&usb, 1u, NULL, 0u);
    assert(!usb.keypad_pending && !usb.keypad_ready && !usb.keypad_status_valid);
    puts("Trace: raw decoding, CRC, sequence, ring overwrite/browse, clear, EP0 commit/abort/reset PASS");
    return 0;
}

#define main replay_main
#include "keypad-replay.c"
#undef main
#include <assert.h>
#include "../src/usb/live_usb.h"

static live_usb_control_t mock_usb;
static keypad_trace_t mock_trace;
static uint8_t defer_application, corrupt_status, fail_transfer;

static void mock_application(void) {
    if (mock_usb.keypad_ready) {
        mock_usb.keypad_ready = 0u;
        assert(keypad_trace_accept(&mock_trace, mock_usb.keypad_record));
    }
    keypad_trace_status(&mock_trace, mock_usb.keypad_status, USB_REPORT_BUILD_ID, mock_usb.rejected);
    mock_usb.keypad_status_valid = 1u;
}

static IOReturn mock_request(void *self, IOUSBDevRequestTO *request) {
    (void)self;
    if (fail_transfer) return kIOReturnNotResponding;
    assert(request->wLength == 64u && request->wValue == 0u && request->wIndex == 0u);
    uint8_t setup[] = {request->bmRequestType, request->bRequest, 0, 0, 0, 0, 64, 0};
    live_usb_control_setup(&mock_usb, setup);
    if (mock_usb.action == USB_ACTION_STALL) return kIOUSBPipeStalled;
    if (request->bmRequestType == 0xc0u) {
        assert(mock_usb.action == USB_ACTION_IN && mock_usb.length == 64u);
        memcpy(request->pData, mock_usb.data, 64u);
        if (corrupt_status) ((uint8_t *)request->pData)[32] ^= 1u;
        live_usb_control_complete(&mock_usb, 1u, NULL, 64u);
        live_usb_control_complete(&mock_usb, 0u, NULL, 0u);
        mock_application();
    } else {
        assert(mock_usb.action == USB_ACTION_OUT);
        live_usb_control_complete(&mock_usb, 0u, request->pData, 64u);
        if (mock_usb.action == USB_ACTION_STALL) return kIOUSBPipeStalled;
        live_usb_control_complete(&mock_usb, 1u, NULL, 0u);
        if (!defer_application) mock_application();
    }
    request->wLenDone = 64u;
    return kIOReturnSuccess;
}

static void test_transport(void) {
    IOUSBDeviceInterface500 interface = {.DeviceRequestTO = mock_request};
    IOUSBDeviceInterface500 *pointer = &interface;
    uint8_t wire[64], raw[32] = {0x13, 0xff, 2, 0, 0, 1, 5}, status[64];
    keypad_trace_t expected;
    keypad_trace_init(&expected);
    keypad_trace_init(&mock_trace);
    live_usb_control_init(&mock_usb, USB_REPORT_BUILD_ID, "C31AAADD5199B2C7");
    mock_usb.address = mock_usb.configured = 1u;
    mock_application();
    assert(read_status(&pointer, status));
    assert(keypad_trace_record(wire, raw, 32u, 0u, 123u, 1u));
    assert(keypad_trace_accept(&expected, wire));
    defer_application = 1u;
    assert(submit(&pointer, wire, &expected));
    assert(mock_trace.total == 1u && keypad_trace_selected(&mock_trace)->key == 5);
    keypad_trace_init(&expected);
    keypad_trace_clear_wire(wire);
    assert(submit(&pointer, wire, &expected));
    assert(mock_trace.total == 0u);
    puts("The next USB/protocol errors are expected.");
    corrupt_status = 1u;
    assert(!read_status(&pointer, status));
    corrupt_status = 0u;
    fail_transfer = 1u;
    assert(!read_status(&pointer, status));
    fail_transfer = 0u;
    mock_usb.build_id ^= 1u;
    keypad_trace_status(&mock_trace, mock_usb.keypad_status, mock_usb.build_id, 0u);
    assert(!read_status(&pointer, status));
}

int main(void) {
    uint8_t wire[64];
    const char *valid = "+1.234 input id=0x11 length=20 raw="
        "11 ff 0b 00 01 a1 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n";
    assert(parse_input(valid, wire, 1u, 0x0bu) == 1);
    assert(keypad_trace_wire_valid(wire));
    assert(badger_bytes_load_u32(wire + 8u) == 1234u && wire[48] == 0x0bu);
    assert(decode_navigation(wire[48], wire[5], wire + 16u, wire[4]) == 1);
    assert(parse_input("  decoded: release\n", wire, 1u, 0u) == 0);
    const char *bad[] = {
        "+nan input id=0x11 length=1 raw=11",
        "+inf input id=0x11 length=1 raw=11",
        "+-1 input id=0x11 length=1 raw=11",
        "+4294968 input id=0x11 length=1 raw=11",
        "+0 input id=0x100 length=1 raw=00",
        "+0 input id=0x100000011 length=1 raw=11",
        "+0 input id=0x11 length=4294967297 raw=11",
        "+0 input id=0x11 length=9999999999999999999999999 raw=11",
        "+0 input id=0x11 length=0 raw=",
        "+0 input id=0x11 length=33 raw=11",
        "+0 input id=0x11 length=2 raw=11",
        "+0 input id=0x11 length=1 raw=1",
        "+0 input id=0x11 length=1 raw=zz",
        "+0 input id=0x11 length=1 raw=110",
        "+0 input id=0x11 length=1 raw=11 00",
        "+0 input id=0x11 length=1 raw=12",
        "input id=0x11 length=1 raw=11"
    };
    for (unsigned i = 0u; i < sizeof(bad) / sizeof(*bad); ++i)
        assert(parse_input(bad[i], wire, 1u, 0u) == -1);
    assert(parse_input("+0 input id=0x11 length=1 raw=11", wire, 1u, 0u) == 1);
    char *help[] = {"replay", "--help"};
    assert(replay_main(2, help) == 0);
    char *missing[] = {"replay", "--replay", "file"};
    assert(replay_main(3, missing) == 2);
    char *wrong[] = {"replay", "--validate", "file", "--badge", "serial"};
    assert(replay_main(5, wrong) == 2);
    test_transport();
    puts("Replay: strict parser, metadata, CLI, real EP0 round trip/readback and error handling PASS");
    return 0;
}

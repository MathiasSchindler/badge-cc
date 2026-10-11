#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../usb/live_usb.h"
#include "../../tools/compiler_test/suite.h"

static live_usb_control_t usb;
static const uint32_t build = 0x12345678u;

static void setup(uint8_t type, uint8_t command, uint16_t page, uint16_t length) {
    uint8_t packet[8] = {type, command, (uint8_t)page, (uint8_t)(page >> 8u), 0u, 0u,
                        (uint8_t)length, (uint8_t)(length >> 8u)};
    live_usb_control_setup(&usb, packet);
}

static void configure(void) {
    setup(0u, 5u, 1u, 0u);
    live_usb_control_complete(&usb, 1u, NULL, 0u);
    setup(0u, 9u, 1u, 0u);
    live_usb_control_complete(&usb, 1u, NULL, 0u);
    assert(usb.configured);
}

int main(void) {
    uint8_t nonce[16] = {1u}, wire[64];
    live_usb_control_init(&usb, build, "08DA26F86E57F5EA");
    configure();
    setup(0xc0u, LIVE_USB_COMPILER_PAGE, 0u, 64u);
    assert(usb.action == USB_ACTION_STALL);
    usb_report_request_for_test(wire, build, nonce, COMPILER_SUITE_ID);
    for (uint32_t byte = 0u; byte < 64u; ++byte) {
        wire[byte] ^= 1u;
        setup(0x40u, LIVE_USB_COMPILER_SUBMIT, 0u, 64u);
        live_usb_control_complete(&usb, 0u, wire, 64u);
        assert(usb.action == USB_ACTION_STALL && !usb.compiler_ready && !usb.compiler_valid);
        wire[byte] ^= 1u;
    }
    setup(0x40u, LIVE_USB_COMPILER_SUBMIT, 0u, 64u);
    live_usb_control_complete(&usb, 0u, wire, 64u);
    assert(usb.compiler_pending && !usb.compiler_ready);
    uint32_t generation = usb.compiler_generation;
    live_usb_control_complete(&usb, 1u, NULL, 0u);
    assert(usb.compiler_ready && memcmp(usb.compiler_nonce, nonce, 16u) == 0);
    compiler_summary(usb.compiler_records, build, nonce, USB_REPORT_OK, 0u);
    usb.compiler_valid = 1u;
    setup(0xc0u, LIVE_USB_COMPILER_PAGE, 0u, 64u);
    assert(usb.action == USB_ACTION_IN);
    assert(memcmp(usb.data, usb.compiler_records, 64u) == 0);
    live_usb_control_complete(&usb, 1u, NULL, 64u);
    live_usb_control_complete(&usb, 0u, NULL, 0u);
    uint32_t values[7] = {1000u};
    uint32_t last_page = COMPILER_SUITE_RECORDS - 1u;
    compiler_data_record(usb.compiler_records + last_page * 64u, build, nonce, last_page, values);
    setup(0xc0u, LIVE_USB_COMPILER_PAGE, (uint16_t)last_page, 64u);
    assert(usb.action == USB_ACTION_IN &&
           memcmp(usb.data, usb.compiler_records + last_page * 64u, 64u) == 0);
    live_usb_control_complete(&usb, 1u, NULL, 64u);
    live_usb_control_complete(&usb, 0u, NULL, 0u);
    setup(0xc0u, LIVE_USB_COMPILER_PAGE, COMPILER_SUITE_RECORDS, 64u);
    assert(usb.action == USB_ACTION_STALL);
    setup(0xc0u, LIVE_USB_COMPILER_PAGE, 0u, 63u);
    assert(usb.action == USB_ACTION_STALL);
    setup(0x40u, LIVE_USB_COMPILER_SUBMIT, 0u, 64u);
    assert(usb.compiler_generation != generation && !usb.compiler_valid);
    live_usb_control_complete(&usb, 0u, wire, 63u);
    assert(usb.action == USB_ACTION_STALL && !usb.compiler_ready);
    setup(0x40u, LIVE_USB_COMPILER_SUBMIT, 0u, 64u);
    live_usb_control_complete(&usb, 0u, wire, 64u);
    setup(0xc0u, LIVE_USB_INFO, 0u, 12u);
    assert(!usb.compiler_pending && !usb.compiler_ready);
    live_usb_control_reset(&usb);
    assert(!usb.compiler_ready && !usb.compiler_valid && !usb.configured);
    configure();
    setup(0x40u, LIVE_USB_COMPILER_SUBMIT, 0u, 64u);
    live_usb_control_complete(&usb, 0u, wire, 64u);
    live_usb_control_complete(&usb, 1u, NULL, 0u);
    setup(0u, 9u, 0u, 0u);
    live_usb_control_complete(&usb, 1u, NULL, 0u);
    assert(!usb.compiler_ready && !usb.compiler_valid);
    puts("compiler USB: corruption, deferred submission, pages, reset/configuration and stale generation PASS");
    return 0;
}

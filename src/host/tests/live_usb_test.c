#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../usb/live_usb.h"
#include "../../radio/wifi_usb_report.h"
#include "../../tools/diagnostics_logic.h"

static live_usb_control_t control;
static const uint32_t build = 0x12345678u;

static void setup(uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint16_t length) {
    const uint8_t packet[] = {type, request, (uint8_t)value, (uint8_t)(value >> 8u),
        (uint8_t)index, (uint8_t)(index >> 8u), (uint8_t)length, (uint8_t)(length >> 8u)};
    live_usb_control_setup(&control, packet);
}

static void finish_in(void) {
    live_usb_control_complete(&control, 1u, NULL, control.length);
    assert(control.action == USB_ACTION_OUT && control.length == 0u);
    live_usb_control_complete(&control, 0u, NULL, 0u);
    assert(control.phase == USB_PHASE_IDLE);
}

static void configured(void) {
    setup(0u, 5u, 12u, 0u, 0u);
    assert(control.address == 0u && control.address_pending);
    live_usb_control_complete(&control, 1u, NULL, 0u);
    assert(control.action == USB_ACTION_ADDRESS && control.address == 12u);
    setup(0u, 9u, 1u, 0u, 0u);
    assert(!control.configured);
    live_usb_control_complete(&control, 1u, NULL, 0u);
    assert(control.configured);
}

static void descriptors(void) {
    live_usb_control_init(&control, build, "08DA26F86E57F5EA");
    setup(0x80u, 6u, 0x100u, 0u, 8u);
    assert(control.action == USB_ACTION_IN && control.length == 8u);
    assert(control.data[0] == 18u && control.data[7] == 64u);
    finish_in();
    setup(0x80u, 6u, 0x100u, 0u, 64u);
    assert(control.length == 18u);
    assert(control.data[8] == (LIVE_USB_VID & 255u));
    finish_in();
    setup(0x80u, 6u, 0x200u, 0u, 255u);
    assert(control.length == 18u && control.data[2] == 18u);
    assert(control.data[13] == 0u && control.data[14] == 0xffu);
    finish_in();
    for (uint16_t i = 0u; i < 4u; ++i) {
        setup(0x80u, 6u, (uint16_t)(0x300u + i), i ? 0x409u : 0u, 255u);
        assert(control.length >= 4u && control.length <= 64u);
        assert(control.data[0] == control.length && control.data[1] == 3u);
        if (i == 3u) {
            assert(control.length == 34u);
            for (unsigned c = 0u; c < 16u; ++c) assert(control.data[2u + c * 2u] == (uint8_t)control.serial[c]);
        }
        finish_in();
    }
    setup(0x80u, 6u, 0x600u, 0u, 10u);
    assert(control.action == USB_ACTION_STALL);
    setup(0x80u, 6u, 0x100u, 0u, 0u);
    assert(control.action == USB_ACTION_STALL);
    setup(0x80u, 6u, 0x100u, 0u, 18u);
    assert(control.action == USB_ACTION_IN);
    finish_in();
    setup(0u, 5u, 128u, 0u, 0u);
    assert(control.action == USB_ACTION_STALL);
    setup(0u, 5u, 13u, 0u, 0u);
    setup(0x80u, 6u, 0x100u, 0u, 18u);
    finish_in();
    assert(control.address == 0u && !control.address_pending);
    configured();
}

static void report_tests(void) {
    uint8_t nonce[USB_REPORT_NONCE_SIZE] = {1u};
    uint8_t request[USB_REPORT_WIRE_SIZE], response[USB_REPORT_WIRE_SIZE];
    setup(0xc0u, LIVE_USB_REPORT, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_STALL);
    for (unsigned i = 0u; i < 20u; ++i) {
        nonce[0] = (uint8_t)(i + 1u);
        usb_report_request(request, build, nonce);
        setup(0x40u, LIVE_USB_SUBMIT, 0u, 0u, 64u);
        assert(control.action == USB_ACTION_OUT && control.length == 64u && !control.report_valid);
        live_usb_control_complete(&control, 0u, request, sizeof(request));
        assert(control.action == USB_ACTION_IN && control.phase == USB_PHASE_STATUS_IN);
        live_usb_control_complete(&control, 1u, NULL, 0u);
        setup(0xc0u, LIVE_USB_REPORT, 0u, 0u, 64u);
        assert(control.action == USB_ACTION_IN && control.length == 64u);
        memcpy(response, control.data, sizeof(response));
        assert(usb_report_check_response(response, sizeof(response), request, sizeof(request), build) == USB_RECORD_OK);
        finish_in();
    }
    assert(control.requests == 20u);
    for (unsigned byte = 0u; byte < USB_REPORT_WIRE_SIZE; ++byte) {
        request[byte] ^= 1u;
        setup(0x40u, LIVE_USB_SUBMIT, 0u, 0u, 64u);
        live_usb_control_complete(&control, 0u, request, sizeof(request));
        assert(control.action == USB_ACTION_STALL && !control.report_valid);
        setup(0xc0u, LIVE_USB_REPORT, 0u, 0u, 64u);
        assert(control.action == USB_ACTION_STALL);
        request[byte] ^= 1u;
    }
    setup(0x40u, LIVE_USB_SUBMIT, 0u, 0u, 64u);
    live_usb_control_complete(&control, 0u, request, 63u);
    assert(control.action == USB_ACTION_STALL);
    setup(0x40u, LIVE_USB_SUBMIT, 0u, 0u, 65u);
    assert(control.action == USB_ACTION_STALL);
    setup(0xc0u, LIVE_USB_INFO, 0u, 0u, 12u);
    assert(control.action == USB_ACTION_IN && memcmp(control.data, "U26L", 4u) == 0);
    assert(usb_report_u32(control.data + 8u) == build);
    finish_in();
    setup(0x40u, LIVE_USB_BOOTSEL, 0u, 0u, 0u);
    assert(!control.reboot_requested && control.reboot_pending);
    setup(0xc0u, LIVE_USB_INFO, 0u, 0u, 12u);
    finish_in();
    assert(!control.reboot_requested && !control.reboot_pending);
    setup(0x40u, LIVE_USB_BOOTSEL, 0u, 0u, 0u);
    live_usb_control_complete(&control, 1u, NULL, 0u);
    assert(control.reboot_requested);
    live_usb_control_reset(&control);
    assert(!control.configured && !control.address && !control.report_valid && !control.reboot_requested);
    setup(0x40u, LIVE_USB_SUBMIT, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_STALL);
}

static void wifi_tests(void) {
    live_usb_control_reset(&control);
    configured();
    uint8_t request[64], nonce[16] = {9u}, taken[16], reply[64];
    usb_report_request_for_test(request, build, nonce, WIFI_USB_TEST_ID);
    assert(usb_report_check_request(request, 64u, build) == USB_RECORD_TEST);
    setup(0x40u, LIVE_USB_WIFI_SUBMIT, 0u, 0u, 64u);
    assert(!control.probe_pio);
    live_usb_control_complete(&control, 0u, request, 64u);
    assert(control.probe_pending && !control.probe_ready);
    assert(live_usb_probe_take(&control, taken) == 0u);
    live_usb_control_complete(&control, 1u, NULL, 0u);
    uint32_t generation = live_usb_probe_take(&control, taken);
    assert(generation != 0u && memcmp(taken, nonce, 16u) == 0);
    assert(live_usb_probe_take(&control, taken) == 0u);
    setup(0xc0u, LIVE_USB_WIFI_REPORT, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_STALL);
    wifi_probe_report_t probe = {.stage = WIFI_PROBE_TEST16, .result = WIFI_BUS_TEST_MISMATCH,
        .reset_pattern = 0xffffffffu, .last_value = 0xffffffffu, .reset_attempts = 10u, .elapsed_us = 300000u};
    wifi_usb_report_encode(reply, build, nonce, &probe);
    live_usb_probe_finish(&control, generation, reply);
    assert(control.report_valid);
    uint8_t trace[WIFI_TRACE_SIZE], collected[WIFI_TRACE_SIZE] = {0};
    for (uint32_t i = 0u; i < WIFI_TRACE_SIZE; ++i) trace[i] = (uint8_t)i;
    setup(0xc0u, LIVE_USB_WIFI_TRACE, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_STALL);
    live_usb_trace_finish(&control, generation + 1u, trace);
    assert(!control.trace_valid);
    live_usb_trace_finish(&control, generation, trace);
    for (uint8_t page = 0u; page < WIFI_TRACE_PAGES; ++page) {
        setup(0xc0u, LIVE_USB_WIFI_TRACE, page, 0u, 64u);
        assert(control.action == USB_ACTION_IN);
        assert(wifi_trace_page_decode(collected, control.data, 64u, reply, page) == USB_RECORD_OK);
        assert(wifi_trace_page_decode(collected, control.data, 63u, reply, page) == USB_RECORD_LENGTH);
        assert(wifi_trace_page_decode(collected, control.data, 64u, reply, (uint8_t)((page+1u)%11u)) != USB_RECORD_OK);
        for (uint32_t i = 0u; i < 64u; ++i) {
            control.data[i] ^= 1u;
            assert(wifi_trace_page_decode(collected, control.data, 64u, reply, page) != USB_RECORD_OK);
            control.data[i] ^= 1u;
        }
        finish_in();
    }
    assert(memcmp(collected, trace, sizeof(trace)) == 0);
    setup(0xc0u, LIVE_USB_WIFI_TRACE, 11u, 0u, 64u);
    assert(control.action == USB_ACTION_STALL);
    uint32_t requests_after_finish = control.requests;
    live_usb_probe_finish(&control, generation, reply);
    assert(control.requests == requests_after_finish);
    setup(0xc0u, LIVE_USB_REPORT, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_STALL);
    setup(0xc0u, LIVE_USB_WIFI_REPORT, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_IN);
    wifi_probe_report_t decoded;
    assert(wifi_usb_report_decode(control.data, 64u, request, build, &decoded) == USB_RECORD_OK);
    assert(decoded.result == WIFI_BUS_TEST_MISMATCH && decoded.reset_pattern == 0xffffffffu);
    assert(decoded.reset_attempts == 10u && decoded.elapsed_us == 300000u);
    finish_in();
    for (unsigned i = 0u; i < 64u; ++i) {
        reply[i] ^= 1u;
        assert(wifi_usb_report_decode(reply, 64u, request, build, &decoded) != USB_RECORD_OK);
        reply[i] ^= 1u;
    }
    for (unsigned len = 0u; len < 64u; ++len) {
        assert(wifi_usb_report_decode(reply, len, request, build, &decoded) == USB_RECORD_LENGTH);
    }
    uint8_t different[16] = {10u};
    wifi_usb_report_encode(reply, build, different, &probe);
    assert(wifi_usb_report_decode(reply, 64u, request, build, &decoded) == USB_RECORD_NONCE);
    probe.stage = WIFI_PROBE_FINISHED;
    probe.result = WIFI_BUS_OK;
    probe.passed_reads = 16u;
    probe.reset_attempts = 1u;
    probe.normal_pattern = probe.reset_pattern = WIFI_GSPI_TEST_PATTERN;
    probe.clock_csr = 0x48u;
    probe.bus_control = WIFI_GSPI_BUS_CONFIG;
    probe.chip_id = 0x1234a9afu;
    probe.powered = 1u;
    wifi_usb_report_encode(reply, build, nonce, &probe);
    assert(wifi_usb_report_decode(reply, 64u, request, build, &decoded) == USB_RECORD_OK);
    assert(decoded.clock_csr == 0x48u && decoded.powered == 1u);
    live_usb_control_reset(&control);
    assert(!control.trace_valid);
    configured();
    live_usb_probe_finish(&control, generation, reply);
    assert(!control.report_valid);
    setup(0x40u, LIVE_USB_WIFI_PIO_SUBMIT, 0u, 0u, 64u);
    assert(control.probe_pio);
    usb_report_request_for_test(request, build, nonce, WIFI_USB_TEST_ID);
    live_usb_control_complete(&control, 0u, request, 64u);
    live_usb_control_complete(&control, 1u, NULL, 0u);
    assert(live_usb_probe_take(&control, taken) != 0u && control.probe_pio);
    setup(0x40u, LIVE_USB_WIFI_SUBMIT, 0u, 0u, 64u);
    live_usb_control_complete(&control, 0u, request, 64u);
    setup(0xc0u, LIVE_USB_INFO, 0u, 0u, 12u);
    finish_in();
    assert(!control.probe_ready && !control.probe_pending);
    setup(0x40u, LIVE_USB_WIFI_SUBMIT, 0u, 0u, 64u);
    live_usb_control_complete(&control, 0u, request, 64u);
    live_usb_control_complete(&control, 1u, NULL, 0u);
    generation = live_usb_probe_take(&control, taken);
    usb_report_request(request, build, nonce);
    setup(0x40u, LIVE_USB_SUBMIT, 0u, 0u, 64u);
    live_usb_probe_finish(&control, generation, reply);
    assert(!control.report_valid);
}

int main(void) {
    live_usb_control_t polling = {0};
    assert(live_usb_control_poll_immediate(&polling));
    polling.configured = 1u;
    assert(!live_usb_control_poll_immediate(&polling));
    polling.phase = USB_PHASE_DATA_IN;
    assert(live_usb_control_poll_immediate(&polling));
    polling.phase = USB_PHASE_DATA_OUT;
    assert(live_usb_control_poll_immediate(&polling));
    polling.phase = USB_PHASE_STATUS_IN;
    assert(live_usb_control_poll_immediate(&polling));
    polling.phase = USB_PHASE_STATUS_OUT;
    assert(live_usb_control_poll_immediate(&polling));
    polling.phase = USB_PHASE_STALL;
    assert(live_usb_control_poll_immediate(&polling));
    descriptors();
    report_tests();
    wifi_tests();
    live_usb_control_reset(&control);
    configured();
    setup(0x40u, LIVE_USB_FW_SUBMIT, 0u, 0u, 64u);
#ifdef LIVE_USB_FIRMWARE
    assert(control.incoming_test == 3u);
    uint8_t request[64], nonce[16] = {3u}, taken[16];
    usb_report_request_for_test(request, build, nonce, 3u);
    live_usb_control_complete(&control, 0u, request, 64u);
    assert(live_usb_probe_take(&control, taken) == 0u);
    live_usb_control_complete(&control, 1u, NULL, 0u);
    uint32_t generation = live_usb_probe_take(&control, taken);
    assert(generation != 0u);
    setup(0xc0u, LIVE_USB_FW_REPORT, 0u, 0u, 64u);
    assert(control.phase == USB_PHASE_STALL);
    live_usb_probe_finish(&control, generation, request);
    assert(control.report_valid && control.report_test == 3u);
    setup(0xc0u, LIVE_USB_WIFI_REPORT, 0u, 0u, 64u);
    assert(control.phase == USB_PHASE_STALL);
    setup(0xc0u, LIVE_USB_FW_REPORT, 0u, 0u, 64u);
    assert(control.phase != USB_PHASE_STALL);
    finish_in();
    live_usb_control_reset(&control);
    configured();
    live_usb_probe_finish(&control, generation, request);
    assert(!control.report_valid);
#else
    assert(control.phase == USB_PHASE_STALL);
#endif
    puts("live_usb: descriptors, deferred address/config, EP0 stages, fresh reports, malformed/stale rejection PASS");
    return 0;
}

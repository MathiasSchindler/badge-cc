#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "usb/control.h"

static usb_control_t control;
static const uint32_t build_id = 0x12345678u;
static const uint32_t chip_id[2] = {0xb9492c77u, 0x7c4918bau};

static void setup(uint8_t type, uint8_t request, uint16_t value,
                  uint16_t index, uint16_t length) {
    const uint8_t packet[] = {
        type, request, (uint8_t)value, (uint8_t)(value >> 8u),
        (uint8_t)index, (uint8_t)(index >> 8u), (uint8_t)length, (uint8_t)(length >> 8u)
    };
    usb_control_setup(&control, packet);
}

static void finish_in(void) {
    usb_control_complete(&control, 1u, NULL, control.length);
    assert(control.action == USB_ACTION_OUT && control.length == 0u);
    usb_control_complete(&control, 0u, NULL, 0u);
    assert(control.phase == USB_PHASE_IDLE);
}

static void configure(void) {
    setup(0u, 5u, 12u, 0u, 0u);
    assert(control.address == 0u && control.address_pending);
    usb_control_complete(&control, 1u, NULL, 0u);
    assert(control.action == USB_ACTION_ADDRESS && control.address == 12u);
    setup(0u, 9u, 1u, 0u, 0u);
    assert(!control.configured && control.configuration_pending);
    usb_control_complete(&control, 1u, NULL, 0u);
    assert(control.configured);
}

static void repair_crc(uint8_t wire[64]) {
    usb_store_u32(wire + 60u, usb_crc32(wire, 60u));
}

static void codec_tests(void) {
    uint8_t request[64], response[64], changed[64], nonce[16] = {1u};
    assert(usb_crc32((const uint8_t *)"123456789", 9u) == 0xcbf43926u);
    assert(usb_crc32(NULL, 0u) == 0u);
    usb_report_request(request, build_id, nonce);
    assert(usb_report_check_request(request, 64u, build_id) == USB_RECORD_OK);
    assert(usb_report_check_request(NULL, 64u, build_id) == USB_RECORD_LENGTH);
    for (uint32_t length = 0u; length < 64u; ++length)
        assert(usb_report_check_request(request, length, build_id) == USB_RECORD_LENGTH);
    assert(usb_report_check_request(request, 65u, build_id) == USB_RECORD_LENGTH);
    for (uint32_t byte = 0u; byte < 64u; ++byte) {
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            memcpy(changed, request, sizeof(changed));
            changed[byte] ^= (uint8_t)(1u << bit);
            assert(usb_report_check_request(changed, 64u, build_id) != USB_RECORD_OK);
        }
    }
    static const struct {
        uint8_t offset;
        usb_record_result_t result;
    } mutations[] = {
        {0u, USB_RECORD_MAGIC}, {4u, USB_RECORD_VERSION}, {5u, USB_RECORD_VERSION},
        {6u, USB_RECORD_LENGTH}, {7u, USB_RECORD_LENGTH}, {8u, USB_RECORD_BUILD},
        {12u, USB_RECORD_TEST}, {32u, USB_RECORD_RESERVED}, {59u, USB_RECORD_RESERVED}
    };
    for (uint32_t i = 0u; i < sizeof(mutations) / sizeof(mutations[0]); ++i) {
        memcpy(changed, request, sizeof(changed));
        changed[mutations[i].offset] ^= 1u;
        repair_crc(changed);
        assert(usb_report_check_request(changed, 64u, build_id) == mutations[i].result);
    }
    memset(nonce, 0, sizeof(nonce));
    usb_report_request(changed, build_id, nonce);
    assert(usb_report_check_request(changed, 64u, build_id) == USB_RECORD_NONCE);
    nonce[0] = 1u;
    usb_report_response(response, build_id, nonce, chip_id, 27u);
    assert(usb_load_u32(response) == USB_RESPONSE_MAGIC);
    assert(memcmp(response + 4u, request + 4u, 28u) == 0);
    assert(usb_load_u32(response + 32u) == 1u);
    assert(usb_load_u32(response + 36u) == chip_id[0]);
    assert(usb_load_u32(response + 40u) == chip_id[1]);
    assert(usb_load_u32(response + 44u) == 27u);
    assert(usb_load_u32(response + 48u) == 0x01234567u);
    assert(usb_load_u32(response + 52u) == 0x89abcdefu);
    assert(usb_load_u32(response + 56u) == USB_REPORT_COMMIT);
    assert(usb_load_u32(response + 60u) == usb_crc32(response, 60u));
}

static void descriptor_tests(void) {
    usb_control_init(&control, build_id, chip_id);
    assert(strcmp(control.serial, "7C4918BAB9492C77") == 0);
    setup(0x80u, 6u, 0x100u, 0u, 8u);
    assert(control.action == USB_ACTION_IN && control.length == 8u);
    assert(control.data[0] == 18u && control.data[7] == 64u);
    finish_in();
    setup(0x80u, 6u, 0x100u, 0u, 255u);
    assert(control.length == 18u);
    assert(control.data[8] == (USB_VID & 255u));
    assert(control.data[10] == (USB_PID & 255u));
    finish_in();
    setup(0x80u, 6u, 0x200u, 0u, 255u);
    assert(control.length == 18u && control.data[2] == 18u);
    assert(control.data[13] == 0u && control.data[14] == 0xffu);
    finish_in();
    for (uint16_t number = 0u; number <= 3u; ++number) {
        setup(0x80u, 6u, (uint16_t)(0x300u + number), number ? 0x409u : 0u, 255u);
        assert(control.action == USB_ACTION_IN);
        assert(control.data[0] == control.length && control.data[1] == 3u);
        assert(control.length <= USB_PACKET_SIZE);
        if (number == 3u) {
            assert(control.length == 34u);
            for (uint32_t digit = 0u; digit < 16u; ++digit) {
                assert(control.data[2u + digit * 2u] == (uint8_t)control.serial[digit]);
                assert(control.data[3u + digit * 2u] == 0u);
            }
        }
        finish_in();
    }
    setup(0x80u, 6u, 0x600u, 0u, 10u);
    assert(control.action == USB_ACTION_STALL);
    setup(0x80u, 6u, 0x100u, 0u, 0u);
    assert(control.action == USB_ACTION_STALL);
    setup(0x80u, 6u, 0x100u, 1u, 18u);
    assert(control.action == USB_ACTION_STALL);
    setup(0u, 5u, 128u, 0u, 0u);
    assert(control.action == USB_ACTION_STALL);
    setup(0u, 5u, 7u, 0u, 0u);
    setup(0x80u, 6u, 0x100u, 0u, 18u);
    finish_in();
    assert(control.address == 0u && !control.address_pending);
    setup(0u, 9u, 1u, 0u, 0u);
    assert(control.action == USB_ACTION_STALL);
    configure();
    setup(0x80u, 8u, 0u, 0u, 1u);
    assert(control.length == 1u && control.data[0] == 1u);
    finish_in();
    setup(0u, 5u, 13u, 0u, 0u);
    assert(control.action == USB_ACTION_STALL);
    setup(0x81u, 10u, 0u, 0u, 1u);
    assert(control.data[0] == 0u);
    finish_in();
    setup(1u, 11u, 0u, 0u, 0u);
    assert(control.phase == USB_PHASE_STATUS_IN);
    usb_control_complete(&control, 1u, NULL, 0u);
    assert(control.phase == USB_PHASE_IDLE);
    setup(0x81u, 0u, 0u, 1u, 2u);
    assert(control.action == USB_ACTION_STALL);
}

static void report_tests(void) {
    uint8_t request[64], nonce[16] = {1u};
    setup(0xc0u, USB_REPORT, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_STALL);
    for (uint32_t sequence = 1u; sequence <= 20u; ++sequence) {
        nonce[0] = (uint8_t)sequence;
        usb_report_request(request, build_id, nonce);
        setup(0x40u, USB_SUBMIT, 0u, 0u, 64u);
        assert(control.action == USB_ACTION_OUT && !control.report_valid);
        usb_control_complete(&control, 0u, request, 64u);
        assert(control.phase == USB_PHASE_STATUS_IN && !control.report_valid);
        assert(control.report_pending);
        usb_control_complete(&control, 1u, NULL, 0u);
        assert(control.report_valid && control.requests == sequence);
        setup(0xc0u, USB_REPORT, 0u, 0u, 64u);
        assert(control.action == USB_ACTION_IN && control.length == 64u);
        assert(memcmp(control.data + 16u, nonce, 16u) == 0);
        assert(usb_load_u32(control.data + 44u) == sequence);
        assert(usb_load_u32(control.data + 60u) == usb_crc32(control.data, 60u));
        finish_in();
    }
    for (uint32_t byte = 0u; byte < 64u; ++byte) {
        request[byte] ^= 1u;
        setup(0x40u, USB_SUBMIT, 0u, 0u, 64u);
        usb_control_complete(&control, 0u, request, 64u);
        assert(control.action == USB_ACTION_STALL && !control.report_valid);
        setup(0xc0u, USB_REPORT, 0u, 0u, 64u);
        assert(control.action == USB_ACTION_STALL);
        request[byte] ^= 1u;
    }
    setup(0x40u, USB_SUBMIT, 0u, 0u, 64u);
    usb_control_complete(&control, 0u, request, 64u);
    setup(0xc0u, USB_REPORT, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_STALL && !control.report_valid);
    assert(control.requests == 20u);
    for (uint16_t length = 0u; length < 64u; ++length) {
        setup(0x40u, USB_SUBMIT, 0u, 0u, 64u);
        usb_control_complete(&control, 0u, request, length);
        assert(control.action == USB_ACTION_STALL && !control.report_valid);
    }
    setup(0x40u, USB_SUBMIT, 0u, 0u, 65u);
    assert(control.action == USB_ACTION_STALL);
    setup(0x40u, USB_SUBMIT, 0u, 0u, 64u);
    usb_control_complete(&control, 0u, NULL, 64u);
    assert(control.action == USB_ACTION_STALL);
    setup(0x40u, USB_SUBMIT, 0u, 0u, 64u);
    usb_control_complete(&control, 1u, request, 64u);
    assert(control.action == USB_ACTION_STALL);
    setup(0xc0u, USB_INFO, 1u, 0u, 12u);
    assert(control.action == USB_ACTION_STALL);
    setup(0xc0u, USB_INFO, 0u, 0u, 12u);
    assert(control.length == 12u && memcmp(control.data, "P2WL", 4u) == 0);
    assert(usb_load_u32(control.data + 8u) == build_id);
    finish_in();
}

static void recovery_tests(void) {
    setup(0x40u, USB_BOOTSEL, 0u, 0u, 0u);
    assert(control.reboot_pending && !control.reboot_requested);
    setup(0xc0u, USB_INFO, 0u, 0u, 12u);
    finish_in();
    assert(!control.reboot_requested && !control.reboot_pending);
    setup(0x40u, USB_BOOTSEL, 0u, 0u, 0u);
    usb_control_complete(&control, 1u, NULL, 0u);
    assert(control.reboot_requested);
    usb_control_reset(&control);
    assert(!control.address && !control.configured && !control.report_valid);
    assert(!control.reboot_requested && !control.report_pending);
    setup(0xc0u, USB_INFO, 0u, 0u, 12u);
    assert(control.action == USB_ACTION_STALL);
    configure();
    uint8_t request[64], nonce[16] = {1u};
    usb_report_request(request, build_id, nonce);
    setup(0x40u, USB_SUBMIT, 0u, 0u, 64u);
    usb_control_complete(&control, 0u, request, 64u);
    usb_control_complete(&control, 1u, NULL, 0u);
    assert(control.report_valid);
    setup(0u, 9u, 0u, 0u, 0u);
    assert(control.configured);
    usb_control_complete(&control, 1u, NULL, 0u);
    assert(!control.configured && !control.report_valid);
    setup(0x40u, USB_BOOTSEL, 0u, 0u, 0u);
    assert(control.action == USB_ACTION_STALL && !control.reboot_requested);
    assert(control.rejected > 0u);
}

int main(void) {
    codec_tests();
    descriptor_tests();
    report_tests();
    recovery_tests();
    puts("PASS: CRC, strict records, descriptors, EP0 sequencing, reports, reset and recovery");
    return 0;
}

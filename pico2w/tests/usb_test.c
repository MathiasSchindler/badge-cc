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

static void windows_descriptor_tests(void) {
    usb_control_init(&control, build_id, chip_id);
    setup(0x80u, 6u, 0x3eeu, 0u, 255u);
    assert(control.length == 18u && control.data[16] == 0x57u);
    assert(control.data[2] == 'M' && control.data[14] == '0');
    finish_in();
    setup(0xc0u, 0x57u, 0u, 4u, 255u);
    assert(control.length == 40u && usb_load_u32(control.data) == 40u);
    assert(memcmp(control.data + 18u, "WINUSB", 6u) == 0);
    finish_in();
    const uint16_t lengths[] = {1u, 10u, 63u, 64u, 65u, 128u, 146u, 255u};
    for (uint32_t i = 0u; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        uint8_t result[146];
        uint16_t total = 0u, expected = lengths[i] < 146u ? lengths[i] : 146u;
        uint8_t pid = 1u;
        setup(0xc1u, 0x57u, 0u, 5u, lengths[i]);
        while (control.phase == USB_PHASE_DATA_IN) {
            assert(control.action == USB_ACTION_IN && control.in_pid == pid);
            assert(control.length <= 64u && total + control.length <= expected);
            memcpy(result + total, control.data, control.length);
            total += control.length;
            usb_control_complete(&control, 1u, NULL, control.length);
            pid ^= 1u;
        }
        assert(total == expected && control.phase == USB_PHASE_STATUS_OUT);
        usb_control_complete(&control, 0u, NULL, 0u);
        assert(control.phase == USB_PHASE_IDLE);
        if (total == 146u) {
            assert(usb_load_u32(result) == 146u && result[8] == 1u);
            assert(usb_load_u32(result + 10u) == 136u);
            assert(usb_load_u32(result + 14u) == 7u && result[18] == 42u);
            assert(usb_load_u32(result + 62u) == 80u);
            const char *guid = "{D17518E7-BD1D-4C3D-81A0-5605CAFE4027}";
            for (uint16_t j = 0u; j < 38u; ++j) {
                assert(result[66u + j * 2u] == (uint8_t)guid[j]);
                assert(result[67u + j * 2u] == 0u);
            }
            assert(usb_load_u32(result + 142u) == 0u);
        }
    }
    setup(0xc1u, 0x57u, 0u, 5u, 255u);
    assert(control.in_remaining == 82u);
    setup(0x80u, 6u, 0x100u, 0u, 18u);
    assert(control.in_remaining == 0u);
    finish_in();
    setup(0xc1u, 0x57u, 0u, 5u, 255u);
    usb_control_reset(&control);
    assert(control.in_remaining == 0u && control.in_zlp == 0u);
    setup(0xc1u, 0x57u, 1u, 5u, 146u);
    assert(control.action == USB_ACTION_STALL);
    setup(0xc0u, 0x57u, 0u, 5u, 146u);
    assert(control.action == USB_ACTION_STALL);
    usb_control_init(&control, build_id, chip_id);
    configure();
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

static void lora_request(uint8_t request[64]) {
    const uint8_t nonce[16] = {2u};
    usb_report_request(request, build_id, nonce);
    usb_store_u32(request + 12u, 2u);
    repair_crc(request);
}

static void lora_submit(const uint8_t request[64]) {
    setup(0x40u, USB_LORA_SUBMIT, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_OUT);
    usb_control_complete(&control, 0u, request, 64u);
}

static void lora_tests(void) {
    uint8_t request[64];
    usb_control_init(&control, build_id, chip_id);
    lora_request(request);
    setup(0x40u, USB_LORA_SUBMIT, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_STALL);
    configure();
    setup(0xc0u, USB_LORA_REPORT, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_STALL);
    for (uint32_t i = 0u; i < 64u; ++i) {
        request[i] ^= 1u;
        lora_submit(request);
        assert(control.action == USB_ACTION_STALL && !control.lora_requested);
        request[i] ^= 1u;
    }
    usb_store_u32(request + 12u, 1u);
    repair_crc(request);
    lora_submit(request);
    assert(control.action == USB_ACTION_STALL);
    lora_request(request);
    lora_submit(request);
    assert(control.lora_pending && !control.lora_requested && !control.lora_valid);
    setup(0xc0u, USB_INFO, 0u, 0u, 12u);
    finish_in();
    assert(!control.lora_pending && !control.lora_requested && !control.lora_valid);
    lora_submit(request);
    usb_control_reset(&control);
    assert(!control.lora_pending && !control.lora_requested);
    configure();
    lora_submit(request);
    usb_control_complete(&control, 1u, NULL, 0u);
    assert(control.lora_requested && control.lora_valid && control.lora_sequence == 1u);
    setup(0xc0u, USB_LORA_REPORT, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_IN);
    assert(memcmp(control.data + 4u, request + 4u, 28u) == 0);
    assert(usb_load_u32(control.data + 32u) == 0u);
    assert(usb_load_u32(control.data + 40u) == 1u);
    assert(usb_load_u32(control.data + 60u) == usb_crc32(control.data, 60u));
    finish_in();
    setup(0x40u, USB_LORA_SUBMIT, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_STALL && control.lora_requested);
    control.lora_requested = 0u;
    control.lora_running = 1u;
    setup(0x40u, USB_LORA_SUBMIT, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_STALL && control.lora_running);
    const sx1262_result_t result = {0, {0x24u, 1u}};
    usb_control_lora_result(&control, &result);
    assert(!control.lora_running);
    setup(0xc0u, USB_LORA_REPORT, 0u, 0u, 64u);
    assert(usb_load_u32(control.data + 32u) == 1u);
    assert(usb_load_u32(control.data + 36u) == 0u);
    assert(memcmp(control.data + 44u, result.samples, 16u) == 0);
    finish_in();
    lora_submit(request);
    usb_control_complete(&control, 1u, NULL, 0u);
    control.lora_requested = 0u;
    control.lora_running = 1u;
    sx1262_result_t failed = {SX1262_BUSY_ERROR, {0}};
    usb_control_lora_result(&control, &failed);
    setup(0xc0u, USB_LORA_REPORT, 0u, 0u, 64u);
    assert(usb_load_u32(control.data + 32u) == 2u);
    assert(usb_load_u32(control.data + 36u) == (uint32_t)SX1262_BUSY_ERROR);
    assert(usb_load_u32(control.data + 40u) == 2u);
    assert(usb_load_u32(control.data + 60u) == usb_crc32(control.data, 60u));
    finish_in();
    for (uint16_t length = 0u; length < 64u; ++length) {
        lora_submit(request);
        /* A new SETUP aborts the full-length pending submission. */
        setup(0x40u, USB_LORA_SUBMIT, 0u, 0u, 64u);
        usb_control_complete(&control, 0u, request, length);
        assert(control.action == USB_ACTION_STALL && !control.lora_requested);
    }
    lora_submit(request);
    usb_control_complete(&control, 1u, NULL, 0u);
    control.lora_requested = 0u;
    control.lora_running = 1u;
    setup(0u, 9u, 0u, 0u, 0u);
    usb_control_complete(&control, 1u, NULL, 0u);
    usb_control_lora_result(&control, &result);
    assert(!control.lora_running && !control.lora_requested && !control.lora_valid);
    usb_control_reset(&control);
    assert(!control.lora_valid);
    configure();
    control.lora_disabled = 1u;
    setup(0x40u, USB_LORA_SUBMIT, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_STALL && !control.lora_requested);
    setup(0xc0u, USB_APP_STATUS, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_STALL);
    control.app_status[0] = 0x50u;
    control.app_status_valid = 1u;
    setup(0xc0u, USB_APP_STATUS, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_IN && control.data[0] == 0x50u);
    finish_in();
    setup(0xc0u, USB_WIFI_STATUS, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_STALL);
    control.wifi_status[0] = 0x44u;
    control.wifi_status_valid = 1u;
    setup(0xc0u, USB_WIFI_STATUS, 0u, 0u, 63u);
    assert(control.action == USB_ACTION_STALL);
    setup(0xc0u, USB_WIFI_STATUS, 0u, 0u, 64u);
    assert(control.action == USB_ACTION_IN && control.data[0] == 0x44u);
    finish_in();
    usb_control_reset(&control);
    assert(control.lora_disabled && control.app_status_valid);
    assert(control.wifi_status_valid);
}

static uint32_t packet_after;
static void link_submit_tests(void) {
    usb_control_init(&control, build_id, chip_id);
    configure();
    uint8_t record[64], nonce[16] = {1};
    usb_report_request(record, build_id, nonce);
    usb_store_u32(record + 12, 3);
    repair_crc(record);
    setup(0x40, USB_LINK_SUBMIT, 0, 0, 64);
    assert(control.action == USB_ACTION_STALL);
    control.link_enabled = 1; control.lora_disabled = 1;
    setup(0x40, USB_LORA_SUBMIT, 0, 0, 64);
    assert(control.action == USB_ACTION_STALL);
    setup(0x40, USB_LINK_SUBMIT, 0, 0, 64);
    assert(control.action == USB_ACTION_OUT);
    usb_control_complete(&control, 0, record, 64);
    assert(control.phase == USB_PHASE_STATUS_IN && !control.lora_requested);
    usb_control_complete(&control, 1, NULL, 0);
    assert(control.lora_requested);
    setup(0x40, USB_LINK_SUBMIT, 0, 0, 64);
    assert(control.action == USB_ACTION_STALL);
    usb_control_reset(&control);
    configure();
    usb_store_u32(record + 12, 2); repair_crc(record);
    setup(0x40, USB_LINK_SUBMIT, 0, 0, 64);
    usb_control_complete(&control, 0, record, 64);
    assert(control.action == USB_ACTION_STALL && !control.lora_requested);
    usb_control_init(&control, build_id, chip_id);
    assert(!control.link_enabled);
}
static void fake_packet(void *context, uint32_t after, uint8_t *record) {
    assert(context == &packet_after);
    packet_after = after;
    for (uint32_t i = 0u; i < USB_LORA_PACKET_SIZE; ++i) record[i] = (uint8_t)(i * 3u + after);
}

static void packet_tests(void) {
    configure();
    setup(0xc0u, USB_LORA_PACKET, 0u, 0u, USB_LORA_PACKET_SIZE);
    assert(control.action == USB_ACTION_STALL);
    control.packet_source = fake_packet;
    control.packet_context = &packet_after;
    setup(0xc0u, USB_LORA_PACKET, 0u, 0u, USB_LORA_PACKET_SIZE - 1u);
    assert(control.action == USB_ACTION_STALL);
    setup(0x40u, USB_LORA_PACKET, 0u, 0u, USB_LORA_PACKET_SIZE);
    assert(control.action == USB_ACTION_STALL);
    static uint8_t result[USB_LORA_PACKET_SIZE];
    uint32_t total = 0u;
    uint8_t pid = 1u;
    setup(0xc0u, USB_LORA_PACKET, 0x5678u, 0x1234u, USB_LORA_PACKET_SIZE);
    assert(packet_after == 0x12345678u);
    while (control.phase == USB_PHASE_DATA_IN) {
        assert(control.action == USB_ACTION_IN && control.in_pid == pid && control.length <= 64u);
        assert(total + control.length <= USB_LORA_PACKET_SIZE);
        memcpy(result + total, control.data, control.length);
        total += control.length;
        usb_control_complete(&control, 1u, NULL, control.length);
        pid ^= 1u;
    }
    assert(total == USB_LORA_PACKET_SIZE && control.phase == USB_PHASE_STATUS_OUT);
    usb_control_complete(&control, 0u, NULL, 0u);
    assert(control.phase == USB_PHASE_IDLE);
    for (uint32_t i = 0u; i < USB_LORA_PACKET_SIZE; ++i) assert(result[i] == (uint8_t)(i * 3u + 0x78u));
    usb_control_reset(&control);
    setup(0xc0u, USB_LORA_PACKET, 0u, 0u, USB_LORA_PACKET_SIZE);
    assert(control.action == USB_ACTION_STALL); /* unconfigured after bus reset */
    configure();
}

static void survey_tests(void) {
    usb_control_reset(&control);
    configure();
    /* 868.1 MHz = 868100 kHz = 0x000d3f04 */
    setup(0xc0u, USB_LORA_SURVEY, 0x3f04u, 0x000du, USB_LORA_SURVEY_SIZE);
    assert(control.action == USB_ACTION_STALL); /* not enabled by the application */
    control.survey_enabled = 1u;
    setup(0xc0u, USB_LORA_SURVEY, 0x3f04u, 0x000du, USB_LORA_SURVEY_SIZE - 1u);
    assert(control.action == USB_ACTION_STALL);
    setup(0x40u, USB_LORA_SURVEY, 0x3f04u, 0x000du, USB_LORA_SURVEY_SIZE);
    assert(control.action == USB_ACTION_STALL);
    setup(0xc0u, USB_LORA_SURVEY, (uint16_t)149999u, (uint16_t)(149999u >> 16), USB_LORA_SURVEY_SIZE);
    assert(control.action == USB_ACTION_STALL && control.survey_pending_khz == 0u);
    setup(0xc0u, USB_LORA_SURVEY, (uint16_t)960001u, (uint16_t)(960001u >> 16), USB_LORA_SURVEY_SIZE);
    assert(control.action == USB_ACTION_STALL && control.survey_pending_khz == 0u);

    setup(0xc0u, USB_LORA_SURVEY, 0x3f04u, 0x000du, USB_LORA_SURVEY_SIZE);
    assert(control.action == USB_ACTION_IN && control.length == USB_LORA_SURVEY_SIZE);
    assert(control.survey_pending_khz == 868100u);
    assert(usb_load_u32(control.data) == USB_LORA_SURVEY_MAGIC && control.data[5] == USB_SURVEY_PENDING);
    assert(usb_load_u32(control.data + 8) == build_id && usb_load_u32(control.data + 12) == 868100u);
    assert(usb_load_u32(control.data + 28) == usb_crc32(control.data, 28));
    finish_in();

    /* A stale result for another frequency is ignored. */
    usb_control_survey_result(&control, 868300u, 250u, 16u, 0, -460, -450, -440);
    assert(control.survey_pending_khz == 868100u && control.survey_done_khz == 0u);
    usb_control_survey_result(&control, 868100u, 250u, 16u, 0, -460, -450, -440);
    assert(control.survey_pending_khz == 0u && control.survey_done_khz == 868100u);
    setup(0xc0u, USB_LORA_SURVEY, 0x3f04u, 0x000du, USB_LORA_SURVEY_SIZE);
    assert(control.action == USB_ACTION_IN && control.data[5] == USB_SURVEY_DONE);
    assert(control.data[16] == 250u && control.data[17] == 0u && control.data[18] == 16u);
    assert((uint16_t)(control.data[20] | control.data[21] << 8) == (uint16_t)-460);
    assert((uint16_t)(control.data[22] | control.data[23] << 8) == (uint16_t)-450);
    assert((uint16_t)(control.data[24] | control.data[25] << 8) == (uint16_t)-440);
    assert(usb_load_u32(control.data + 28) == usb_crc32(control.data, 28));
    finish_in();
    assert(control.survey_pending_khz == 0u); /* repeating does not re-queue */

    /* A new frequency invalidates the old result; failures carry the error. */
    setup(0xc0u, USB_LORA_SURVEY, (uint16_t)950000u, (uint16_t)(950000u >> 16), USB_LORA_SURVEY_SIZE);
    assert(control.data[5] == USB_SURVEY_PENDING && control.survey_done_khz == 0u);
    finish_in();
    setup(0xc0u, USB_LORA_SURVEY, 0x3f04u, 0x000du, USB_LORA_SURVEY_SIZE);
    assert(control.data[5] == USB_SURVEY_PENDING && control.survey_pending_khz == 868100u);
    finish_in();
    usb_control_survey_result(&control, 868100u, 0u, 0u, -3011, 0, 0, 0);
    setup(0xc0u, USB_LORA_SURVEY, 0x3f04u, 0x000du, USB_LORA_SURVEY_SIZE);
    assert(control.data[5] == USB_SURVEY_FAILED && (int32_t)usb_load_u32(control.data + 20) == -3011);
    for (uint32_t i = 16u; i < 20u; ++i) assert(control.data[i] == 0u);
    for (uint32_t i = 24u; i < 28u; ++i) assert(control.data[i] == 0u);
    finish_in();

    usb_control_reset(&control);
    setup(0xc0u, USB_LORA_SURVEY, 0x3f04u, 0x000du, USB_LORA_SURVEY_SIZE);
    assert(control.action == USB_ACTION_STALL); /* unconfigured after bus reset */
    configure();
}

int main(void) {
    codec_tests();
    descriptor_tests();
    windows_descriptor_tests();
    report_tests();
    recovery_tests();
    lora_tests();
    packet_tests();
    survey_tests();
    link_submit_tests();
    puts("PASS: CRC, strict records, descriptors, EP0 sequencing, reports, LoRa, packets, survey, reset and recovery");
    return 0;
}

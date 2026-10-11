#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../shared/usb_report.h"
#include "../../tools/diagnostics_logic.h"

static void checksum(uint8_t *record) {
    uint32_t crc = diag_crc32(record, 60u);
    for (unsigned i = 0u; i < 4u; ++i) record[60u + i] = (uint8_t)(crc >> (8u * i));
}

static int fixture(const char *request_path, const char *report_path, const char *mode) {
    uint8_t request[USB_REPORT_WIRE_SIZE], report[USB_REPORT_WIRE_SIZE];
    FILE *file = fopen(request_path, "rb");
    assert(file != NULL);
    assert(fread(request, 1u, sizeof(request), file) == sizeof(request));
    assert(fclose(file) == 0);
    uint32_t build = usb_report_u32(request + 8u);
    assert(usb_report_check_request(request, sizeof(request), build) == USB_RECORD_OK);
    if (strcmp(mode, "stale") == 0) request[16u] ^= 1u;
    usb_report_response(report, build, request + 16u, USB_REPORT_OK, 0xfeedc0deu);
    file = fopen(report_path, "wb");
    assert(file != NULL);
    assert(fwrite(report, 1u, sizeof(report), file) == sizeof(report));
    assert(fclose(file) == 0);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 5 && strcmp(argv[1], "--fixture") == 0) return fixture(argv[2], argv[3], argv[4]);
    const uint32_t build = 0x12345678u;
    uint8_t nonce[USB_REPORT_NONCE_SIZE];
    uint8_t request[USB_REPORT_WIRE_SIZE], report[USB_REPORT_WIRE_SIZE], bad[USB_REPORT_WIRE_SIZE];
    for (unsigned i = 0u; i < sizeof(nonce); ++i) nonce[i] = (uint8_t)(i + 1u);
    usb_report_request(request, build, nonce);
    assert(usb_report_check_request(request, sizeof(request), build) == USB_RECORD_OK);
    usb_report_response(report, build, nonce, USB_REPORT_OK, 0xfeedc0deu);
    assert(usb_report_check_response(report, sizeof(report), request, sizeof(request), build) == USB_RECORD_OK);
    assert(usb_report_u32(report + 8u) == build);
    assert(usb_report_u32(report + 48u) == USB_REPORT_RESPONSE_ADDRESS);
    for (unsigned length = 0u; length < USB_REPORT_WIRE_SIZE; ++length) {
        assert(usb_report_check_request(request, length, build) == USB_RECORD_LENGTH);
        assert(usb_report_check_response(report, length, request, sizeof(request), build) == USB_RECORD_LENGTH);
    }
    assert(usb_report_check_response(report, 65u, request, sizeof(request), build) == USB_RECORD_LENGTH);
    for (unsigned i = 0u; i < USB_REPORT_WIRE_SIZE; ++i) {
        memcpy(bad, report, sizeof(bad));
        bad[i] ^= 1u;
        assert(usb_report_check_response(bad, sizeof(bad), request, sizeof(request), build) != USB_RECORD_OK);
    }
    const unsigned fields[] = {0u, 4u, 6u, 8u, 12u, 16u, 32u, 36u, 40u, 44u, 48u, 52u, 56u};
    const usb_record_result_t failures[] = {
        USB_RECORD_MAGIC, USB_RECORD_VERSION, USB_RECORD_LENGTH, USB_RECORD_BUILD,
        USB_RECORD_TEST, USB_RECORD_NONCE, USB_RECORD_STATUS, USB_RECORD_PAYLOAD,
        USB_RECORD_PAYLOAD, USB_RECORD_PAYLOAD, USB_RECORD_PAYLOAD, USB_RECORD_PAYLOAD, USB_RECORD_PAYLOAD
    };
    for (unsigned i = 0u; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        memcpy(bad, report, sizeof(bad));
        bad[fields[i]] ^= 1u;
        checksum(bad);
        assert(usb_report_check_response(bad, sizeof(bad), request, sizeof(request), build) == failures[i]);
    }
    memset(nonce, 0, sizeof(nonce));
    usb_report_request(bad, build, nonce);
    assert(usb_report_check_request(bad, sizeof(bad), build) == USB_RECORD_NONCE);
    memcpy(bad, request, sizeof(bad));
    bad[59u] = 1u;
    checksum(bad);
    assert(usb_report_check_request(bad, sizeof(bad), build) == USB_RECORD_RESERVED);
    for (unsigned i = 0u; i < sizeof(nonce); ++i) nonce[i] = (uint8_t)(i + 2u);
    usb_report_response(bad, build, nonce, USB_REPORT_OK, 0xfeedc0deu);
    assert(usb_report_check_response(bad, sizeof(bad), request, sizeof(request), build) == USB_RECORD_NONCE);
    puts("usb_report: exact lengths, version/build/test, nonce freshness, CRC and known payload PASS");
    return 0;
}

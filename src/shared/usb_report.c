#include "usb_report.h"
#include "../tools/diagnostics_logic.h"
#include "bytes.h"

#define REQUEST_MAGIC 0x51363255u
#define RESPONSE_MAGIC 0x52363255u
#define COMMIT_TAG 0xc001cafeu

uint32_t usb_report_u32(const uint8_t *wire) {
    return badger_bytes_load_u32(wire);
}

static void store(uint8_t *wire, uint32_t value) {
    badger_bytes_store_u32(wire, value);
}

void usb_report_header(uint8_t wire[USB_REPORT_WIRE_SIZE], uint32_t magic, uint32_t build_id,
                        const uint8_t nonce[USB_REPORT_NONCE_SIZE], uint32_t test_id) {
    badger_bytes_zero(wire, USB_REPORT_WIRE_SIZE);
    store(wire, magic);
    wire[4] = 1u;
    wire[6] = USB_REPORT_WIRE_SIZE;
    store(wire + 8u, build_id);
    store(wire + 12u, test_id);
    badger_bytes_copy(wire + 16u, nonce, USB_REPORT_NONCE_SIZE);
}

static usb_record_result_t check_header(const uint8_t *wire, uint32_t length,
                                        uint32_t magic, uint32_t build_id, uint32_t test_id) {
    if (wire == 0 || length != USB_REPORT_WIRE_SIZE) return USB_RECORD_LENGTH;
    if (usb_report_u32(wire) != magic) return USB_RECORD_MAGIC;
    if (wire[4] != 1u || wire[5] != 0u) return USB_RECORD_VERSION;
    if (wire[6] != USB_REPORT_WIRE_SIZE || wire[7] != 0u) return USB_RECORD_LENGTH;
    if (usb_report_u32(wire + 60u) != diag_crc32(wire, 60u)) return USB_RECORD_CRC;
    if (usb_report_u32(wire + 8u) != build_id) return USB_RECORD_BUILD;
    if (usb_report_u32(wire + 12u) != test_id) return USB_RECORD_TEST;
    uint8_t any_nonce = 0u;
    for (uint32_t i = 0u; i < USB_REPORT_NONCE_SIZE; ++i) any_nonce |= wire[16u + i];
    return any_nonce == 0u ? USB_RECORD_NONCE : USB_RECORD_OK;
}

void usb_report_request(uint8_t wire[USB_REPORT_WIRE_SIZE], uint32_t build_id,
                         const uint8_t nonce[USB_REPORT_NONCE_SIZE]) {
    usb_report_request_for_test(wire, build_id, nonce, USB_REPORT_TEST_ID);
}

void usb_report_request_for_test(uint8_t wire[USB_REPORT_WIRE_SIZE], uint32_t build_id,
                                  const uint8_t nonce[USB_REPORT_NONCE_SIZE], uint32_t test_id) {
    usb_report_header(wire, REQUEST_MAGIC, build_id, nonce, test_id);
    store(wire + 60u, diag_crc32(wire, 60u));
}

usb_record_result_t usb_report_check_request(const uint8_t *wire, uint32_t length,
                                              uint32_t build_id) {
    return usb_report_check_request_for_test(wire, length, build_id, USB_REPORT_TEST_ID);
}

usb_record_result_t usb_report_check_request_for_test(const uint8_t *wire, uint32_t length,
                                                       uint32_t build_id, uint32_t test_id) {
    usb_record_result_t result = check_header(wire, length, REQUEST_MAGIC, build_id, test_id);
    if (result != USB_RECORD_OK) return result;
    for (uint32_t i = 32u; i < 60u; ++i) {
        if (wire[i] != 0u) return USB_RECORD_RESERVED;
    }
    return USB_RECORD_OK;
}

void usb_report_response(uint8_t wire[USB_REPORT_WIRE_SIZE], uint32_t build_id,
                          const uint8_t nonce[USB_REPORT_NONCE_SIZE],
                          uint32_t status, uint32_t detail) {
    usb_report_header(wire, RESPONSE_MAGIC, build_id, nonce, USB_REPORT_TEST_ID);
    store(wire + 32u, status);
    store(wire + 36u, detail);
    store(wire + 40u, 0x01234567u);
    store(wire + 44u, 0x89abcdefu);
    store(wire + 48u, USB_REPORT_RESPONSE_ADDRESS);
    store(wire + 52u, USB_REPORT_WIRE_SIZE);
    store(wire + 56u, COMMIT_TAG);
    store(wire + 60u, diag_crc32(wire, 60u));
}

usb_record_result_t usb_report_check_response(const uint8_t *wire, uint32_t length,
                                               const uint8_t *request, uint32_t request_length,
                                               uint32_t build_id) {
    usb_record_result_t result = usb_report_check_request(request, request_length, build_id);
    if (result != USB_RECORD_OK) return result;
    result = check_header(wire, length, RESPONSE_MAGIC, build_id, USB_REPORT_TEST_ID);
    if (result != USB_RECORD_OK) return result;
    for (uint32_t i = 16u; i < 32u; ++i) {
        if (wire[i] != request[i]) return USB_RECORD_NONCE;
    }
    if (usb_report_u32(wire + 32u) != USB_REPORT_OK) return USB_RECORD_STATUS;
    if (usb_report_u32(wire + 36u) != 0xfeedc0deu ||
        usb_report_u32(wire + 40u) != 0x01234567u ||
        usb_report_u32(wire + 44u) != 0x89abcdefu ||
        usb_report_u32(wire + 48u) != USB_REPORT_RESPONSE_ADDRESS ||
        usb_report_u32(wire + 52u) != USB_REPORT_WIRE_SIZE ||
        usb_report_u32(wire + 56u) != COMMIT_TAG) return USB_RECORD_PAYLOAD;
    return USB_RECORD_OK;
}

const char *usb_report_result_name(usb_record_result_t result) {
    switch (result) {
        case USB_RECORD_OK: return "VERIFIED";
        case USB_RECORD_LENGTH: return "INVALID LENGTH";
        case USB_RECORD_MAGIC: return "INVALID MAGIC";
        case USB_RECORD_VERSION: return "INVALID VERSION";
        case USB_RECORD_CRC: return "CHECKSUM MISMATCH";
        case USB_RECORD_BUILD: return "WRONG BUILD";
        case USB_RECORD_TEST: return "WRONG TEST";
        case USB_RECORD_NONCE: return "STALE OR INVALID RUN";
        case USB_RECORD_RESERVED: return "INVALID RESERVED FIELDS";
        case USB_RECORD_STATUS: return "DEVICE REPORTED FAILURE";
        case USB_RECORD_PAYLOAD: return "KNOWN PAYLOAD MISMATCH";
    }
    return "UNKNOWN RECORD ERROR";
}

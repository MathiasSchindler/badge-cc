#include "report.h"

uint32_t usb_load_u32(const uint8_t *data) {
    return (uint32_t)data[0] | (uint32_t)data[1] << 8u |
           (uint32_t)data[2] << 16u | (uint32_t)data[3] << 24u;
}

void usb_store_u32(uint8_t *data, uint32_t value) {
    for (uint32_t i = 0u; i < 4u; ++i) data[i] = (uint8_t)(value >> (i * 8u));
}

uint32_t usb_crc32(const uint8_t *data, uint32_t length) {
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0u; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((0u - (crc & 1u)) & 0xedb88320u);
    }
    return crc ^ 0xffffffffu;
}

static void header(uint8_t wire[USB_REPORT_SIZE], uint32_t magic,
                   uint32_t build_id, const uint8_t nonce[USB_NONCE_SIZE]) {
    for (uint32_t i = 0u; i < USB_REPORT_SIZE; ++i) wire[i] = 0u;
    usb_store_u32(wire, magic);
    wire[4] = 1u;
    wire[6] = USB_REPORT_SIZE;
    usb_store_u32(wire + 8u, build_id);
    usb_store_u32(wire + 12u, USB_REPORT_TEST_ID);
    for (uint32_t i = 0u; i < USB_NONCE_SIZE; ++i) wire[16u + i] = nonce[i];
}

void usb_report_request(uint8_t wire[USB_REPORT_SIZE], uint32_t build_id,
                        const uint8_t nonce[USB_NONCE_SIZE]) {
    header(wire, USB_REQUEST_MAGIC, build_id, nonce);
    usb_store_u32(wire + 60u, usb_crc32(wire, 60u));
}

usb_record_result_t usb_report_check_request(const uint8_t *wire, uint32_t length,
                                             uint32_t build_id) {
    if (wire == 0 || length != USB_REPORT_SIZE) return USB_RECORD_LENGTH;
    if (usb_load_u32(wire) != USB_REQUEST_MAGIC) return USB_RECORD_MAGIC;
    if (wire[4] != 1u || wire[5] != 0u) return USB_RECORD_VERSION;
    if (wire[6] != USB_REPORT_SIZE || wire[7] != 0u) return USB_RECORD_LENGTH;
    if (usb_load_u32(wire + 60u) != usb_crc32(wire, 60u)) return USB_RECORD_CRC;
    if (usb_load_u32(wire + 8u) != build_id) return USB_RECORD_BUILD;
    if (usb_load_u32(wire + 12u) != USB_REPORT_TEST_ID) return USB_RECORD_TEST;
    uint8_t any_nonce = 0u;
    for (uint32_t i = 0u; i < USB_NONCE_SIZE; ++i) any_nonce |= wire[16u + i];
    if (any_nonce == 0u) return USB_RECORD_NONCE;
    for (uint32_t i = 32u; i < 60u; ++i)
        if (wire[i] != 0u) return USB_RECORD_RESERVED;
    return USB_RECORD_OK;
}

void usb_report_response(uint8_t wire[USB_REPORT_SIZE], uint32_t build_id,
                         const uint8_t nonce[USB_NONCE_SIZE],
                         const uint32_t chip_id[2], uint32_t sequence) {
    header(wire, USB_RESPONSE_MAGIC, build_id, nonce);
    usb_store_u32(wire + 32u, 1u);
    usb_store_u32(wire + 36u, chip_id[0]);
    usb_store_u32(wire + 40u, chip_id[1]);
    usb_store_u32(wire + 44u, sequence);
    usb_store_u32(wire + 48u, 0x01234567u);
    usb_store_u32(wire + 52u, 0x89abcdefu);
    usb_store_u32(wire + 56u, USB_REPORT_COMMIT);
    usb_store_u32(wire + 60u, usb_crc32(wire, 60u));
}

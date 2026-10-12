#include "nametag_measure.h"
#include "bytes.h"
#include "../tools/diagnostics_logic.h"

int nametag_m2_page(uint8_t out[64], const uint32_t words[NAMETAG_M2_WORDS],
    uint32_t build, const uint8_t nonce[16], uint32_t page) {
    if (out == 0 || words == 0 || nonce == 0 || page >= NAMETAG_M2_PAGES) return 0;
    usb_report_header(out, 0x4d363255u, build, nonce, NAMETAG_M2_TEST);
    uint32_t offset = page * NAMETAG_M2_CHUNK;
    uint32_t length = NAMETAG_M2_WORDS * 4u - offset;
    if (length > NAMETAG_M2_CHUNK) length = NAMETAG_M2_CHUNK;
    out[32] = (uint8_t)page;
    out[33] = NAMETAG_M2_PAGES;
    out[34] = (uint8_t)length;
    for (uint32_t i = 0u; i < length; ++i) {
        uint32_t index = offset + i;
        out[36u + i] = (uint8_t)(words[index / 4u] >> ((index % 4u) * 8u));
    }
    badger_bytes_store_u32(out + 60u, diag_crc32(out, 60u));
    return 1;
}

usb_record_result_t nametag_m2_page_decode(uint32_t words[NAMETAG_M2_WORDS],
    const uint8_t *wire, uint32_t length, uint32_t build, const uint8_t nonce[16],
    uint32_t page) {
    if (words == 0 || wire == 0 || nonce == 0 || length != 64u ||
        page >= NAMETAG_M2_PAGES) return USB_RECORD_LENGTH;
    if (usb_report_u32(wire) != 0x4d363255u) return USB_RECORD_MAGIC;
    if (wire[4] != 1u || wire[5] != 0u) return USB_RECORD_VERSION;
    if (wire[6] != 64u || wire[7] != 0u) return USB_RECORD_LENGTH;
    if (usb_report_u32(wire + 60u) != diag_crc32(wire, 60u)) return USB_RECORD_CRC;
    if (usb_report_u32(wire + 8u) != build) return USB_RECORD_BUILD;
    if (usb_report_u32(wire + 12u) != NAMETAG_M2_TEST) return USB_RECORD_TEST;
    uint8_t any = 0u;
    for (uint32_t i = 0u; i < 16u; ++i) {
        if (wire[16u + i] != nonce[i]) return USB_RECORD_NONCE;
        any |= nonce[i];
    }
    if (!any) return USB_RECORD_NONCE;
    uint32_t offset = page * NAMETAG_M2_CHUNK;
    uint32_t bytes = NAMETAG_M2_WORDS * 4u - offset;
    if (bytes > NAMETAG_M2_CHUNK) bytes = NAMETAG_M2_CHUNK;
    if (wire[32] != page || wire[33] != NAMETAG_M2_PAGES || wire[34] != bytes ||
        wire[35] != 0u) return USB_RECORD_PAYLOAD;
    for (uint32_t i = bytes; i < NAMETAG_M2_CHUNK; ++i)
        if (wire[36u + i] != 0u) return USB_RECORD_RESERVED;
    for (uint32_t i = 0u; i < bytes / 4u; ++i)
        words[offset / 4u + i] = usb_report_u32(wire + 36u + i * 4u);
    return USB_RECORD_OK;
}

int nametag_m2_statistics(const uint32_t *samples, uint32_t count, uint32_t out[4]) {
    if (samples == 0 || out == 0 || count == 0u || count > NAMETAG_M2_SAMPLES) return 0;
    uint32_t sorted[NAMETAG_M2_SAMPLES];
    for (uint32_t i = 0u; i < count; ++i) {
        uint32_t j = i;
        while (j != 0u && sorted[j - 1u] > samples[i]) {
            sorted[j] = sorted[j - 1u];
            --j;
        }
        sorted[j] = samples[i];
    }
    out[0] = sorted[0];
    out[1] = sorted[(count * 50u + 99u) / 100u - 1u];
    out[2] = sorted[(count * 99u + 99u) / 100u - 1u];
    out[3] = sorted[count - 1u];
    return 1;
}

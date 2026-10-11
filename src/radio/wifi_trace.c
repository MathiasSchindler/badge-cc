#include "wifi_trace.h"
#include "../tools/diagnostics_logic.h"

void wifi_trace_page(uint8_t out[64], const uint8_t trace[WIFI_TRACE_SIZE],
                      const uint8_t report[64], uint8_t page) {
    for (uint32_t i = 0u; i < 64u; ++i) out[i] = 0u;
    if (page >= WIFI_TRACE_PAGES) return;
    for (uint32_t i = 0u; i < 32u; ++i) out[i] = report[i];
    out[3] = 'T';
    uint32_t offset = page * WIFI_TRACE_CHUNK;
    uint32_t length = WIFI_TRACE_SIZE - offset;
    if (length > WIFI_TRACE_CHUNK) length = WIFI_TRACE_CHUNK;
    out[32] = page;
    out[33] = WIFI_TRACE_PAGES;
    out[34] = (uint8_t)length;
    for (uint32_t i = 0u; i < length; ++i) out[36u + i] = trace[offset + i];
    uint32_t crc = diag_crc32(out, 60u);
    for (uint32_t i = 0u; i < 4u; ++i) out[60u + i] = (uint8_t)(crc >> (8u * i));
}

usb_record_result_t wifi_trace_page_decode(uint8_t trace[WIFI_TRACE_SIZE],
                                            const uint8_t *page, uint32_t length,
                                            const uint8_t report[64], uint8_t expected_page) {
    if (trace == 0 || page == 0 || report == 0 || length != 64u ||
        expected_page >= WIFI_TRACE_PAGES) return USB_RECORD_LENGTH;
    if (page[0] != 'U' || page[1] != '2' || page[2] != '6' || page[3] != 'T') return USB_RECORD_MAGIC;
    if (usb_report_u32(page + 60u) != diag_crc32(page, 60u)) return USB_RECORD_CRC;
    for (uint32_t i = 4u; i < 32u; ++i) {
        if (page[i] != report[i]) return USB_RECORD_NONCE;
    }
    uint32_t offset = expected_page * WIFI_TRACE_CHUNK;
    uint32_t bytes = WIFI_TRACE_SIZE - offset;
    if (bytes > WIFI_TRACE_CHUNK) bytes = WIFI_TRACE_CHUNK;
    if (page[32] != expected_page || page[33] != WIFI_TRACE_PAGES ||
        page[34] != bytes || page[35] != 0u) return USB_RECORD_PAYLOAD;
    for (uint32_t i = bytes; i < WIFI_TRACE_CHUNK; ++i) if (page[36u+i]) return USB_RECORD_RESERVED;
    for (uint32_t i = 0u; i < bytes; ++i) trace[offset+i] = page[36u+i];
    return USB_RECORD_OK;
}

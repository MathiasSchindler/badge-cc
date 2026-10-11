#include "wifi_usb_report.h"
#include "../tools/diagnostics_logic.h"

static void store(uint8_t *wire, uint32_t value) {
    for (uint32_t i = 0; i < 4u; ++i) wire[i] = (uint8_t)(value >> (i * 8u));
}

void wifi_usb_report_encode(uint8_t wire[64], uint32_t build_id,
                             const uint8_t nonce[16], const wifi_probe_report_t *report) {
    for (uint32_t i = 0; i < 64u; ++i) wire[i] = 0u;
    store(wire, 0x57363255u);
    wire[4] = 1u;
    wire[6] = 64u;
    store(wire + 8u, build_id);
    store(wire + 12u, WIFI_USB_TEST_ID);
    for (uint32_t i = 0; i < 16u; ++i) wire[16u + i] = nonce[i];
    store(wire + 32u, (uint32_t)report->stage | ((uint32_t)report->result << 4u) |
          ((uint32_t)report->reset_attempts << 8u) | ((uint32_t)report->passed_reads << 12u) |
          ((uint32_t)report->powered << 17u) | ((uint32_t)report->clock_csr << 18u));
    store(wire + 36u, report->reset_pattern);
    store(wire + 40u, report->normal_pattern);
    store(wire + 44u, report->bus_control);
    store(wire + 48u, report->chip_id);
    store(wire + 52u, report->last_value);
    store(wire + 56u, report->elapsed_us);
    store(wire + 60u, diag_crc32(wire, 60u));
}

usb_record_result_t wifi_usb_report_decode(const uint8_t *wire, uint32_t length,
                                            const uint8_t request[64], uint32_t build_id,
                                            wifi_probe_report_t *report) {
    usb_record_result_t result = usb_report_check_request_for_test(request, 64u, build_id, WIFI_USB_TEST_ID);
    if (result != USB_RECORD_OK) return result;
    if (wire == 0 || report == 0 || length != 64u || wire[6] != 64u || wire[7] != 0u) return USB_RECORD_LENGTH;
    if (usb_report_u32(wire) != 0x57363255u) return USB_RECORD_MAGIC;
    if (wire[4] != 1u || wire[5] != 0u) return USB_RECORD_VERSION;
    if (usb_report_u32(wire + 60u) != diag_crc32(wire, 60u)) return USB_RECORD_CRC;
    if (usb_report_u32(wire + 8u) != build_id) return USB_RECORD_BUILD;
    if (usb_report_u32(wire + 12u) != WIFI_USB_TEST_ID) return USB_RECORD_TEST;
    for (uint32_t i = 16u; i < 32u; ++i) if (wire[i] != request[i]) return USB_RECORD_NONCE;
    uint32_t state = usb_report_u32(wire + 32u);
    uint32_t stage = state & 15u, error = (state >> 4u) & 15u;
    uint32_t attempts = (state >> 8u) & 15u, reads = (state >> 12u) & 31u;
    if (stage < WIFI_PROBE_RESET || stage > WIFI_PROBE_FINISHED ||
        error >= WIFI_BUS_RADIO_OFF || attempts > 10u || reads > 16u ||
        (state >> 26u) != 0u) return USB_RECORD_PAYLOAD;
    if (error == WIFI_BUS_OK &&
        (stage != WIFI_PROBE_FINISHED || reads != 16u ||
         attempts == 0u ||
         usb_report_u32(wire + 36u) != WIFI_GSPI_TEST_PATTERN ||
         usb_report_u32(wire + 40u) != WIFI_GSPI_TEST_PATTERN ||
         (usb_report_u32(wire + 44u) & WIFI_GSPI_CONFIG_MASK) !=
             (WIFI_GSPI_BUS_CONFIG & WIFI_GSPI_CONFIG_MASK) ||
         usb_report_u32(wire + 48u) == 0u || usb_report_u32(wire + 48u) == 0xffffffffu ||
         ((state >> 18u) & 0x40u) == 0u)) return USB_RECORD_PAYLOAD;
    if (error != WIFI_BUS_OK && stage == WIFI_PROBE_FINISHED) return USB_RECORD_PAYLOAD;
    report->stage = (wifi_probe_stage_t)stage;
    report->result = (wifi_bus_result_t)error;
    report->reset_attempts = (uint8_t)attempts;
    report->passed_reads = (uint16_t)reads;
    report->reset_pattern = usb_report_u32(wire + 36u);
    report->normal_pattern = usb_report_u32(wire + 40u);
    report->bus_control = usb_report_u32(wire + 44u);
    report->chip_id = usb_report_u32(wire + 48u);
    report->last_value = usb_report_u32(wire + 52u);
    report->elapsed_us = usb_report_u32(wire + 56u);
    report->clock_csr = (uint8_t)(state >> 18u);
    report->powered = (uint8_t)((state >> 17u) & 1u);
    return USB_RECORD_OK;
}

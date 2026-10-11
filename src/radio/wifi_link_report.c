#include "wifi_link_report.h"
#include "../tools/diagnostics_logic.h"
#include "../shared/bytes.h"

static void store(uint8_t *bytes, uint32_t value) {
    badger_bytes_store_u32(bytes, value);
}

void wifi_link_snapshot_begin(wifi_link_snapshot_t *snapshot, const wifi_link_report_t *report) {
    badger_bytes_copy((uint8_t *)&snapshot->committed, (const uint8_t *)report, sizeof(snapshot->committed));
    snapshot->in_progress = 1u;
}

void wifi_link_snapshot_end(wifi_link_snapshot_t *snapshot) {
    snapshot->in_progress = 0u;
}

const wifi_link_report_t *wifi_link_snapshot_view(const wifi_link_snapshot_t *snapshot,
                                                  const wifi_link_report_t *current) {
    return snapshot->in_progress ? &snapshot->committed : current;
}

void wifi_link_report_encode(uint8_t bytes[64], uint32_t build,
                              const uint8_t nonce[16], const wifi_link_report_t *report) {
    usb_report_header(bytes, 0x4e363255u, build, nonce, WIFI_LINK_TEST_ID);
    bytes[32] = report->role;
    bytes[33] = (uint8_t)report->stage;
    bytes[34] = (uint8_t)report->error;
    bytes[35] = report->authorized;
    store(bytes+36u, report->last_command);
    store(bytes+40u, report->controller_status);
    store(bytes+44u, report->peer_count);
    for (uint32_t i = 0u; i < 6u; ++i) {
        bytes[48u+i] = report->mac[i];
        bytes[54u+i] = report->peer[i];
    }
    store(bytes+60u, diag_crc32(bytes,60u));
}

usb_record_result_t wifi_link_report_decode(const uint8_t bytes[64], uint32_t build,
                                            const uint8_t nonce[16], uint8_t role,
                                            wifi_link_report_t *report) {
    if (bytes == 0 || nonce == 0 || report == 0 || role > WIFI_LINK_STATION) return USB_RECORD_LENGTH;
    if (usb_report_u32(bytes) != 0x4e363255u) return USB_RECORD_MAGIC;
    if (bytes[4] != 1u || bytes[5] != 0u) return USB_RECORD_VERSION;
    if (bytes[6] != 64u || bytes[7] != 0u) return USB_RECORD_LENGTH;
    if (usb_report_u32(bytes+60u) != diag_crc32(bytes,60u)) return USB_RECORD_CRC;
    if (usb_report_u32(bytes+8u) != build) return USB_RECORD_BUILD;
    if (usb_report_u32(bytes+12u) != WIFI_LINK_TEST_ID) return USB_RECORD_TEST;
    for (uint32_t i = 0u; i < 16u; ++i) if (bytes[16u+i] != nonce[i]) return USB_RECORD_NONCE;
    if (bytes[32] != role || bytes[33] > WIFI_LINK_FAILED ||
        bytes[34] > WIFI_LINK_BAD_CONFIG || bytes[35] > 1u ||
        (bytes[34] != 0u && bytes[33] != WIFI_LINK_FAILED)) return USB_RECORD_PAYLOAD;
    report->role = bytes[32];
    report->stage = (wifi_link_stage_t)bytes[33];
    report->error = (wifi_link_error_t)bytes[34];
    report->authorized = bytes[35];
    report->last_command = usb_report_u32(bytes+36u);
    report->controller_status = usb_report_u32(bytes+40u);
    report->peer_count = usb_report_u32(bytes+44u);
    uint8_t local = 0u, peer = 0u;
    for (uint32_t i = 0u; i < 6u; ++i) {
        report->mac[i] = bytes[48u+i]; local |= report->mac[i];
        report->peer[i] = bytes[54u+i]; peer |= report->peer[i];
    }
    if (report->stage == WIFI_LINK_CONNECTED &&
        (report->error != WIFI_LINK_OK || report->peer_count == 0u || local == 0u || peer == 0u ||
         (report->mac[0] & 1u) || (report->peer[0] & 1u))) return USB_RECORD_PAYLOAD;
    return USB_RECORD_OK;
}

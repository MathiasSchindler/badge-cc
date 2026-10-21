#include "picoctl_protocol.h"
#include <string.h>

static int digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int picoctl_hex(const char *text, uint8_t *output, uint32_t bytes) {
    if (strlen(text) != (size_t)bytes * 2u) return 0;
    for (uint32_t i = 0; i < bytes; ++i) {
        int high = digit(text[2u * i]), low = digit(text[2u * i + 1u]);
        if (high < 0 || low < 0) return 0;
        output[i] = (uint8_t)(high * 16 + low);
    }
    return 1;
}

int picoctl_serial(const char *text, char output[17]) {
    uint8_t value[8];
    static const char hex[] = "0123456789ABCDEF";
    if (!picoctl_hex(text, value, 8)) return 0;
    for (uint32_t i = 0; i < 8; ++i) {
        output[2u * i] = hex[value[i] >> 4u];
        output[2u * i + 1u] = hex[value[i] & 15u];
    }
    output[16] = 0;
    return 1;
}

int picoctl_identity(const uint8_t record[64], uint32_t build,
                     const uint8_t nonce[16], const char *serial) {
    uint8_t expected[64], id[8];
    if (!picoctl_hex(serial, id, 8)) return 0;
    uint32_t high = (uint32_t)id[0] << 24u | (uint32_t)id[1] << 16u |
                    (uint32_t)id[2] << 8u | id[3];
    uint32_t low = (uint32_t)id[4] << 24u | (uint32_t)id[5] << 16u |
                   (uint32_t)id[6] << 8u | id[7];
    const uint32_t chip[2] = {low, high};
    usb_report_response(expected, build, nonce, chip, usb_load_u32(record + 44));
    return memcmp(record, expected, 64) == 0;
}

int picoctl_status(const uint8_t record[64], uint32_t build, picoctl_status_t *s) {
    if (usb_load_u32(record) != 0x53573250u || record[4] != 1 || record[5] != 0 ||
        record[6] != 64 || record[7] != 0 || usb_load_u32(record + 8) != build ||
        usb_load_u32(record + 60) != usb_crc32(record, 60)) return 0;
    s->wifi_stage = usb_load_u32(record + 12);
    s->wifi_error = (int32_t)usb_load_u32(record + 16);
    s->dhcp_state = usb_load_u32(record + 20);
    s->network_error = usb_load_u32(record + 24);
    s->ip = usb_load_u32(record + 28);
    s->lora_error = (int32_t)usb_load_u32(record + 32);
    s->received = usb_load_u32(record + 36);
    s->dropped = usb_load_u32(record + 40);
    s->transmitted = usb_load_u32(record + 44);
    s->uptime_ms = usb_load_u32(record + 48);
    s->connected = usb_load_u32(record + 52);
    s->controller_status = usb_load_u32(record + 56);
    return s->wifi_stage <= 8 && s->dhcp_state <= 5 && s->connected <= 1;
}

int picoctl_network_ready(const picoctl_status_t *s) {
    return s->connected == 1 && s->wifi_stage == 7 && s->wifi_error == 0 &&
           s->network_error == 0 && (s->dhcp_state == 3 || s->dhcp_state == 4) &&
           s->ip != 0 && (s->ip >> 24u) != 0 && (s->ip >> 24u) < 224 &&
           (s->ip >> 24u) != 127;
}

int picoctl_wifi_diagnostic(const uint8_t record[64], uint32_t build) {
    return usb_load_u32(record) == 0x44573250u && record[4] == 1u && record[5] == 0u &&
           record[6] == 64u && record[7] == 0u && usb_load_u32(record + 8) == build &&
           usb_load_u32(record + 12) <= 14u && usb_load_u32(record + 36) <= 8u &&
           usb_load_u32(record + 40) <= 7u &&
           usb_load_u32(record + 60) == usb_crc32(record, 60);
}

int picoctl_packet(const uint8_t record[PICOCTL_PACKET_SIZE], uint32_t build,
                   picoctl_packet_t *p) {
    if (usb_load_u32(record) != 0x50573250u || record[4] != 1u || record[5] != 0u ||
        record[6] != (PICOCTL_PACKET_SIZE & 255u) || record[7] != (PICOCTL_PACKET_SIZE >> 8) ||
        usb_load_u32(record + 8) != build || usb_load_u32(record + 12) > 1u ||
        usb_load_u32(record + 324) != usb_crc32(record, 324)) return 0;
    p->present = usb_load_u32(record + 12);
    p->newest = usb_load_u32(record + 16);
    p->oldest = usb_load_u32(record + 20);
    p->sequence = usb_load_u32(record + 24);
    p->timestamp_ms = usb_load_u32(record + 28);
    p->frequency_hz = usb_load_u32(record + 32);
    p->bandwidth_khz = (uint32_t)record[36] | (uint32_t)record[37] << 8;
    p->sf = record[38];
    p->profile = record[39];
    p->rssi_qdb = (int32_t)usb_load_u32(record + 40);
    p->snr_qdb = (int32_t)usb_load_u32(record + 44);
    p->signal_rssi_qdb = (int32_t)usb_load_u32(record + 48);
    p->packets = usb_load_u32(record + 52);
    p->crc_errors = usb_load_u32(record + 56);
    p->header_errors = usb_load_u32(record + 60);
    p->length = record[64];
    p->flags = record[65];
    if (!p->present && (p->length || p->sequence)) return 0;
    if (p->present && (!p->sequence || p->profile > 1u)) return 0;
    for (uint32_t i = 0; i < 255u; ++i) p->payload[i] = record[68 + i];
    return 1;
}

int picoctl_uf2_block(const uint8_t b[512], uint32_t index, uint32_t count) {
    return count != 0 && count <= 16384 && index < count &&
           usb_load_u32(b) == 0x0a324655u && usb_load_u32(b + 4) == 0x9e5d5157u &&
           usb_load_u32(b + 8) == 0x2000u &&
           usb_load_u32(b + 12) == 0x10000000u + index * 256u &&
           usb_load_u32(b + 16) == 256 && usb_load_u32(b + 20) == index &&
           usb_load_u32(b + 24) == count && usb_load_u32(b + 28) == 0xe48bff59u &&
           usb_load_u32(b + 508) == 0x0ab16f30u;
}

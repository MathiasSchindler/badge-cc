#include "keypad_host.h"
#include "../../src/shared/bytes.h"
#include "../../src/tools/diagnostics_logic.h"

static uint16_t u16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8u); }

host_result_t keypad_host_configuration(const uint8_t *b, uint16_t length,
                                        keypad_endpoints_t *ep) {
    if (!b || !ep || length < 9u || b[0] != 9u || b[1] != 2u ||
        u16(b + 2u) != length || b[4] != 1u || !b[5]) return HOST_DESCRIPTOR;
    keypad_endpoints_t candidate = {0};
    candidate.configuration = b[5];
    uint8_t interface_seen = 0u, hid_seen = 0u, endpoint_count = 0u;
    for (uint32_t at = 9u; at < length;) {
        if (at + 2u > length || b[at] < 2u || at + b[at] > length) return HOST_DESCRIPTOR;
        const uint8_t *d = b + at;
        if (d[1] == 4u) {
            if (interface_seen || d[0] != 9u || d[3] || d[4] != 2u ||
                d[5] != 3u || d[6] || d[7]) return HOST_DESCRIPTOR;
            candidate.interface = d[2];
            interface_seen = 1u;
        } else if (d[1] == 0x21u) {
            if (!interface_seen || hid_seen || d[0] != 9u || d[5] != 1u || d[6] != 0x22u)
                return HOST_DESCRIPTOR;
            candidate.report_size = u16(d + 7u);
            hid_seen = 1u;
        } else if (d[1] == 5u) {
            if (!interface_seen || d[0] != 7u || d[3] != 3u || !(d[2] & 15u) ||
                (d[2] & 0x70u) || !d[6]) return HOST_DESCRIPTOR;
            uint16_t size = u16(d + 4u);
            if (!size || size > 64u) return HOST_DESCRIPTOR;
            if (d[2] & 0x80u) {
                if (candidate.input || size != 32u) return HOST_DESCRIPTOR;
                candidate.input = d[2];
                candidate.input_size = (uint8_t)size;
                candidate.input_interval = d[6];
            } else {
                if (candidate.output || size < 20u) return HOST_DESCRIPTOR;
                candidate.output = d[2];
                candidate.output_size = (uint8_t)size;
                candidate.output_interval = d[6];
            }
            ++endpoint_count;
        } else return HOST_DESCRIPTOR;
        at += d[0];
    }
    if (!interface_seen || !hid_seen || endpoint_count != 2u ||
        !candidate.input || !candidate.output || candidate.report_size != 140u) return HOST_DESCRIPTOR;
    *ep = candidate;
    return HOST_OK;
}

int keypad_host_known_hid(const uint8_t *bytes, uint16_t length) {
    /* Descriptor bytes observed on this keypad, not an external HID library. */
    static const uint8_t known[] = {
        0x06,0x43,0xff,0x0a,0x02,0x1a,0xa1,0x01,0x85,0x11,0x95,0x13,0x75,0x08,
        0x15,0x00,0x26,0xff,0x00,0x09,0x02,0x81,0x00,0x09,0x02,0x91,0x00,0xc0,
        0x06,0x43,0xff,0x0a,0x08,0x1a,0xa1,0x01,0x85,0x13,0x96,0x1f,0x00,0x75,
        0x08,0x15,0x00,0x26,0xff,0x00,0x09,0x08,0x82,0x02,0x01,0x09,0x08,0x92,
        0x02,0x01,0xc0,0x06,0x43,0xff,0x0a,0x10,0x1a,0xa1,0x01,0x85,0x14,0x96,
        0xfe,0x0f,0x75,0x08,0x15,0x00,0x26,0xff,0x00,0x09,0x10,0x92,0x02,0x01,
        0xc0,0x05,0x01,0x09,0x80,0xa1,0x01,0x85,0x04,0x95,0x01,0x75,0x02,0x15,
        0x01,0x25,0x03,0x09,0x82,0x09,0x81,0x09,0x83,0x81,0x00,0x75,0x06,0x81,
        0x03,0xc0,0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x05,0x95,0x01,0x75,0x01,
        0x15,0x00,0x25,0x01,0x0a,0x24,0x02,0x81,0x02,0x95,0x07,0x81,0x03,0xc0
    };
    return bytes && length == sizeof(known) && badger_bytes_equal(bytes, known, length);
}

static host_result_t control(keypad_host_t *k, uint8_t type, uint8_t request,
                              uint16_t value, uint16_t index, uint8_t *data,
                              uint16_t size, uint16_t *got) {
    uint8_t setup[] = {type, request, (uint8_t)value, (uint8_t)(value >> 8u),
                       (uint8_t)index, (uint8_t)(index >> 8u), (uint8_t)size, (uint8_t)(size >> 8u)};
    return usb_host_control(k->usb, setup, data, size, got);
}

static void progress(keypad_host_t *k, const char *stage) {
    if (k->progress) k->progress(k->context, stage);
}

host_result_t keypad_host_enumerate(keypad_host_t *k) {
    uint8_t bytes[512];
    uint16_t got;
    k->ready = 0u;
    if (k->changed) return HOST_HID_REPLY;
    k->feature = k->software_id = k->hid_error = 0u;
    progress(k, "BUS RESET");
    host_result_t result = usb_host_reset(k->usb);
    if (result != HOST_OK) return result;
    progress(k, "READ DEVICE HEADER");
    result = control(k, 0x80u, 6u, 0x0100u, 0u, bytes, 8u, &got);
    if (result != HOST_OK) return result;
    if (got != 8u || bytes[0] != 18u || bytes[1] != 1u ||
        (bytes[7] != 8u && bytes[7] != 16u && bytes[7] != 32u && bytes[7] != 64u))
        return HOST_DESCRIPTOR;
    k->usb->ep0_size = bytes[7];
    result = control(k, 0x80u, 6u, 0x0100u, 0u, bytes, 18u, &got);
    if (result != HOST_OK) return result;
    if (got != 18u || bytes[0] != 18u || bytes[1] != 1u ||
        bytes[7] != k->usb->ep0_size || bytes[17] != 1u) return HOST_DESCRIPTOR;
    if (bytes[4] == 9u) return HOST_HUB;
    if (u16(bytes + 8u) != 0x046du || u16(bytes + 10u) != 0xc354u) return HOST_WRONG_DEVICE;
    progress(k, "SET ADDRESS 1");
    result = control(k, 0u, 5u, 1u, 0u, 0, 0u, &got);
    if (result != HOST_OK) return result;
    k->usb->address = 1u;
    result = usb_host_wait(k->usb, 2000u);
    if (result != HOST_OK) return result;
    progress(k, "READ FULL-SPEED CONFIG");
    result = control(k, 0x80u, 6u, 0x0200u, 0u, bytes, 9u, &got);
    if (result != HOST_OK) return result;
    if (got != 9u || bytes[0] != 9u || bytes[1] != 2u) return HOST_DESCRIPTOR;
    uint16_t size = u16(bytes + 2u);
    if (size < 9u || size > sizeof(bytes)) return HOST_DESCRIPTOR;
    result = control(k, 0x80u, 6u, 0x0200u, 0u, bytes, size, &got);
    if (result != HOST_OK) return result;
    result = keypad_host_configuration(bytes, got, &k->endpoints);
    if (result != HOST_OK) return result;
    progress(k, "SET CONFIGURATION");
    result = control(k, 0u, 9u, k->endpoints.configuration, 0u, 0, 0u, &got);
    if (result != HOST_OK) return result;
    k->input_pid = k->output_pid = 0u;
    progress(k, "VERIFY HID DESCRIPTOR");
    result = control(k, 0x81u, 6u, 0x2200u, k->endpoints.interface,
                     bytes, k->endpoints.report_size, &got);
    if (result != HOST_OK) return result;
    if (!keypad_host_known_hid(bytes, got)) return HOST_DESCRIPTOR;
    k->report_crc = diag_crc32(bytes, got);
    k->ready = 1u;
    progress(k, "LIVE LCD KEYS / ENTER: NAV ON");
    return HOST_OK;
}

static host_result_t input(keypad_host_t *k, uint8_t raw[32], uint16_t *got) {
    if (!k->ready) return HOST_ARGUMENT;
    host_result_t result = usb_host_packet(k->usb, HOST_IN, k->endpoints.input & 15u,
                                           1u, k->input_pid, raw, k->endpoints.input_size, got);
    if (result != HOST_OK) return result;
    k->input_pid ^= 1u;
    if (*got && !k->report(k->context, raw, *got, k->feature)) return HOST_BUFFER_ERROR;
    return HOST_OK;
}

host_result_t keypad_host_poll(keypad_host_t *k) {
    uint8_t raw[32];
    uint16_t got;
    return input(k, raw, &got);
}

static host_result_t exchange(keypad_host_t *k, uint8_t feature, uint8_t function,
                               const uint8_t *parameters, uint8_t count, uint8_t reply[32]) {
    if (!k->ready || count > 16u) return HOST_ARGUMENT;
    k->software_id = (uint8_t)(k->software_id % 15u + 1u);
    uint8_t request[20] = {0x11u, 0xffu, feature, (uint8_t)((function << 4u) | k->software_id)};
    badger_bytes_copy(request + 4u, parameters, count);
    uint32_t started = k->usb->io.now(k->usb->io.context);
    for (;;) {
        uint16_t sent;
        host_result_t result = usb_host_packet(k->usb, HOST_OUT, k->endpoints.output & 15u,
                                               1u, k->output_pid, request, 20u, &sent);
        if (result != HOST_OK && result != HOST_NAK) return result;
        if (result == HOST_OK) { k->output_pid ^= 1u; break; }
        if (k->usb->io.now(k->usb->io.context) - started >= 1000000u) return HOST_REPLY_TIMEOUT;
        result = usb_host_wait(k->usb, (uint32_t)k->endpoints.output_interval * 1000u);
        if (result != HOST_OK) return result;
    }
    while (k->usb->io.now(k->usb->io.context) - started < 1000000u) {
        host_result_t result = usb_host_wait(k->usb, (uint32_t)k->endpoints.input_interval * 1000u);
        if (result != HOST_OK) return result;
        uint16_t got;
        result = input(k, reply, &got);
        if (result == HOST_NAK) continue;
        if (result != HOST_OK) return result;
        int match = got ? match_hidpp_reply(request, reply[0], reply, got) : 0;
        if (match == 1) return HOST_OK;
        if (match < 0) { k->hid_error = reply[5]; return HOST_HID_ERROR; }
    }
    return HOST_REPLY_TIMEOUT;
}

static host_result_t get_diversion(keypad_host_t *k, uint8_t control_id, uint8_t *value) {
    uint8_t parameters[] = {1u, control_id}, reply[32];
    host_result_t result = exchange(k, k->feature, 2u, parameters, 2u, reply);
    if (result != HOST_OK) return result;
    if (reply[4] != 1u || reply[5] != control_id) return HOST_HID_REPLY;
    *value = reply[6] & 1u;
    return HOST_OK;
}

static host_result_t set_diversion(keypad_host_t *k, uint8_t control_id, uint8_t value) {
    uint8_t parameters[] = {1u, control_id, (uint8_t)(2u | value)}, reply[32], actual;
    host_result_t result = exchange(k, k->feature, 3u, parameters, 3u, reply);
    if (result != HOST_OK) return result;
    result = get_diversion(k, control_id, &actual);
    return result != HOST_OK ? result : actual == value ? HOST_OK : HOST_HID_REPLY;
}

host_result_t keypad_host_navigation(keypad_host_t *k) {
    if (k->changed) return HOST_ARGUMENT;
    uint8_t parameters[] = {0x1bu, 4u}, reply[32];
    host_result_t result = exchange(k, 0u, 0u, parameters, 2u, reply);
    if (result != HOST_OK) return result;
    if (!reply[4] || reply[4] == 0xffu) return HOST_HID_REPLY;
    k->feature = reply[4];
    for (uint8_t i = 0u; i < 2u; ++i) {
        result = get_diversion(k, (uint8_t)(0xa1u + i), k->original + i);
        if (result != HOST_OK) return result;
    }
    for (uint8_t i = 0u; i < 2u; ++i) {
        if (k->original[i]) continue;
        k->changed |= (uint8_t)(1u << i);
        result = set_diversion(k, (uint8_t)(0xa1u + i), 1u);
        if (result != HOST_OK) return result;
    }
    return HOST_OK;
}

host_result_t keypad_host_restore(keypad_host_t *k) {
    host_result_t error = HOST_OK;
    for (uint8_t i = 0u; i < 2u; ++i) {
        if (!(k->changed & (1u << i))) continue;
        host_result_t result = set_diversion(k, (uint8_t)(0xa1u + i), k->original[i]);
        if (result == HOST_OK) k->changed &= (uint8_t)~(1u << i);
        else error = result;
    }
    return error;
}

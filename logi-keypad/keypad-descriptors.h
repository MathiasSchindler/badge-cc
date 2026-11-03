#ifndef U26_KEYPAD_DESCRIPTORS_H
#define U26_KEYPAD_DESCRIPTORS_H

#include "../src/shared/bytes.h"

typedef struct {
    uint8_t configuration, interface, input, output, input_size, output_size;
    uint8_t input_interval, output_interval;
    uint16_t report_size, max_power_ma;
} keypad_endpoints_t;

static inline uint16_t keypad_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8u);
}

static inline int keypad_configuration(const uint8_t *b, uint16_t length,
                                       keypad_endpoints_t *ep) {
    if (!b || !ep || length < 9u || b[0] != 9u || b[1] != 2u ||
        keypad_u16(b + 2u) != length || b[4] != 1u || !b[5]) return 0;
    keypad_endpoints_t candidate;
    badger_bytes_zero((uint8_t *)&candidate, sizeof(candidate));
    candidate.configuration = b[5];
    candidate.max_power_ma = (uint16_t)b[8] * 2u;
    uint8_t interface_seen = 0u, hid_seen = 0u, endpoint_count = 0u;
    for (uint32_t at = 9u; at < length;) {
        if (at + 2u > length || b[at] < 2u || at + b[at] > length) return 0;
        const uint8_t *d = b + at;
        if (d[1] == 4u) {
            if (interface_seen || d[0] != 9u || d[3] || d[4] != 2u ||
                d[5] != 3u || d[6] || d[7]) return 0;
            candidate.interface = d[2];
            interface_seen = 1u;
        } else if (d[1] == 0x21u) {
            if (!interface_seen || hid_seen || d[0] != 9u || d[5] != 1u || d[6] != 0x22u)
                return 0;
            candidate.report_size = keypad_u16(d + 7u);
            hid_seen = 1u;
        } else if (d[1] == 5u) {
            if (!interface_seen || d[0] != 7u || d[3] != 3u || !(d[2] & 15u) ||
                (d[2] & 0x70u) || !d[6]) return 0;
            uint16_t size = keypad_u16(d + 4u);
            if (!size || size > 64u) return 0;
            if (d[2] & 0x80u) {
                if (candidate.input || size != 32u) return 0;
                candidate.input = d[2];
                candidate.input_size = (uint8_t)size;
                candidate.input_interval = d[6];
            } else {
                if (candidate.output || size < 20u) return 0;
                candidate.output = d[2];
                candidate.output_size = (uint8_t)size;
                candidate.output_interval = d[6];
            }
            ++endpoint_count;
        } else return 0;
        at += d[0];
    }
    if (!interface_seen || !hid_seen || endpoint_count != 2u ||
        !candidate.input || !candidate.output || candidate.report_size != 140u) return 0;
    badger_bytes_copy((uint8_t *)ep, (const uint8_t *)&candidate, sizeof(candidate));
    return 1;
}

static inline int keypad_known_hid(const uint8_t *bytes, uint16_t length) {
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

#endif

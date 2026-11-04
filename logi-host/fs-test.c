#include "fs.h"
#include "fs_lcd.h"
#include "../src/shared/bytes.h"
#include "../src/tools/diagnostics_logic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t state(const fs_tx_t *tx, unsigned i) {
    assert(i < tx->count);
    return (uint8_t)(tx->words[i / 16u] >> (2u * (i % 16u))) & 3u;
}

/* Deliberately bitwise reference, independent of the production lookup decoder. */
static unsigned unpack(const fs_tx_t *tx, unsigned *offset, uint8_t bytes[68]) {
    uint8_t previous = 1u, ones = 0u;
    unsigned bits = 0u;
    memset(bytes, 0, 68u);
    while (*offset < tx->count && state(tx, *offset) == 1u) ++*offset;
    while (state(tx, *offset)) {
        uint8_t line = state(tx, (*offset)++), bit = (line & 1u) == previous;
        assert(line == 1u || line == 2u);
        previous = line & 1u;
        if (ones == 6u) { assert(!bit); ones = 0u; continue; }
        assert(bits < 68u * 8u);
        bytes[bits / 8u] |= bit << (bits % 8u);
        ++bits;
        ones = bit ? (uint8_t)(ones + 1u) : 0u;
    }
    assert(!ones || ones < 6u);
    assert(!state(tx, (*offset)++));
    assert(!state(tx, (*offset)++));
    assert(state(tx, (*offset)++) == 1u);
    assert(!(bits % 8u) && bytes[0] == 0x80u);
    return bits / 8u;
}

static int32_t receive(const fs_tx_t *tx, fs_rx_t *rx, fs_packet_t *packet) {
    fs_rx_init(rx);
    unsigned i = 0u;
    while (i < tx->count && state(tx, i)) {
        uint16_t samples = 0u;
        uint8_t n = 0u;
        while (n < 8u && i < tx->count && state(tx, i))
            samples |= (uint16_t)state(tx, i++) << (2u * n++);
        int32_t error = fs_rx_feed(rx, samples, n);
        if (error) return error;
    }
    return fs_rx_finish(rx, packet);
}

static void wire_tests(void) {
    fs_wire_init();
    assert(fs_crc5(0u) == 2u);
    for (unsigned bits = 0u; bits < 2048u; ++bits) {
        unsigned value = bits | (unsigned)fs_crc5((uint16_t)bits) << 11u, crc = 31u;
        for (unsigned i = 0u; i < 16u; ++i) {
            unsigned feedback = (crc ^ (value >> i)) & 1u;
            crc >>= 1u; if (feedback) crc ^= 0x14u;
        }
        assert(crc == 6u);
    }
    assert(fs_crc16((const uint8_t *)"123456789", 9u) == 0xb4c8u);
    assert(fs_crc16(0, 0u) == 0u);
    fs_tx_t token = {0};
    assert(!fs_token(&token, FS_SETUP, 0u, 0u));
    uint8_t bytes[68]; unsigned offset = 0u;
    assert(unpack(&token, &offset, bytes) == 4u);
    assert(!memcmp(bytes, (uint8_t[]){0x80u,0x2du,0u,0x10u}, 4u));
    const uint8_t sync[] = {2u,1u,2u,1u,2u,1u,2u,2u};
    for (unsigned i = 0u; i < 8u; ++i) assert(state(&token, i) == sync[i]);
    uint32_t random = 0x37c0181u;
    for (unsigned n = 0u; n <= 64u; ++n) for (unsigned pattern = 0u; pattern < 5u; ++pattern) {
        uint8_t payload[64];
        for (unsigned i = 0u; i < n; ++i) {
            random = random * 1664525u + 1013904223u;
            payload[i] = pattern == 0u ? 255u : pattern == 1u ? 0u :
                         pattern == 2u ? 0x7eu : pattern == 3u ? 0xfcu : (uint8_t)(random >> 24u);
        }
        fs_tx_t tx = {0}; fs_rx_t rx; fs_packet_t packet;
        uint8_t pid = n & 1u ? FS_DATA0 : FS_DATA1;
        assert(!fs_data(&tx, pid, payload, (uint8_t)n));
        offset = 0u;
        assert(unpack(&tx, &offset, bytes) == n + 4u);
        assert(bytes[1] == pid && !memcmp(bytes + 2u, payload, n));
        assert(!receive(&tx, &rx, &packet));
        assert(packet.pid == pid && packet.length == n && !memcmp(packet.data, payload, n));
        rx.bytes[1] ^= 0x10u;
        assert(fs_rx_finish(&rx, &packet) == FS_PID);
        rx.bytes[1] ^= 0x10u;
        rx.crc ^= 1u;
        assert(fs_rx_finish(&rx, &packet) == FS_CRC);
    }
    for (unsigned i = 0u; i < 3u; ++i) {
        uint8_t pid = (uint8_t[]){FS_ACK,FS_NAK,FS_STALL_PID}[i];
        fs_tx_t tx = {0}; fs_rx_t rx; fs_packet_t packet;
        assert(!fs_handshake(&tx, pid));
        assert(!receive(&tx, &rx, &packet) && packet.pid == pid && !packet.length);
    }
    fs_rx_t rx; fs_rx_init(&rx);
    assert(fs_rx_feed(&rx, 3u, 1u) == FS_LINE);
    fs_rx_init(&rx); assert(fs_rx_feed(&rx, 0u, 1u) == FS_LINE);
    fs_rx_init(&rx); rx.state = 13u;
    assert(fs_rx_feed(&rx, 1u, 1u) == FS_ENCODING);
    fs_rx_init(&rx); rx.length = FS_PACKET_BYTES;
    assert(fs_rx_feed(&rx, 0x9999u, 8u) == FS_OVERFLOW);
    fs_tx_t tx = {.count = FS_TX_WORDS * 16u};
    assert(fs_handshake(&tx, FS_ACK) == FS_OVERFLOW);
    tx.count = 0u;
    assert(fs_token(&tx, FS_IN, 128u, 0u) == FS_PID);
    assert(fs_token(&tx, FS_IN, 0u, 16u) == FS_PID);
    assert(fs_data(&tx, FS_DATA0, 0, 1u) == FS_LENGTH);
    assert(fs_sof(&tx, 2048u) == FS_LENGTH);
}

static const uint8_t descriptor[18] = {
    18,1,0,2,0,0,0,8,0x6d,4,0x54,0xc3,0,1,1,2,3,1
};
static const uint8_t configuration[] = {
    9,2,41,0,1,1,4,0x80,180, 9,4,0,0,2,3,0,0,0,
    9,0x21,0x11,1,0,1,0x22,140,0,
    7,5,0x81,3,32,0,5, 7,5,1,3,64,0,5
};
static const uint8_t hid[] = {
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
typedef struct {
    uint32_t now, sof, resets, exchanges, naks;
    uint8_t step, read_offset, address, mps;
    uint8_t stall, timeout, forever_nak, corrupt, duplicate, duplicated, bad_toggle, short_packet, status_nak;
    int32_t reset_error;
    uint8_t keypad, configured, control_state, request[8], inputs, output_naks;
    uint8_t bad_config, bad_hid, no_query, query_ready, hid_error, cleanup_stall;
    uint16_t control_offset;
    uint32_t last_input, last_output;
    uint8_t lcd_mode, out_pid, in_pid, control_pid, reply[32], reply_length, image_part;
    uint8_t lcd_images, busy_once, reject_image, drop_image_reply, output_nak, output_naked, early_reply;
    uint8_t control_fault, control_setup_nak, control_status_nak;
    uint8_t output_report[4095];
    uint16_t output_count, visible_images;
    uint8_t startup_cleared;
    uint8_t update_key, update_inverted;
    uint16_t rendered_inverted;
    uint32_t updates;
    uint8_t animation_mode, image_key, animation_frame;
    uint32_t animation_frames, frame_times[64];
    uint32_t image_offset, keepalives, fragments, last_keepalive, configured_at;
} device_t;

static int32_t lcd_transfer(device_t *d, const fs_tx_t *tx, const uint8_t token[68],
                              unsigned offset, fs_packet_t *p) {
    p->length = 0u;
    if (token[1] == FS_IN) {
        assert(d->now - d->last_input >= 5000u); d->last_input = d->now;
        if (!d->reply_length) { p->pid = FS_NAK; return 0; }
        p->pid = d->in_pid; d->in_pid ^= FS_DATA0 ^ FS_DATA1;
        p->length = d->reply_length; d->reply_length = 0u;
        memcpy(p->data, d->reply, p->length);
        return 0;
    }
    assert(token[1] == FS_OUT);
    if (d->control_state != 4u) assert(d->now - d->last_output >= 5000u);
    d->last_output = d->now;
    uint8_t raw[68];
    unsigned size = unpack(tx, &offset, raw) - 4u;
    uint8_t *pid = d->control_state == 4u ? &d->control_pid : &d->out_pid;
    assert(size && size <= 64u && raw[1] == *pid);
    assert(fs_crc16(raw + 2u, (uint16_t)size) == (uint16_t)(raw[size + 2u] | raw[size + 3u] << 8u));
    if (d->output_nak && d->output_count >= 64u && !d->output_naked) {
        d->output_naked = 1u; p->pid = FS_NAK; return 0;
    }
    assert(d->output_count + size <= sizeof(d->output_report));
    memcpy(d->output_report + d->output_count, raw + 2u, size);
    d->output_count += size;
    *pid ^= FS_DATA0 ^ FS_DATA1; p->pid = FS_ACK;
    const uint8_t *report = d->output_report;
    unsigned expected = report[0] == 0x11u ? 20u : 4095u;
    if (report[0] == 0x14u) assert(d->control_state == 4u);
    assert(d->output_count <= expected);
    if (d->early_reply && report[0] == 0x14u && d->output_count == size) {
        assert(!d->reply_length);
        memset(d->reply, 0, sizeof(d->reply));
        memcpy(d->reply, (uint8_t[]){0x13,0xff,2,0x2b}, 4u);
        d->reply[4] = 0xc0u | d->image_part;
        d->reply[5] = d->busy_once && d->image_part == 2u ? 0xfbu : 0u;
        d->reply_length = 32u;
    }
    if (d->output_count != expected) { assert(size == (d->control_state == 4u ? d->mps : 64u)); return 0; }
    assert(!d->reply_length);
    memset(d->reply, 0, sizeof(d->reply));
    d->output_count = 0u;
    if (report[0] == 0x11u) {
        assert(size == 20u && report[1] == 0xffu);
        memcpy(d->reply, report, 4u);
        d->reply_length = 20u;
        if (report[2] == 0u) {
            assert((report[3] >> 4u) == 0u);
            assert((report[4] == 0x1bu && report[5] == 4u) || (!report[4] && report[5] == 8u));
            d->reply[4] = report[4] == 0x1bu ? 0x0bu : 4u;
        } else {
            assert(report[2] == 4u && report[3] >> 4u == 1u && report[4] == 0x0bu && report[5] == 0xb8u);
            ++d->keepalives; d->last_keepalive = d->now;
        }
    } else {
        assert(report[0] == 0x14u && size == 4095u % d->mps && d->keepalives);
        assert(d->now - d->last_keepalive < 3000000u);
        unsigned key = d->lcd_images < 9u ? d->lcd_images : d->update_key;
        if (d->animation_mode) {
            if (d->image_part == 1u) {
                unsigned x = report[9] << 8u | report[10], y = report[11] << 8u | report[12];
                assert(x >= 23u && x <= 339u && !((x - 23u) % 158u));
                assert(y >= 6u && y <= 322u && !((y - 6u) % 158u));
                d->image_key = (uint8_t)((y - 6u) / 158u * 3u + (x - 23u) / 158u);
            }
            key = d->image_key;
            assert(key == (d->lcd_images < 9u ? d->lcd_images : FS_LCD_ANIMATION_KEY) ||
                   (d->lcd_images == 9u && key == d->update_key));
        }
        int animated = d->animation_mode && d->lcd_images == 9u && key == FS_LCD_ANIMATION_KEY;
        const fs_lcd_image_t *image = animated ? &fs_lcd_animation[d->animation_frame] :
                                      d->lcd_images < 9u || !d->update_inverted ?
                                      &fs_lcd_images[key] : &fs_lcd_inverted_images[key];
        unsigned header = d->image_part == 1u ? 20u : 5u;
        unsigned count = image->length - d->image_offset;
        if (count > 4095u - header) count = 4095u - header;
        int last = d->image_offset + count == image->length;
        assert(report[4] == (d->image_part | 0x20u | (header == 20u ? 0x80u : 0u) | (last ? 0x40u : 0u)));
        if (header == 20u) {
            assert((report[9] << 8u | report[10]) == 23u + key % 3u * 158u);
            assert((report[11] << 8u | report[12]) == 6u + key / 3u * 158u);
            assert(report[13] == 0u && report[14] == 118u && report[15] == 0u && report[16] == 118u);
            assert(((uint32_t)report[18] << 8u | report[19]) == image->length);
        }
        assert(!memcmp(report + header, image->bytes + d->image_offset, count));
        for (unsigned i = header + count; i < 4095u; ++i) assert(!report[i]);
        memcpy(d->reply, (uint8_t[]){0x13,0xff,2,0x2b}, 4u);
        d->reply[4] = 0xc0u | d->image_part;
        d->reply[5] = d->reject_image ? 1u : 0u;
        if (d->busy_once && d->image_part == 2u) {
            d->busy_once = 0u; d->reply[5] = 0xfbu;
            d->image_part = 1u; d->image_offset = 0u;
        } else {
            ++d->fragments;
            d->image_offset += count;
            ++d->image_part;
            if (last) {
                d->visible_images |= 1u << key;
                if (d->lcd_images < 9u) ++d->lcd_images;
                else if (animated) {
                    assert(d->animation_frames < sizeof(d->frame_times) / sizeof(*d->frame_times));
                    d->frame_times[d->animation_frames++] = d->now;
                    d->animation_frame = (uint8_t)((d->animation_frame + 1u) % FS_LCD_ANIMATION_FRAMES);
                }
                else {
                    if (d->update_inverted) d->rendered_inverted |= 1u << key;
                    else d->rendered_inverted &= (uint16_t)~(1u << key);
                    ++d->updates;
                }
                d->image_offset = 0u; d->image_part = 1u;
            }
        }
        d->reply_length = d->drop_image_reply || d->early_reply ? 0u : 32u;
    }
    return 0;
}

static int32_t keypad_exchange(device_t *d, const fs_tx_t *tx, unsigned bits,
                               const uint8_t token[68], unsigned offset, fs_packet_t *p) {
    assert((bits & 127u) == 1u);
    uint8_t raw[68];
    p->length = 0u;
    if (bits >> 7u) {
        assert((bits >> 7u) == 1u && d->configured);
        if (d->lcd_mode) return lcd_transfer(d, tx, token, offset, p);
        if (token[1] == FS_OUT) {
            assert(d->now - d->last_output >= 5000u); d->last_output = d->now;
            assert(unpack(tx, &offset, raw) == 24u && raw[1] == FS_DATA0);
            assert(!memcmp(raw + 2u, (uint8_t[]){0x11,0xff,0,1,0x1b,4}, 6u));
            if (d->output_naks) { --d->output_naks; p->pid = FS_NAK; }
            else { d->query_ready = 1u; p->pid = FS_ACK; }
        } else {
            assert(token[1] == FS_IN && d->now - d->last_input >= 5000u);
            d->last_input = d->now;
            if (!d->query_ready || d->no_query || d->inputs >= 4u) { p->pid = FS_NAK; return 0; }
            memset(p->data, 0, 32u);
            if (!d->inputs) {
                memcpy(p->data, (uint8_t[]){0x11,0xff,0,1,0x0b}, 5u);
                if (d->hid_error) memcpy(p->data + 2u, (uint8_t[]){0xff,0,1,7}, 4u);
                p->length = 20u; p->pid = FS_DATA0;
            } else {
                memcpy(p->data, (uint8_t[]){0x13,0xff,2,0,42,1,9}, 7u);
                p->length = 32u; p->pid = d->inputs < 3u ? FS_DATA1 : FS_DATA0;
                if (d->inputs == 3u) p->data[6] = 0u;
            }
            ++d->inputs;
        }
        return 0;
    }
    if (token[1] == FS_SETUP) {
        assert(unpack(tx, &offset, raw) == 12u && raw[1] == FS_DATA0);
        memcpy(d->request, raw + 2u, 8u);
        d->control_offset = 0u;
        if (d->request[0] == 0x21u) {
            assert(d->lcd_mode && !memcmp(d->request, (uint8_t[]){0x21,9,0x14,2,0,0,0xff,0x0f}, 8u));
            if (d->control_fault == 5u) { p->pid = FS_STALL_PID; return 0; }
            if (d->control_fault == 6u || d->control_setup_nak) {
                d->control_setup_nak = 0u; p->pid = FS_NAK; return 0;
            }
            d->control_state = 4u;
            d->control_pid = FS_DATA1;
        } else if (d->request[1] == 9u) {
            assert(!d->request[0] && d->request[2] <= 1u && !d->request[6] && !d->request[7]);
            d->control_state = 3u;
        } else {
            assert(d->request[1] == 6u && (d->request[3] == 2u || d->request[3] == 0x22u));
            assert(d->request[0] == (d->request[3] == 2u ? 0x80u : 0x81u));
            d->control_state = 1u;
        }
        p->pid = FS_ACK;
    } else if (d->control_state == 4u) {
        int32_t error = lcd_transfer(d, tx, token, offset, p);
        if (!d->output_count && p->pid == FS_ACK) d->control_state = 5u;
        return error;
    } else if (d->control_state == 5u) {
        assert(token[1] == FS_IN);
        if (d->control_status_nak) { d->control_status_nak = 0u; p->pid = FS_NAK; return 0; }
        p->pid = d->control_fault == 4u ? FS_DATA0 : FS_DATA1;
        d->control_state = 0u;
    } else if (d->control_state == 1u) {
        assert(token[1] == FS_IN);
        uint16_t length = keypad_u16(d->request + 6u);
        const uint8_t *source = d->request[3] == 2u ? configuration : hid;
        assert(length <= (d->request[3] == 2u ? sizeof(configuration) : sizeof(hid)));
        p->length = (uint8_t)(length - d->control_offset);
        if (p->length > d->mps) p->length = d->mps;
        p->pid = d->control_offset / d->mps & 1u ? FS_DATA0 : FS_DATA1;
        memcpy(p->data, source + d->control_offset, p->length);
        if (d->bad_config && d->request[3] == 2u && d->control_offset + p->length == 41u)
            p->data[p->length - 1u] = 0u;
        if (d->bad_hid && d->request[3] == 0x22u) p->data[0] ^= 1u;
        d->control_offset += p->length;
        if (d->control_offset == length) d->control_state = 2u;
    } else if (d->control_state == 2u) {
        assert(token[1] == FS_OUT && unpack(tx, &offset, raw) == 4u && raw[1] == FS_DATA1);
        p->pid = FS_ACK; d->control_state = 0u;
    } else {
        assert(d->control_state == 3u && token[1] == FS_IN);
        if (d->cleanup_stall && !d->request[2]) { p->pid = FS_STALL_PID; return 0; }
        p->pid = FS_DATA1; d->configured = d->request[2]; d->control_state = 0u;
        if (d->configured) d->configured_at = d->now;
    }
    return 0;
}

static uint32_t mock_now(void *context) {
    device_t *d = context;
    d->now += 10u;
    if (d->lcd_mode && d->configured && !d->startup_cleared && d->now - d->configured_at >= 300000u) {
        d->startup_cleared = 1u;
        d->visible_images = 0u;
    }
    return d->now;
}
static int32_t mock_reset(void *context) {
    device_t *d = context; ++d->resets; d->now += 20000u; return d->reset_error;
}
static int32_t mock_exchange(void *context, const fs_tx_t *tx, uint8_t reply, fs_packet_t *packet) {
    device_t *d = context;
    unsigned offset = 0u; uint8_t token[68];
    assert(unpack(tx, &offset, token) == 4u);
    unsigned bits = token[2] | (token[3] & 7u) << 8u;
    assert((token[3] >> 3u) == fs_crc5((uint16_t)bits));
    if (token[1] == FS_SOF) { assert(!reply); ++d->sof; return 0; }
    assert(reply);
    ++d->exchanges;
    if (d->keypad && d->step == 8u) return keypad_exchange(d, tx, bits, token, offset, packet);
    assert(!(bits >> 7u));
    if (d->timeout) return FS_TIMEOUT;
    if (d->forever_nak || d->naks || (d->status_nak && d->step == 2u)) {
        if (d->naks) --d->naks;
        packet->pid = FS_NAK; return 0;
    }
    if (d->stall) { packet->pid = FS_STALL_PID; return 0; }
    assert((bits & 127u) == d->address);
    packet->length = 0u;
    if (d->step == 0u || d->step == 3u || d->step == 5u) {
        assert(token[1] == FS_SETUP);
        uint8_t data[68];
        assert(unpack(tx, &offset, data) == 12u && offset == tx->count);
        assert(data[1] == FS_DATA0);
        uint16_t crc = fs_crc16(data + 2u, 8u);
        assert(data[10] == (uint8_t)crc && data[11] == (uint8_t)(crc >> 8u));
        if (d->step == 3u) assert(!memcmp(data + 2u, (uint8_t[]){0,5,1,0,0,0,0,0}, 8u));
        else assert(!memcmp(data + 2u, (uint8_t[]){0x80,6,0,1,0,0,d->step == 0u ? 8u : 18u,0}, 8u));
        packet->pid = FS_ACK;
        ++d->step;
    } else if (d->step == 1u || d->step == 6u) {
        assert(token[1] == FS_IN);
        uint8_t total = d->step == 1u ? 8u : 18u;
        uint8_t mps = d->step == 1u ? 8u : d->mps;
        packet->pid = (d->read_offset / mps) & 1u ? FS_DATA0 : FS_DATA1;
        if (d->bad_toggle) packet->pid ^= FS_DATA0 ^ FS_DATA1;
        if (d->duplicate && d->read_offset && !d->duplicated) {
            packet->pid ^= FS_DATA0 ^ FS_DATA1;
            packet->length = mps;
            memcpy(packet->data, descriptor, mps);
            d->duplicated = 1u;
            return 0;
        }
        packet->length = (uint8_t)(total - d->read_offset);
        if (packet->length > mps) packet->length = mps;
        memcpy(packet->data, descriptor + d->read_offset, packet->length);
        if (!d->read_offset) packet->data[7] = d->mps;
        if (d->corrupt) packet->data[0] = 17u;
        if (d->short_packet) packet->length = 1u;
        d->read_offset += packet->length;
        if (d->read_offset == total) { ++d->step; d->read_offset = 0u; }
    } else if (d->step == 4u) {
        assert(token[1] == FS_IN && d->address == 0u);
        packet->pid = FS_DATA1; ++d->step; d->address = 1u;
    } else {
        assert(d->step == 2u || d->step == 7u);
        assert(token[1] == FS_OUT);
        uint8_t data[68];
        assert(unpack(tx, &offset, data) == 4u && offset == tx->count);
        assert(!memcmp(data, (uint8_t[]){0x80,FS_DATA1,0,0}, 4u));
        packet->pid = FS_ACK; ++d->step;
    }
    return 0;
}

static int32_t probe(device_t *d, fs_result_t *r) {
    fs_bus_t bus = {.context = d, .now = mock_now, .reset = mock_reset, .exchange = mock_exchange};
    return fs_probe(&bus, r);
}

static void host_tests(void) {
    fs_result_t r;
    for (unsigned mps = 8u; mps <= 64u; mps *= 2u) {
        device_t d = {.mps = (uint8_t)mps, .naks = 2u, .duplicate = 1u};
        assert(!probe(&d, &r) && !r.error && r.stage == 6u && r.descriptor_length == 18u);
        assert(r.mps == mps && r.naks == 2u && d.resets == 1u && d.step == 8u && d.sof >= 10u);
        assert(r.descriptor[8] == 0x6du && r.descriptor[10] == 0x54u);
        uint8_t report[64];
        fs_report(&r, 0x1234u, 1u, report);
        assert(badger_bytes_load_u32(report) == FS_REPORT_MAGIC && report[34] == 18u);
        assert(badger_bytes_load_u32(report + 60u) == diag_crc32(report, 60u));
    }
    device_t d = {.mps = 8u, .timeout = 1u}; assert(probe(&d, &r) == FS_TIMEOUT && r.timeouts == 1u);
    d = (device_t){.mps = 8u, .stall = 1u}; assert(probe(&d, &r) == FS_STALL);
    d = (device_t){.mps = 8u, .forever_nak = 1u};
    assert(probe(&d, &r) == FS_DEADLINE && r.naks == 128u && d.now < 300000u && r.phase == 1u);
    d = (device_t){.mps = 64u, .status_nak = 1u};
    assert(probe(&d, &r) == FS_DEADLINE && r.phase == 3u && r.descriptor_length == 8u && r.mps == 64u);
    assert(!memcmp(r.descriptor, (uint8_t[]){18,1,0,2,0,0,0,64}, 8u));
    d = (device_t){.mps = 8u, .bad_toggle = 1u}; assert(probe(&d, &r) == FS_TOGGLE);
    d = (device_t){.mps = 8u, .corrupt = 1u}; assert(probe(&d, &r) == FS_DESCRIPTOR);
    d = (device_t){.mps = 8u, .short_packet = 1u}; assert(probe(&d, &r) == FS_LENGTH);
    d = (device_t){.mps = 3u}; assert(probe(&d, &r) == FS_DESCRIPTOR);
    d = (device_t){.reset_error = FS_LINE};
    assert(probe(&d, &r) == FS_LINE && r.stage == 1u && !d.exchanges);
}

static void keypad_tests(void) {
    for (unsigned mps = 8u; mps <= 64u; mps *= 2u) {
        device_t d = {.mps = (uint8_t)mps, .keypad = 1u, .output_naks = 2u};
        fs_bus_t bus = {.context = &d, .now = mock_now, .reset = mock_reset, .exchange = mock_exchange};
        fs_keypad_t k; fs_result_t r; fs_packet_t p;
        assert(!fs_keypad_start(&bus, &r, &k));
        assert(k.stage == 2u && k.configured && k.endpoints.max_power_ma == 360u &&
               k.report_crc == diag_crc32(hid, sizeof(hid)));
        unsigned events = 0u;
        for (unsigned i = 0u; i < 20000u && k.reports < 3u; ++i) {
            int32_t result = fs_keypad_step(&k, &p);
            assert(result >= 0);
            if (result) {
                uint8_t event[64];
                fs_keypad_event(&k, &p, 123u, 7u, event);
                assert(event[14] == p.length && keypad_u16(event + 12u) == 7u);
                assert(!memcmp(event + 16u, p.data, p.length));
                assert(badger_bytes_load_u32(event + 8u) == ++events);
                assert(badger_bytes_load_u32(event + 60u) == diag_crc32(event, 60u));
            }
        }
        assert(events == 3u && k.buttons == 2u && k.duplicates == 1u && k.query_done && k.feature == 0x0bu);
        assert(!fs_keypad_stop(&k) && !d.configured && k.stage == 3u);
        uint8_t status[64]; fs_keypad_status(&k, 123u, 7u, status);
        assert(status[56] && !status[57] && status[36] == 0x81u && status[38] == 5u && status[40] == 64u);
    }
    for (unsigned mode = 0u; mode < 4u; ++mode) {
        device_t d = {.mps = 64u, .keypad = 1u};
        d.bad_config = mode == 0u; d.bad_hid = mode == 1u; d.no_query = mode == 2u; d.hid_error = mode == 3u;
        fs_bus_t bus = {.context = &d, .now = mock_now, .reset = mock_reset, .exchange = mock_exchange};
        fs_keypad_t k; fs_result_t r; fs_packet_t p;
        int32_t error = fs_keypad_start(&bus, &r, &k);
        if (mode < 2u) assert(error == FS_DESCRIPTOR);
        else {
            assert(!error);
            for (unsigned i = 0u; i < 100000u && !k.error; ++i) {
                error = fs_keypad_step(&k, &p);
                if (error < 0) k.error = error;
            }
            assert(k.error == (mode == 2u ? FS_DEADLINE : FS_HID_REPLY));
            if (mode == 3u) assert(k.reports == 1u && error == 1);
        }
        assert(fs_keypad_stop(&k) < 0 && !d.configured && k.stage == 4u);
    }
    device_t d = {.mps = 64u, .keypad = 1u, .cleanup_stall = 1u};
    fs_bus_t bus = {.context = &d, .now = mock_now, .reset = mock_reset, .exchange = mock_exchange};
    fs_keypad_t k; fs_result_t r;
    assert(!fs_keypad_start(&bus, &r, &k));
    k.query_done = 1u;
    assert(fs_keypad_stop(&k) == FS_STALL && k.cleanup_error == FS_STALL && k.configured);
    puts("Keypad: multi-packet config/HID, all EP0 sizes, interrupt intervals/toggles/duplicates,");
    puts("read-only HID++ query, raw events, malformed descriptors/replies and deconfiguration PASS");
}

static void lcd_key(fs_lcd_t *l, uint8_t key) {
    fs_packet_t p = {.pid = FS_DATA1, .length = 32u,
                     .data = {0x13,0xff,2,0,42,1}};
    p.data[6] = key;
    assert(!fs_lcd_input(l, &p));
}

static void lcd_tick(device_t *d, fs_keypad_t *k, fs_lcd_t *l) {
    d->now += 1000u;
    assert(!fs_lcd_step(l, k));
    fs_packet_t p;
    int32_t result = fs_keypad_step(k, &p);
    assert(result >= 0);
    if (result) assert(!fs_lcd_input(l, &p));
}

static void lcd_until_updates(device_t *d, fs_keypad_t *k, fs_lcd_t *l, uint32_t updates) {
    for (unsigned loops = 0u; loops < 20000u; ++loops) {
        lcd_tick(d, k, l);
        if (l->images == 9u && !l->rendering && !l->waiting && !k->output_length &&
            d->updates == updates) return;
    }
    assert(0);
}

static void lcd_toggle_tests(void) {
    device_t d = {.mps = 64u, .keypad = 1u, .lcd_mode = 1u,
                  .out_pid = FS_DATA0, .in_pid = FS_DATA0, .image_part = 1u,
                  .update_key = 0u, .update_inverted = 1u};
    fs_bus_t bus = {.context = &d, .now = mock_now, .reset = mock_reset, .exchange = mock_exchange};
    fs_keypad_t k; fs_result_t r; fs_lcd_t l, static_lcd;
    assert(!fs_keypad_start(&bus, &r, &k));
    k.host.budget = 62000000u;
    fs_lcd_begin(&static_lcd, FS_LCD_STATIC);
    lcd_key(&static_lcd, 1u);
    assert(!static_lcd.desired);
    fs_lcd_begin(&l, FS_LCD_TOGGLE);
    lcd_key(&l, 1u);
    lcd_until_updates(&d, &k, &l, 1u);
    assert(l.desired == 1u && l.displayed == 1u && d.rendered_inverted == 1u);
    lcd_key(&l, 1u);
    lcd_key(&l, 10u);
    for (unsigned i = 0u; i < 50u; ++i) lcd_tick(&d, &k, &l);
    assert(d.updates == 1u && l.desired == 1u);
    lcd_key(&l, 0u);
    assert(l.desired == 1u);
    lcd_key(&l, 1u);
    d.update_inverted = 0u;
    unsigned loops = 0u;
    while (!l.rendering || !l.offset) {
        assert(++loops < 2000u);
        lcd_tick(&d, &k, &l);
    }
    assert(!l.inverted);
    lcd_key(&l, 0u); lcd_key(&l, 1u);
    lcd_until_updates(&d, &k, &l, 2u);
    assert(l.desired == 1u && !l.displayed && !d.rendered_inverted);
    d.update_inverted = 1u;
    lcd_until_updates(&d, &k, &l, 3u);
    assert(l.displayed == 1u && d.rendered_inverted == 1u);

    lcd_key(&l, 0u); lcd_key(&l, 9u);
    d.update_key = 8u; d.update_inverted = 1u;
    d.busy_once = d.output_nak = 1u; d.output_naked = 0u;
    lcd_until_updates(&d, &k, &l, 4u);
    assert(l.displayed == 0x101u && d.rendered_inverted == 0x101u && d.output_naked);
    lcd_key(&l, 0u); lcd_key(&l, 5u); lcd_key(&l, 0u); lcd_key(&l, 5u);
    for (unsigned i = 0u; i < 50u; ++i) lcd_tick(&d, &k, &l);
    assert(d.updates == 4u && l.desired == 0x101u);
    lcd_key(&l, 0u); lcd_key(&l, 1u);
    l.stopping = 1u;
    lcd_key(&l, 0u); lcd_key(&l, 4u);
    d.update_key = 0u; d.update_inverted = 0u;
    lcd_until_updates(&d, &k, &l, 5u);
    assert(l.desired == 0x100u && l.displayed == 0x100u && d.rendered_inverted == 0x100u);
    assert(l.images == 9u && l.fragments > 27u && d.keepalives > 2u);
    assert(!fs_keypad_stop(&k) && !d.configured);
    puts("LCD toggle: press edges, release/held/invalid/static filtering, startup input, per-key JPEG redraw,");
    puts("in-flight snapshot, queued-toggle coalescing, busy/NAK retry, keepalive and stop/drain PASS");
}

static void lcd_animation_tests(void) {
    device_t d = {.mps = 64u, .keypad = 1u, .lcd_mode = 1u,
                  .out_pid = FS_DATA0, .in_pid = FS_DATA0, .image_part = 1u,
                  .animation_mode = 1u, .update_key = 0u, .update_inverted = 1u};
    fs_bus_t bus = {.context = &d, .now = mock_now, .reset = mock_reset, .exchange = mock_exchange};
    fs_keypad_t k; fs_result_t r; fs_lcd_t l;
    assert(!fs_keypad_start(&bus, &r, &k));
    k.host.budget = 62000000u;
    fs_lcd_begin(&l, FS_LCD_ANIMATE);
    lcd_until_updates(&d, &k, &l, 0u);
    uint32_t hold_started = l.hold_started;
    unsigned loops = 0u;
    while (l.animation_frames < 25u) {
        assert(++loops < 20000u);
        lcd_tick(&d, &k, &l);
    }
    assert(d.animation_frames == 25u);
    for (unsigned i = 1u; i < d.animation_frames; ++i) {
        uint32_t gap = d.frame_times[i] - d.frame_times[i - 1u];
        assert(gap >= 195000u && gap <= 220000u);
    }
    uint32_t average = (d.frame_times[24] - d.frame_times[0]) / 24u;
    assert(average >= 199000u && average <= 205000u);
    lcd_key(&l, 1u);
    lcd_until_updates(&d, &k, &l, 1u);
    assert(l.displayed == 1u && d.rendered_inverted == 1u);
    lcd_key(&l, 1u); lcd_key(&l, 5u); lcd_key(&l, 0u);
    assert(l.desired == 1u);
    loops = 0u;
    while (l.animation_frames < 35u || !l.rendering || !l.animated) {
        assert(++loops < 20000u);
        lcd_tick(&d, &k, &l);
    }
    lcd_key(&l, 1u);
    d.update_inverted = 0u;
    l.stopping = 1u;
    lcd_until_updates(&d, &k, &l, 2u);
    assert(!l.displayed && !d.rendered_inverted && !l.desired);
    uint16_t frames = l.animation_frames;
    for (unsigned i = 0u; i < 500u; ++i) lcd_tick(&d, &k, &l);
    assert(l.animation_frames == frames && frames == d.animation_frames && l.hold_started == hold_started);
    assert(d.keepalives >= 5u);
    uint8_t status[64] = {0};
    fs_lcd_status(&l, status);
    assert(status[58] == FS_LCD_ANIMATE && status[51] == frames);
    assert(!fs_keypad_stop(&k) && !d.configured);
    puts("LCD animation: exact eight-frame JPEG sequence/center geometry, 5-fps cadence,");
    puts("concurrent key redraw, reserved center/held filtering, keepalive and in-flight stop/drain PASS");
}

static void lcd_tests(void) {
    for (unsigned mode = 0u; mode < 10u; ++mode) {
        device_t d = {.mps = mode < 7u ? 64u : (uint8_t)(8u << (mode - 7u)),
                      .keypad = 1u, .lcd_mode = 1u, .out_pid = FS_DATA0,
                      .in_pid = FS_DATA0, .image_part = 1u, .output_nak = 1u,
                      .busy_once = mode == 1u, .reject_image = mode == 2u, .drop_image_reply = mode == 3u};
        d.early_reply = mode == 1u;
        d.control_fault = mode < 7u ? (uint8_t)mode : 0u;
        d.control_setup_nak = d.control_status_nak = mode == 1u;
        fs_bus_t bus = {.context = &d, .now = mock_now, .reset = mock_reset, .exchange = mock_exchange};
        fs_keypad_t k; fs_result_t r; fs_lcd_t l; fs_packet_t p;
        assert(!fs_keypad_start(&bus, &r, &k));
        k.host.budget = 62000000u;
        fs_lcd_begin(&l, FS_LCD_STATIC);
        uint32_t began = d.now;
        int32_t error = 0;
        for (unsigned loops = 0u; loops < 40000u; ++loops) {
            d.now += 1000u;
            error = fs_lcd_step(&l, &k);
            if (error) break;
            int32_t result = fs_keypad_step(&k, &p);
            if (result < 0) { error = result; break; }
            if (result) error = fs_lcd_input(&l, &p);
            assert(l.images <= d.lcd_images && l.fragments <= d.fragments);
            if (error || (l.images == 9u && d.now - l.hold_started >= 1200000u)) break;
        }
        if (mode < 2u || mode >= 7u) {
            assert(!error && l.images == 9u && d.lcd_images == 9u && d.output_naked);
            assert(d.startup_cleared && d.visible_images == 0x1ffu);
            assert(l.fragments == d.fragments && l.fragments >= 27u && d.keepalives >= 3u);
            assert(d.now - began < 30000000u);
            uint8_t status[64] = {0}; fs_lcd_status(&l, status);
            assert(status[50] == 9u && status[58] == 1u);
        } else {
            assert(error == (mode == 2u ? FS_LCD_REPLY : mode == 4u ? FS_PID :
                             mode == 5u ? FS_STALL : FS_DEADLINE));
            k.error = error;
        }
        assert(fs_keypad_stop(&k) == error && !d.configured);
    }
    puts("LCD: nine prepared JPEGs reconstructed over HID SET_REPORT, EP0 8/16/32/64, final short packet,");
    puts("SETUP/data/status NAKs, toggles, early reply, busy restart, keepalive, rejection/STALL/deadline PASS");
}

int main(void) {
    wire_tests();
    host_tests();
    keypad_tests();
    lcd_tests();
    lcd_toggle_tests();
    lcd_animation_tests();
    puts("Full-speed: independent wire vectors, all payload sizes/stuffing, bad CRC/PID/line,");
    puts("reset, SOF, EP0 8/16/32/64, address/status/toggle, duplicate/NAK/STALL/deadline PASS");
    return 0;
}

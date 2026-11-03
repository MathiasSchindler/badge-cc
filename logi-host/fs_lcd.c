#include "fs_lcd.h"
#include "../src/tools/diagnostics_logic.h"
#include "lcd-images.h"

static int32_t accept_reply(fs_lcd_t *l, const fs_packet_t *p, uint32_t now);

void fs_lcd_begin(fs_lcd_t *l) {
    badger_bytes_zero((uint8_t *)l, sizeof(*l));
    l->stage = FS_LCD_DISCOVER;
    l->part = 1u;
    l->software = 1u;
}

int32_t fs_lcd_step(fs_lcd_t *l, fs_keypad_t *k) {
    if (!l->stage || l->stage >= FS_LCD_DONE || !k->query_done) return 0;
    uint32_t now = k->host.bus->now(k->host.bus->context);
    if (l->waiting) {
        if (k->output_length) return 0;
        if (l->reply.length) {
            int32_t error = accept_reply(l, &l->reply, now);
            l->reply.length = 0u;
            return error;
        }
        if (!l->reply_started) l->reply_started = now;
        return now - l->reply_started >= 1000000u ? FS_DEADLINE : 0;
    }
    if (now - l->pause_started < l->pause_us) return 0;
    if (k->output_length) return FS_BUSY;
    uint16_t length = 20u;
    if (!l->feature || !l->last_keepalive || now - l->last_keepalive >= 1000000u) {
        badger_bytes_zero(l->command, 20u);
        l->command[0] = 0x11u; l->command[1] = 0xffu;
        l->software = (uint8_t)(l->software % 15u + 1u);
        l->command[3] = l->software;
        if (!l->feature) {
            l->stage = FS_LCD_DISCOVER;
            l->command[5] = 8u;
        } else {
            l->stage = FS_LCD_KEEPALIVE;
            l->command[2] = l->feature;
            l->command[3] |= 0x10u;
            l->command[4] = 0x0bu; l->command[5] = 0xb8u;
        }
    } else if (l->images < 9u) {
        l->stage = FS_LCD_UPLOAD;
        const fs_lcd_image_t *image = &fs_lcd_images[l->images];
        l->count = keypad_lcd_packet(l->report, l->images + 1u, image->bytes,
                                     image->length, l->offset, l->part);
        if (!l->count) return FS_LENGTH;
        badger_bytes_copy(l->command, l->report, 20u);
        length = LCD_PACKET_SIZE;
    } else {
        l->stage = FS_LCD_HOLD;
        return 0;
    }
    int32_t error = fs_keypad_queue(k, length == 20u ? l->command : l->report, length);
    if (error) return error;
    l->waiting = 1u;
    l->reply_started = 0u;
    l->reply.length = 0u;
    return 0;
}

int32_t fs_lcd_input(fs_lcd_t *l, const fs_packet_t *p) {
    if (!l->waiting || !p->length) return 0;
    int match = match_hidpp_reply(l->command, p->data[0], p->data, p->length);
    if (!match) return 0;
    if (match < 0) return FS_HID_REPLY;
    if (p->length > 32u || l->reply.length) return FS_LCD_REPLY;
    /* A device may acknowledge the header before the final USB OUT packet. */
    l->reply.length = p->length;
    badger_bytes_copy(l->reply.data, p->data, p->length);
    return 0;
}

static int32_t accept_reply(fs_lcd_t *l, const fs_packet_t *p, uint32_t now) {
    if (l->stage == FS_LCD_DISCOVER) {
        if (!p->data[4] || p->data[4] == 0xffu) return FS_HID_REPLY;
        l->feature = p->data[4];
    } else if (l->stage == FS_LCD_KEEPALIVE) {
        l->last_keepalive = now;
    } else if (l->stage == FS_LCD_UPLOAD) {
        if (p->data[0] != 0x13u || p->length != 32u) return FS_LCD_REPLY;
        if (p->data[5] == 0xfbu) {
            if (++l->retries >= 3u) return FS_LCD_REPLY;
            l->offset = 0u; l->part = 1u;
            l->pause_started = now; l->pause_us = 100000u;
        } else {
            if (p->data[5]) return FS_LCD_REPLY;
            ++l->fragments;
            l->offset += l->count;
            ++l->part;
            l->pause_started = now; l->pause_us = 5000u;
            if (l->offset == fs_lcd_images[l->images].length) {
                ++l->images; l->offset = 0u; l->part = 1u; l->retries = 0u;
                if (l->images == 9u) { l->hold_started = now; l->stage = FS_LCD_HOLD; }
            }
        }
    } else return FS_LCD_REPLY;
    l->waiting = 0u;
    return 0;
}

void fs_lcd_status(const fs_lcd_t *l, uint8_t bytes[64]) {
    bytes[50] = l->images; bytes[51] = l->fragments;
    bytes[58] = l->stage != 0u; bytes[59] = l->stage;
    badger_bytes_store_u32(bytes + 60u, diag_crc32(bytes, 60u));
}

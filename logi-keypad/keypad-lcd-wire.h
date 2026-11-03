#ifndef KEYPAD_LCD_WIRE_H
#define KEYPAD_LCD_WIRE_H

#include "../src/shared/bytes.h"

enum { LCD_KEY_SIZE = 118, LCD_PACKET_SIZE = 4095, LCD_PIXELS_SIZE = 118 * 118 * 4 };

static inline void lcd_put16(uint8_t *out, unsigned value) {
    out[0] = (uint8_t)(value >> 8u);
    out[1] = (uint8_t)value;
}

static inline uint32_t keypad_lcd_packet(uint8_t out[LCD_PACKET_SIZE], unsigned key,
                                         const uint8_t *jpeg, uint32_t size,
                                         uint32_t offset, unsigned part) {
    if (!out || !jpeg || key < 1u || key > 9u || !size || size > 65535u ||
        offset >= size || part < 1u || part > 17u) return 0u;
    uint32_t expected = part == 1u ? 0u : 4075u + (part - 2u) * 4090u;
    if (offset != expected) return 0u;
    uint32_t header = part == 1u ? 20u : 5u, count = size - offset;
    if (count > LCD_PACKET_SIZE - header) count = LCD_PACKET_SIZE - header;
    badger_bytes_zero(out, LCD_PACKET_SIZE);
    out[0] = 0x14u; out[1] = 0xffu; out[2] = 2u; out[3] = 0x2bu;
    out[4] = (uint8_t)(part | 0x20u | (part == 1u ? 0x80u : 0u) |
                      (offset + count == size ? 0x40u : 0u));
    if (part == 1u) {
        lcd_put16(out + 5u, 0x0100u);
        lcd_put16(out + 7u, 0x0100u);
        lcd_put16(out + 9u, 23u + (key - 1u) % 3u * 158u);
        lcd_put16(out + 11u, 6u + (key - 1u) / 3u * 158u);
        lcd_put16(out + 13u, LCD_KEY_SIZE);
        lcd_put16(out + 15u, LCD_KEY_SIZE);
        lcd_put16(out + 18u, size);
    }
    badger_bytes_copy(out + header, jpeg + offset, count);
    return count;
}

#endif

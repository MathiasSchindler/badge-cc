#ifndef KEYPAD_LCD_H
#define KEYPAD_LCD_H

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { LCD_KEY_SIZE = 118, LCD_PACKET_SIZE = 4095, LCD_PIXELS_SIZE = 118 * 118 * 4 };

static void lcd_rectangle(uint8_t *pixels, unsigned x, unsigned y,
                           unsigned width, unsigned height) {
    for (unsigned row = y; row < y + height; ++row) {
        for (unsigned col = x; col < x + width; ++col) {
            uint8_t *p = pixels + (row * LCD_KEY_SIZE + col) * 4;
            p[0] = p[1] = p[2] = p[3] = 255;
        }
    }
}

static int lcd_pixels(unsigned key, uint8_t *pixels) {
    static const uint8_t colors[9][3] = {
        { 25, 65, 155 }, { 10, 105, 50 }, { 155, 30, 30 },
        { 105, 35, 145 }, { 10, 105, 115 }, { 155, 65, 10 },
        { 90, 60, 130 }, { 30, 90, 100 }, { 125, 35, 75 }
    };
    static const uint8_t digits[9] = { 0x06, 0x5b, 0x4f, 0x66, 0x6d,
                                       0x7d, 0x07, 0x7f, 0x6f };
    if (key < 1 || key > 9 || !pixels) {
        fputs("Invalid LCD pattern key/buffer.\n", stderr);
        return 0;
    }
    for (unsigned y = 0; y < LCD_KEY_SIZE; ++y) {
        for (unsigned x = 0; x < LCD_KEY_SIZE; ++x) {
            uint8_t *p = pixels + (y * LCD_KEY_SIZE + x) * 4;
            unsigned shade = ((x / 7 + y / 7) & 1) ? 18 : 0;
            for (unsigned c = 0; c < 3; ++c) p[c] = (uint8_t)(colors[key - 1][c] + shade);
            p[3] = 255;
        }
    }
    lcd_rectangle(pixels, 2, 2, 114, 2);
    lcd_rectangle(pixels, 2, 114, 114, 2);
    lcd_rectangle(pixels, 2, 2, 2, 114);
    lcd_rectangle(pixels, 114, 2, 2, 114);
    const unsigned segments[7][4] = {
        { 39, 20, 40, 8 }, { 79, 28, 8, 27 }, { 79, 63, 8, 27 },
        { 39, 90, 40, 8 }, { 31, 63, 8, 27 }, { 31, 28, 8, 27 },
        { 39, 55, 40, 8 }
    };
    for (unsigned bit = 0; bit < 7; ++bit) {
        if (digits[key - 1] & (1u << bit)) {
            lcd_rectangle(pixels, segments[bit][0], segments[bit][1],
                           segments[bit][2], segments[bit][3]);
        }
    }
    return 1;
}

static CFDataRef lcd_jpeg(unsigned key) {
    uint8_t pixels[LCD_PIXELS_SIZE];
    if (!lcd_pixels(key, pixels)) return NULL;
    CGColorSpaceRef colors = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, pixels, sizeof(pixels), NULL);
    CGImageRef image = colors && provider ?
        CGImageCreate(LCD_KEY_SIZE, LCD_KEY_SIZE, 8, 32, LCD_KEY_SIZE * 4, colors,
                      kCGImageAlphaNoneSkipLast, provider, NULL, false,
                      kCGRenderingIntentDefault) : NULL;
    CFMutableDataRef data = CFDataCreateMutable(NULL, 0);
    CGImageDestinationRef encoder = data ?
        CGImageDestinationCreateWithData(data, CFSTR("public.jpeg"), 1, NULL) : NULL;
    double quality = 0.85;
    CFNumberRef q = CFNumberCreate(NULL, kCFNumberDoubleType, &quality);
    const void *keys[] = { kCGImageDestinationLossyCompressionQuality };
    const void *values[] = { q };
    CFDictionaryRef options = q ? CFDictionaryCreate(NULL, keys, values, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks) : NULL;
    int ok = 0;
    if (image && encoder && options) {
        CGImageDestinationAddImage(encoder, image, options);
        ok = CGImageDestinationFinalize(encoder) &&
             CFDataGetLength(data) > 0 && CFDataGetLength(data) <= 65535;
    }
    if (options) CFRelease(options);
    if (q) CFRelease(q);
    if (encoder) CFRelease(encoder);
    if (image) CGImageRelease(image);
    if (provider) CGDataProviderRelease(provider);
    if (colors) CGColorSpaceRelease(colors);
    if (!ok) {
        fprintf(stderr, "Cannot encode LCD key %u as a JPEG of 1..65535 bytes.\n", key);
        if (data) CFRelease(data);
        return NULL;
    }
    return data;
}

static void lcd_put16(uint8_t *out, unsigned value) {
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)value;
}

static size_t lcd_packet(uint8_t out[LCD_PACKET_SIZE], unsigned key,
                         const uint8_t *jpeg, size_t size, size_t offset,
                         unsigned part) {
    if (!out || !jpeg || key < 1 || key > 9 || !size || size > 65535 ||
        offset >= size || part < 1 || part > 17) {
        fputs("Invalid LCD packet geometry, size or sequence.\n", stderr);
        return 0;
    }
    size_t expected_offset = part == 1 ? 0 : 4075u + (size_t)(part - 2) * 4090u;
    if (offset != expected_offset) {
        fputs("Invalid LCD fragment offset.\n", stderr);
        return 0;
    }
    size_t header = part == 1 ? 20 : 5;
    size_t count = size - offset;
    if (count > LCD_PACKET_SIZE - header) count = LCD_PACKET_SIZE - header;
    memset(out, 0, LCD_PACKET_SIZE);
    out[0] = 0x14;
    out[1] = 0xff;
    out[2] = 0x02;
    out[3] = 0x2b;
    out[4] = (uint8_t)(part | 0x20 | (part == 1 ? 0x80 : 0) |
                        (offset + count == size ? 0x40 : 0));
    if (part == 1) {
        lcd_put16(out + 5, 0x0100);
        lcd_put16(out + 7, 0x0100);
        lcd_put16(out + 9, 23 + (key - 1) % 3 * 158);
        lcd_put16(out + 11, 6 + (key - 1) / 3 * 158);
        lcd_put16(out + 13, LCD_KEY_SIZE);
        lcd_put16(out + 15, LCD_KEY_SIZE);
        lcd_put16(out + 18, (unsigned)size);
    }
    memcpy(out + header, jpeg + offset, count);
    return count;
}

#endif

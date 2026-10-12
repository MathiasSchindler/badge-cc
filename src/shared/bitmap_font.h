#ifndef UNIVERSE26_BITMAP_FONT_H
#define UNIVERSE26_BITMAP_FONT_H

#include <stdint.h>
#include "../platform/badger/graphics.h"

typedef enum {
    BADGER_FONT_OK,
    BADGER_FONT_ARGUMENT,
    BADGER_FONT_UTF8,
    BADGER_FONT_MISSING,
    BADGER_FONT_ASSET,
    BADGER_FONT_OVERFLOW
} badger_font_result_t;

typedef struct {
    uint32_t codepoint;
    uint32_t offset;
    int16_t advance, left, top;
    uint16_t width, height;
} badger_font_glyph_t;

typedef struct {
    const badger_font_glyph_t *glyphs;
    const uint8_t *coverage;
    uint32_t coverage_bytes;
    uint16_t glyph_count, pixel_size, line_height, ascent;
    uint8_t coverage_bits, bold;
} badger_font_t;

typedef struct {
    int32_t x, y, width, height;
} badger_font_clip_t;

typedef struct {
    int32_t advance, left, right, top, bottom;
} badger_font_metrics_t;

/* Exact (value + 127) / 255 for products in the range 0..65025. */
#if defined(__GNUC__)
static inline
#else
static
#endif
uint32_t badger_font_alpha_round(uint32_t value) {
    value += 128u;
    return (value + (value >> 8u)) >> 8u;
}

badger_font_result_t badger_utf8_next(const char *text, uint32_t length,
                                      uint32_t *used, uint32_t *codepoint);
int badger_font_codepoint_supported(uint32_t codepoint);
const badger_font_glyph_t *badger_font_glyph(const badger_font_t *font, uint32_t codepoint);
badger_font_result_t badger_font_measure(const badger_font_t *font, const char *text,
                                        uint32_t length, badger_font_metrics_t *metrics);
uint16_t badger_rgb565_blend(uint16_t background, uint16_t foreground, uint8_t alpha);
badger_font_result_t badger_font_draw(badger_canvas_t *canvas, const badger_font_t *font,
    const char *text, uint32_t length, int32_t x, int32_t baseline,
    uint16_t color, uint8_t opacity, badger_font_clip_t clip, int crisp);
badger_font_result_t badger_font_mask(uint8_t *mask, uint32_t stride, uint32_t rows,
    const badger_font_t *font, const char *text, uint32_t length,
    int32_t x, int32_t baseline, int crisp);
const char *badger_font_result_name(badger_font_result_t result);

#endif

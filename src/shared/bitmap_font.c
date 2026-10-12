#include "bitmap_font.h"

badger_font_result_t badger_utf8_next(const char *text, uint32_t length,
                                      uint32_t *used, uint32_t *codepoint) {
    if (text == 0 || used == 0 || codepoint == 0 || length == 0u) return BADGER_FONT_ARGUMENT;
    const uint8_t *bytes = (const uint8_t *)text;
    uint32_t count, value, minimum;
    if (bytes[0] < 0x80u) { count = 1u; value = bytes[0]; minimum = 0u; }
    else if (bytes[0] >= 0xc2u && bytes[0] <= 0xdfu) {
        count = 2u; value = bytes[0] & 31u; minimum = 0x80u;
    } else if (bytes[0] >= 0xe0u && bytes[0] <= 0xefu) {
        count = 3u; value = bytes[0] & 15u; minimum = 0x800u;
    } else if (bytes[0] >= 0xf0u && bytes[0] <= 0xf4u) {
        count = 4u; value = bytes[0] & 7u; minimum = 0x10000u;
    } else return BADGER_FONT_UTF8;
    if (count > length) return BADGER_FONT_UTF8;
    for (uint32_t i = 1u; i < count; ++i) {
        if ((bytes[i] & 0xc0u) != 0x80u) return BADGER_FONT_UTF8;
        value = (value << 6u) | (bytes[i] & 63u);
    }
    if (value < minimum || value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu) ||
        value == 0u) return BADGER_FONT_UTF8;
    *used = count;
    *codepoint = value;
    return BADGER_FONT_OK;
}

int badger_font_codepoint_supported(uint32_t codepoint) {
    if ((codepoint >= 0x20u && codepoint <= 0x7eu) ||
        (codepoint >= 0xa0u && codepoint <= 0xffu)) return 1;
    return codepoint == 0x152u || codepoint == 0x153u || codepoint == 0x160u ||
        codepoint == 0x161u || codepoint == 0x178u || codepoint == 0x17du ||
        codepoint == 0x17eu || codepoint == 0x2013u || codepoint == 0x2014u ||
        codepoint == 0x2018u || codepoint == 0x2019u || codepoint == 0x201cu ||
        codepoint == 0x201du || codepoint == 0x2026u || codepoint == 0x20acu;
}

const badger_font_glyph_t *badger_font_glyph(const badger_font_t *font, uint32_t codepoint) {
    if (font == 0 || font->glyphs == 0) return 0;
    uint32_t low = 0u, high = font->glyph_count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        uint32_t candidate = font->glyphs[middle].codepoint;
        if (candidate < codepoint) low = middle + 1u;
        else high = middle;
    }
    return low < font->glyph_count && font->glyphs[low].codepoint == codepoint ?
        &font->glyphs[low] : 0;
}

static badger_font_result_t glyph_valid(const badger_font_t *font, const badger_font_glyph_t *glyph) {
    if (font->coverage_bits != 4u && font->coverage_bits != 8u) return BADGER_FONT_ASSET;
    uint32_t pixels = (uint32_t)glyph->width * glyph->height;
    uint32_t bytes = font->coverage_bits == 8u ? pixels : (pixels + 1u) / 2u;
    if (glyph->advance < 0 || glyph->offset > font->coverage_bytes ||
        bytes > font->coverage_bytes - glyph->offset || (bytes != 0u && font->coverage == 0)) {
        return BADGER_FONT_ASSET;
    }
    return BADGER_FONT_OK;
}

badger_font_result_t badger_font_measure(const badger_font_t *font, const char *text,
                                        uint32_t length, badger_font_metrics_t *metrics) {
    if (font == 0 || text == 0 || metrics == 0) return BADGER_FONT_ARGUMENT;
    metrics->advance = metrics->left = metrics->right = metrics->top = metrics->bottom = 0;
    uint32_t offset = 0u;
    while (offset < length) {
        uint32_t used, codepoint;
        badger_font_result_t result = badger_utf8_next(text + offset, length - offset, &used, &codepoint);
        if (result != BADGER_FONT_OK) return result;
        offset += used;
        if (!badger_font_codepoint_supported(codepoint)) return BADGER_FONT_MISSING;
        if (codepoint == 0xadu) continue;
        const badger_font_glyph_t *glyph = badger_font_glyph(font, codepoint);
        if (glyph == 0) return BADGER_FONT_MISSING;
        result = glyph_valid(font, glyph);
        if (result != BADGER_FONT_OK) return result;
        int64_t left = (int64_t)metrics->advance + glyph->left;
        int64_t right = left + glyph->width;
        if (right > 0x7fffffffu || (int64_t)metrics->advance + glyph->advance > 0x7fffffffu) {
            return BADGER_FONT_OVERFLOW;
        }
        if (glyph->width != 0u && glyph->height != 0u) {
            if (left < metrics->left) metrics->left = (int32_t)left;
            if (right > metrics->right) metrics->right = (int32_t)right;
            if (-glyph->top < metrics->top) metrics->top = -glyph->top;
            if ((int32_t)glyph->height - glyph->top > metrics->bottom) {
                metrics->bottom = (int32_t)glyph->height - glyph->top;
            }
        }
        metrics->advance += glyph->advance;
    }
    if (metrics->advance > metrics->right) metrics->right = metrics->advance;
    return BADGER_FONT_OK;
}

uint16_t badger_rgb565_blend(uint16_t background, uint16_t foreground, uint8_t alpha) {
    if (alpha == 0u) return background;
    if (alpha == 255u) return foreground;
    uint32_t inverse = 255u - alpha;
    uint32_t red = badger_font_alpha_round((background >> 11u) * inverse + (foreground >> 11u) * alpha);
    uint32_t green = badger_font_alpha_round(((background >> 5u) & 63u) * inverse +
                                           ((foreground >> 5u) & 63u) * alpha);
    uint32_t blue = badger_font_alpha_round((background & 31u) * inverse + (foreground & 31u) * alpha);
    return (uint16_t)((red << 11u) | (green << 5u) | blue);
}

static uint8_t coverage(const badger_font_t *font, const badger_font_glyph_t *glyph,
                         uint32_t index, int crisp) {
    uint32_t value;
    if (font->coverage_bits == 8u) value = font->coverage[glyph->offset + index];
    else {
        uint32_t packed = font->coverage[glyph->offset + index / 2u];
        value = ((index & 1u) ? packed & 15u : packed >> 4u) * 17u;
    }
    if (crisp) value = 255u - badger_font_alpha_round((255u - value) * (255u - value));
    return (uint8_t)value;
}

static badger_font_result_t draw(badger_canvas_t *canvas, uint8_t *mask,
    uint32_t stride, uint32_t rows, const badger_font_t *font,
    const char *text, uint32_t length, int32_t x, int32_t baseline,
    uint16_t color, uint8_t opacity, badger_font_clip_t clip, int crisp) {
    badger_font_metrics_t metrics;
    badger_font_result_t result = badger_font_measure(font, text, length, &metrics);
    if (result != BADGER_FONT_OK) return result;
    int64_t right = (int64_t)clip.x + clip.width, bottom = (int64_t)clip.y + clip.height;
    uint32_t offset = 0u;
    int64_t pen = x;
    while (offset < length) {
        uint32_t used, codepoint;
        result = badger_utf8_next(text + offset, length - offset, &used, &codepoint);
        if (result != BADGER_FONT_OK) return result;
        offset += used;
        if (codepoint == 0xadu) continue;
        const badger_font_glyph_t *glyph = badger_font_glyph(font, codepoint);
        int64_t gx = pen + glyph->left, gy = (int64_t)baseline - glyph->top;
        int64_t left = gx > clip.x ? gx : clip.x;
        int64_t top = gy > clip.y ? gy : clip.y;
        int64_t end_x = gx + glyph->width < right ? gx + glyph->width : right;
        int64_t end_y = gy + glyph->height < bottom ? gy + glyph->height : bottom;
        if (left < 0) left = 0;
        if (top < 0) top = 0;
        if (end_x > stride) end_x = stride;
        if (end_y > rows) end_y = rows;
        if (left < end_x && top < end_y) {
            uint32_t first_column = (uint32_t)(left - gx), last_column = (uint32_t)(end_x - gx);
            uint32_t first_row = (uint32_t)(top - gy), last_row = (uint32_t)(end_y - gy);
            if (mask != 0) {
                for (uint32_t row = first_row; row < last_row; ++row) {
                    uint8_t *destination = mask + (uint32_t)(gy + row) * stride + (uint32_t)left;
                    uint32_t source = row * glyph->width + first_column;
                    for (uint32_t column = first_column; column < last_column;
                         ++column, ++source, ++destination) {
                        uint8_t alpha = coverage(font, glyph, source, crisp);
                        if (alpha > *destination) *destination = alpha;
                    }
                }
            } else {
                for (uint32_t column = first_column; column < last_column; ++column) {
                    uint16_t *destination = canvas->pixels +
                        (uint32_t)(gx + column) * BADGER_GFX_HEIGHT + (uint32_t)top;
                    uint32_t source = first_row * glyph->width + column;
                    for (uint32_t row = first_row; row < last_row;
                         ++row, source += glyph->width, ++destination) {
                        uint8_t alpha = coverage(font, glyph, source, crisp);
                        if (alpha == 0u) continue;
                        if (opacity != 255u) alpha = (uint8_t)badger_font_alpha_round((uint32_t)alpha * opacity);
                        if (alpha != 0u) *destination = badger_rgb565_blend(*destination, color, alpha);
                    }
                }
            }
        }
        pen += glyph->advance;
    }
    return BADGER_FONT_OK;
}

badger_font_result_t badger_font_draw(badger_canvas_t *canvas, const badger_font_t *font,
    const char *text, uint32_t length, int32_t x, int32_t baseline,
    uint16_t color, uint8_t opacity, badger_font_clip_t clip, int crisp) {
    if (canvas == 0 || clip.width <= 0 || clip.height <= 0) return BADGER_FONT_ARGUMENT;
    return draw(canvas, 0, BADGER_GFX_WIDTH, BADGER_GFX_HEIGHT, font, text, length,
                x, baseline, color, opacity, clip, crisp);
}

badger_font_result_t badger_font_mask(uint8_t *mask, uint32_t stride, uint32_t rows,
    const badger_font_t *font, const char *text, uint32_t length,
    int32_t x, int32_t baseline, int crisp) {
    if (mask == 0 || stride == 0u || rows == 0u || stride > BADGER_GFX_WIDTH ||
        rows > BADGER_GFX_HEIGHT) return BADGER_FONT_ARGUMENT;
    badger_font_clip_t clip = {0, 0, (int32_t)stride, (int32_t)rows};
    return draw(0, mask, stride, rows, font, text, length, x, baseline, 0u, 255u, clip, crisp);
}

const char *badger_font_result_name(badger_font_result_t result) {
    switch (result) {
        case BADGER_FONT_OK: return "ok";
        case BADGER_FONT_ARGUMENT: return "invalid font-rendering argument";
        case BADGER_FONT_UTF8: return "invalid UTF-8";
        case BADGER_FONT_MISSING: return "unsupported or missing glyph";
        case BADGER_FONT_ASSET: return "invalid font asset";
        case BADGER_FONT_OVERFLOW: return "text measurement overflow";
    }
    return "unknown font error";
}

#include "graphics.h"

/* Project-owned 5x7 glyphs shared in shape with the existing IMU demo. */
static const uint8_t letters[26][7] = {
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
};
static const uint8_t digits[10][7] = {
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
};

uint8_t badger_gfx_glyph_row(char c, uint32_t row) {
    if (row >= 7u) return 0u;
    if (c >= 'A' && c <= 'Z') return letters[(uint32_t)(c - 'A')][row];
    if (c >= '0' && c <= '9') return digits[(uint32_t)(c - '0')][row];
    if (c == '-') return row == 3u ? 14u : 0u;
    if (c == '+') return row == 3u ? 14u : (row > 0u && row < 6u ? 4u : 0u);
    if (c == '.') return row == 6u ? 4u : 0u;
    if (c == ':') return row == 2u || row == 5u ? 4u : 0u;
    if (c == '/') return (uint8_t)(1u << (row * 4u / 6u));
    if (c == '%') return row == 0u || row == 6u ? 17u :
                         (uint8_t)(1u << (row * 4u / 6u));
    return 0u;
}

void badger_gfx_clear(badger_canvas_t *canvas, uint16_t color) {
    for (uint32_t i = 0; i < BADGER_GFX_WIDTH * BADGER_GFX_HEIGHT; ++i) {
        canvas->pixels[i] = color;
    }
}

void badger_gfx_rect(badger_canvas_t *canvas, int32_t x, int32_t y,
                     int32_t width, int32_t height, uint16_t color) {
    if (width <= 0 || height <= 0) return;
    int32_t right = x + width;
    int32_t bottom = y + height;
    if (right > BADGER_GFX_WIDTH) right = BADGER_GFX_WIDTH;
    if (bottom > BADGER_GFX_HEIGHT) bottom = BADGER_GFX_HEIGHT;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    for (int32_t column = x; column < right; ++column) {
        for (int32_t row = y; row < bottom; ++row) {
            canvas->pixels[column * BADGER_GFX_HEIGHT + row] = color;
        }
    }
}

void badger_gfx_text(badger_canvas_t *canvas, int32_t x, int32_t y,
                     const char *text, uint32_t scale, uint16_t color) {
    if (scale == 0u || scale > 4u) return;
    for (uint32_t i = 0; text[i] != '\0' && x < BADGER_GFX_WIDTH; ++i) {
        for (uint32_t row = 0; row < 7u; ++row) {
            uint8_t bits = badger_gfx_glyph_row(text[i], row);
            for (uint32_t column = 0; column < 5u; ++column) {
                if ((bits & (1u << (4u - column))) != 0u) {
                    badger_gfx_rect(canvas, x + (int32_t)(column * scale),
                                    y + (int32_t)(row * scale),
                                    (int32_t)scale, (int32_t)scale, color);
                }
            }
        }
        x += (int32_t)(6u * scale);
    }
}

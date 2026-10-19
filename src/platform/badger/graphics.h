#ifndef UNIVERSE26_GRAPHICS_H
#define UNIVERSE26_GRAPHICS_H

#include <stdint.h>

#define BADGER_GFX_WIDTH 320
#define BADGER_GFX_HEIGHT 240

typedef struct {
    uint16_t pixels[BADGER_GFX_WIDTH * BADGER_GFX_HEIGHT];
} badger_canvas_t;

uint8_t badger_gfx_glyph_row(char c, uint32_t row);
void badger_gfx_clear(badger_canvas_t *canvas, uint16_t color);
void badger_gfx_rect(badger_canvas_t *canvas, int32_t x, int32_t y,
                     int32_t width, int32_t height, uint16_t color);
void badger_gfx_text(badger_canvas_t *canvas, int32_t x, int32_t y,
                     const char *text, uint32_t scale, uint16_t color);

#endif

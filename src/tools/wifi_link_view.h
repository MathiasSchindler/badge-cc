#ifndef UNIVERSE26_WIFI_LINK_VIEW_H
#define UNIVERSE26_WIFI_LINK_VIEW_H

#include "../platform/badger/graphics.h"
#include "../platform/badger/display.h"
#include "../shared/bytes.h"

typedef struct {
    int32_t x, y;
    uint32_t scale;
    uint16_t color;
    char text[40];
} wifi_view_text_t;

typedef struct {
    wifi_view_text_t text[24];
    uint32_t count, phase, offset, command, character, tile;
    uint8_t first_frame;
} wifi_view_t;

static int wifi_view_text(wifi_view_t *view, int32_t x, int32_t y,
                          const char *text, uint32_t scale, uint16_t color) {
    if (view->count == 24u || scale == 0u || scale > 4u) return 0;
    uint32_t length = 0u;
    while (text[length] != '\0' && length < 39u) ++length;
    if (text[length] != '\0') return 0;
    wifi_view_text_t *item = &view->text[view->count++];
    item->x = x;
    item->y = y;
    item->scale = scale;
    item->color = color;
    for (uint32_t i = 0u; i <= length; ++i) item->text[i] = text[i];
    return 1;
}

/* One bounded unit: 256 clear pixels, one glyph, or one 16x16 tile. */
static void wifi_view_step(wifi_view_t *view, badger_canvas_t *canvas,
                           badger_canvas_t *previous) {
    if (view->phase == 1u) {
        badger_pixels_fill(canvas->pixels + view->offset, 0x0863u, 256u);
        view->offset += 256u;
        if (view->offset == BADGER_GFX_WIDTH * BADGER_GFX_HEIGHT) view->phase = 2u;
    } else if (view->phase == 2u) {
        if (view->command == view->count) {
            view->phase = 3u;
            return;
        }
        const wifi_view_text_t *item = &view->text[view->command];
        int32_t x = item->x + (int32_t)(view->character * 6u * item->scale);
        if (item->text[view->character] == '\0' || x >= BADGER_GFX_WIDTH) {
            ++view->command;
            view->character = 0u;
            return;
        }
        char glyph[2] = {item->text[view->character++], '\0'};
        badger_gfx_text(canvas, x, item->y, glyph, item->scale, item->color);
    } else if (view->phase == 3u) {
        uint32_t x = view->tile / 15u * 16u, y = view->tile % 15u * 16u;
        uint8_t dirty = view->first_frame || previous == 0;
        for (uint32_t dx = 0u; dx < 16u && !dirty; ++dx) {
            uint32_t i = (x + dx) * BADGER_GFX_HEIGHT + y;
            dirty = !badger_bytes_equal((const uint8_t *)(canvas->pixels + i),
                                        (const uint8_t *)(previous->pixels + i), 32u);
        }
        if (dirty) {
            badger_display_begin_region(x, y, 16u, 16u);
            for (uint32_t dx = 0u; dx < 16u; ++dx) {
                uint32_t i = (x + dx) * BADGER_GFX_HEIGHT + y;
                badger_display_write_pixels(canvas->pixels + i, 16u);
                if (previous != 0) {
                    badger_bytes_copy((uint8_t *)(previous->pixels + i),
                                      (const uint8_t *)(canvas->pixels + i), 32u);
                }
            }
            badger_display_end_update();
        }
        if (++view->tile == 300u) {
            if (view->first_frame) badger_display_end();
            view->first_frame = 0u;
            view->phase = 0u;
        }
    }
}

static void wifi_view_finish(wifi_view_t *view, badger_canvas_t *canvas,
                             badger_canvas_t *previous) {
    while (view->phase != 0u) wifi_view_step(view, canvas, previous);
}

#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../tools/wifi_link_view.h"

static badger_canvas_t canvas, previous, reference, lcd;
static uint32_t region_x, region_y, region_pixels, writes, regions, enabled;

void badger_display_begin_region(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    assert(width == 16u && height == 16u && x + width <= 320u && y + height <= 240u);
    region_x = x;
    region_y = y;
    region_pixels = 0u;
    ++regions;
}

void badger_display_write_pixels(const uint16_t *pixels, uint32_t count) {
    assert(count == 16u && region_pixels + count <= 256u);
    for (uint32_t i = 0u; i < count; ++i) {
        uint32_t x = region_x + region_pixels / 16u;
        uint32_t y = region_y + region_pixels % 16u;
        lcd.pixels[x * BADGER_GFX_HEIGHT + y] = pixels[i];
        ++region_pixels;
        ++writes;
    }
}

void badger_display_end_update(void) { assert(region_pixels == 256u); }
void badger_display_end(void) { ++enabled; }

static void begin(wifi_view_t *view) {
    view->count = view->offset = view->command = view->character = view->tile = 0u;
    view->phase = 1u;
    badger_gfx_clear(&reference, 0x0863u);
}

static void text(wifi_view_t *view, int32_t x, int32_t y, const char *value,
                  uint32_t scale, uint16_t color) {
    assert(wifi_view_text(view, x, y, value, scale, color));
    badger_gfx_text(&reference, x, y, value, scale, color);
}

static void finish(wifi_view_t *view, badger_canvas_t *last) {
    uint32_t steps = 0u;
    while (view->phase != 0u) {
        uint32_t before = writes, offset = view->offset;
        wifi_view_step(view, &canvas, last);
        assert(writes - before <= 256u);
        assert(view->offset - offset <= 256u);
        assert(++steps < 2000u);
    }
    assert(memcmp(&canvas, &reference, sizeof(canvas)) == 0);
    assert(memcmp(&lcd, &reference, sizeof(lcd)) == 0);
    if (last != 0) assert(memcmp(last, &reference, sizeof(reference)) == 0);
}

static void frame(wifi_view_t *view) {
    begin(view);
    char local[] = "COPIED 123";
    text(view, 8, 8, local, 3u, 0xffffu);
    memset(local, 'Z', sizeof(local) - 1u);
    text(view, -5, -3, "CLIPPED", 2u, 0x9cf3u);
    text(view, 314, 100, "RIGHT", 4u, 0xf9c7u);
    text(view, 8, 238, "BOTTOM", 1u, 0x5ecbu);
    text(view, 8, 120, "", 1u, 0xffffu);
}

int main(void) {
    wifi_view_t view = {.first_frame = 1u};
    memset(&previous, 0xa5, sizeof(previous));
    frame(&view);
    finish(&view, &previous);
    assert(regions == 300u && writes == 76800u && enabled == 1u && !view.first_frame);
    uint32_t before = writes;
    frame(&view);
    finish(&view, &previous);
    assert(writes == before && regions == 300u && enabled == 1u);
    frame(&view);
    text(&view, 319, 239, "T", 1u, 0xffffu);
    finish(&view, &previous);
    assert(writes == before + 256u && regions == 301u);
    assert(region_x == 304u && region_y == 224u);
    frame(&view);
    before = writes;
    finish(&view, 0);
    assert(writes == before + 76800u && enabled == 1u);
    begin(&view);
    assert(!wifi_view_text(&view, 0, 0, "X", 0u, 0u));
    assert(!wifi_view_text(&view, 0, 0, "X", 5u, 0u));
    char long_text[41];
    memset(long_text, 'A', 40u);
    long_text[40] = '\0';
    assert(!wifi_view_text(&view, 0, 0, long_text, 1u, 0u) && view.count == 0u);
    long_text[39] = '\0';
    assert(wifi_view_text(&view, 0, 0, long_text, 1u, 0u));
    for (uint32_t i = 1u; i < 24u; ++i) assert(wifi_view_text(&view, 0, 0, "", 1u, 0u));
    assert(!wifi_view_text(&view, 0, 0, "", 1u, 0u) && view.count == 24u);
    view.phase = 0u;
    before = writes;
    wifi_view_step(&view, &canvas, &previous);
    assert(writes == before);
    frame(&view);
    wifi_view_step(&view, &canvas, &previous);
    wifi_view_finish(&view, &canvas, &previous);
    assert(view.phase == 0u && memcmp(&canvas, &reference, sizeof(canvas)) == 0 &&
           memcmp(&lcd, &reference, sizeof(lcd)) == 0);
    puts("wifi_link_view: bounded steps, reference pixels, copied strings, tile diffs and limits PASS");
    return 0;
}

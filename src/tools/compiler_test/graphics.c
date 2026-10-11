#include "suite.h"
#include "../diagnostics_logic.h"

void compiler_graphics(uint32_t seed, compiler_result_t *out, badger_canvas_t *canvas,
                        void (*service)(void *), void *context) {
    badger_gfx_clear(canvas, 0x0863u);
    badger_gfx_rect(canvas, -7, -5, 24, 19, 0xffffu);
    badger_gfx_rect(canvas, 310, 230, 24, 19, 0xf800u);
    badger_gfx_rect(canvas, 32, 32, 80, 32, (uint16_t)seed);
    badger_gfx_text(canvas, 8, 80, "COMPILER TEST 2026", 2u, 0xffffu);
    badger_gfx_text(canvas, -4, 136, "CLIPPED", 1u, 0x07e0u);
    badger_gfx_rect(canvas, 8, 176, 96, 32, 0x07e0u);
    badger_gfx_rect(canvas, 112, 176, 96, 32, 0x001fu);
    badger_gfx_rect(canvas, 216, 176, 96, 32, 0xf81fu);
    out->cases = BADGER_GFX_WIDTH * BADGER_GFX_HEIGHT;
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0u; i < out->cases; i += COMPILER_GRAPHICS_STRIDE) {
        if ((i & 511u) == 0u && service != 0) service(context);
        uint16_t pixel = canvas->pixels[i];
        uint8_t bytes[2] = {(uint8_t)pixel, (uint8_t)(pixel >> 8u)};
        crc = diag_crc32_update(crc, bytes, 2u);
        out->values[1] += pixel;
        if (pixel == 0xffffu) ++out->values[2];
        if (pixel == 0x07e0u) ++out->values[3];
    }
    out->values[0] = ~crc;
    out->values[4] = canvas->pixels[0];
    out->values[5] = canvas->pixels[out->cases - 1u];
}

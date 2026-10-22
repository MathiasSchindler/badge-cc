#ifndef CLEAN_MESSENGER_SCREEN_H
#define CLEAN_MESSENGER_SCREEN_H

#include "../../src/platform/badger/graphics.h"
#include "../../src/platform/badger/display.h"

static inline void msg_display_columns(const badger_canvas_t *canvas,
                                      uint32_t first, uint32_t columns,
                                      int (*service)(void *)) {
    badger_display_begin_region(first,0u,columns,BADGER_GFX_HEIGHT);
    /* The shared canvas and LCD stream both run down a column before advancing x. */
    for (uint32_t x=first; x<first+columns; ++x) {
        badger_display_write_pixels(canvas->pixels+x*BADGER_GFX_HEIGHT,BADGER_GFX_HEIGHT);
        if (service) (void)service(0);
    }
}

#endif

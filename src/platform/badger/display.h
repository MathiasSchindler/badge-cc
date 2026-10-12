#ifndef UNIVERSE26_DISPLAY_H
#define UNIVERSE26_DISPLAY_H

#include <stdint.h>

#define BADGER_DISPLAY_WIDTH 320u
#define BADGER_DISPLAY_HEIGHT 240u

/* Both RP2350 entry points wait 150,000 inherited timer ticks after reset and
   sleep-out. A running ROM-inherited timer is required. */
void badger_display_init(void);
/* A zero callback result cancels initialization; the caller handles recovery. */
int badger_display_init_serviced(int (*service)(void *), void *context);
void badger_display_begin(void);
void badger_display_begin_region(uint32_t x, uint32_t y,
                                 uint32_t width, uint32_t height);
void badger_display_write(uint16_t pixel);
void badger_display_write_pixels(const uint16_t *pixels, uint32_t count);
void badger_display_end_update(void);
void badger_display_end(void);
void badger_display_set_tearing(int enabled);
/* Require a running timer. Wake leaves output/backlight off until display_end. */
void badger_display_sleep(void);
void badger_display_wake(void);

#endif

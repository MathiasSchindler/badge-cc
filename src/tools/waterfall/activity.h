#ifndef WATERFALL_ACTIVITY_H
#define WATERFALL_ACTIVITY_H

#include "../../platform/badger/graphics.h"
#include "../../../clean-rx/src/frame_limits.h"

#define ACT_CHANNELS 11u
#define ACT_ROWS 152u
#define ACT_X 12u
#define ACT_Y 54u
#define ACT_CELL 27u
#define ACT_DWELL_US 150000u
#define ACT_FAST_DWELL_US 50000u
#define ACT_MAGIC 0x31414657u

typedef struct {
    uint32_t changed_at;
    uint8_t raw, stable, fast;
} activity_button_t;

typedef struct {
    uint16_t history[ACT_ROWS][ACT_CHANNELS];
    uint16_t counts[ACT_CHANNELS];
    uint16_t rates[ACT_CHANNELS];
    uint32_t dwell_us, channel_updates, sweep_us;
    uint32_t sweeps, frames, rejected, rendered;
    uint32_t sample_length;
    uint8_t sample[256];
} activity_t;

uint16_t activity_color(uint32_t count);
uint16_t activity_rate(uint16_t count, uint32_t elapsed_us);
void activity_button(activity_button_t *button, uint8_t pressed, uint32_t now_us);
void activity_progress(badger_canvas_t *canvas, const activity_t *a, uint32_t completed);
int activity_frame(activity_t *a, const uint8_t *packet, uint32_t length, uint32_t channel);
void activity_commit(activity_t *a);
void activity_render(badger_canvas_t *canvas, const activity_t *a, const char *status);

#endif

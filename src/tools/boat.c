#include "display.h"
#include "graphics.h"
#include "home.h"
#include "diagnostics_logic.h"
#include "i2c_bitbang.h"
#include "lsm6ds3.h"
#include "../shared/boat_physics.h"
#include "../shared/boat_input.h"
#include "../shared/frame_diff.h"
#include "../shared/bytes.h"
#ifdef BOAT_BREAKING
#include "../shared/boat_fluid.h"
#endif
#include "../arch/rp2350/nametag_clock.h"
#include "../arch/rp2350/bootrom_reboot.h"
#ifdef BOAT_PROFILE
#include "../shared/boat_measure.h"
#endif

#ifndef BOAT_TIME_US
#define BOAT_TIME_US() (*(volatile uint32_t *)0x400b000cu)
#endif
#ifndef BOAT_HALT
#define BOAT_HALT() __asm__ volatile ("wfi")
#endif

static badger_frame_t frame;
static badger_frame_presenter_t presenter;
#ifdef BOAT_BREAKING
static boat_fluid_t sea;
volatile uint32_t boat_fluid_step_us, boat_fluid_frame_us;
#else
static boat_physics_t sea;
#endif
static boat_input_t input;
static diag_buttons_t buttons;
static int display_ready, display_on;
volatile int32_t boat_error;
volatile uint32_t boat_steps, boat_frames, boat_skips;
volatile uint32_t boat_input_status;

static uint32_t tick(void) { return BOAT_TIME_US(); }

static void number(int32_t x, int32_t y, uint32_t value) {
    char text[11];
    uint32_t at = 10u;
    text[at] = 0;
    do { text[--at] = (char)('0' + value % 10u); value /= 10u; } while (value != 0u);
    badger_gfx_text(&frame.canvas, x, y, text + at, 1u, 0xffffu);
}

static int writer(void *context, const badger_canvas_t *source,
    const badger_frame_region_t *region) {
    (void)context;
#ifdef BOAT_PROFILE
    uint32_t started = tick();
    if (!boat_profile_service()) return 0;
#endif
    badger_display_begin_region(region->x, region->y, region->width, region->height);
    for (uint32_t x = region->x; x < region->x + region->width; ++x) {
        badger_display_write_pixels(source->pixels + x * 240u + region->y, region->height);
#ifdef BOAT_PROFILE
        if ((x - region->x) % 16u == 15u && !boat_profile_service()) {
            badger_display_end_update();
            return 0;
        }
#endif
    }
    badger_display_end_update();
#ifdef BOAT_PROFILE
    boat_profile_record(BM_WRITER, tick() - started);
#endif
    return 1;
}

static void fail(int32_t error, const char *message) {
    boat_error = error;
#ifdef BOAT_PROFILE
    boat_profile_abort(error);
#endif
    if (display_ready) {
        badger_gfx_clear(&frame.canvas, 0x0801u);
        badger_gfx_text(&frame.canvas, 8, 30, "BOAT ERROR", 2u, 0xffffu);
        badger_gfx_text(&frame.canvas, 8, 54, message, 1u, 0xffffu);
        badger_gfx_text(&frame.canvas, 8, 70, "RETURNING TO BOOTSEL", 1u, 0xffffu);
        const badger_frame_region_t region = {0u,0u,320u,240u};
        writer(0, &frame.canvas, &region);
        badger_display_end();
    }
    while (1) BOAT_HALT();
}

static void draw_background(void) {
#ifdef BOAT_BREAKING
    boat_fluid_render(&sea, &frame.canvas);
#else
    for (int32_t x = 0; x < 320; ++x) {
        int32_t surface = boat_surface(&sea, x * BOAT_ONE) / BOAT_ONE;
        uint16_t *column = frame.canvas.pixels + x * 240;
        badger_pixels_fill(column, 0x18edu, (uint32_t)surface);
        badger_pixels_fill(column + surface, 0x2395u, (uint32_t)(BOAT_BOTTOM - surface));
        if (surface < BOAT_BOTTOM) column[surface] = 0x9f5fu;
        if (surface + 1 < BOAT_BOTTOM) column[surface + 1] = 0x9f5fu;
        badger_pixels_fill(column + BOAT_BOTTOM, 0x0843u, 12u);
    }
#endif
}

static void draw(boat_input_result_t status) {
    draw_background();
    int32_t bx = sea.x / BOAT_ONE, by = sea.y / BOAT_ONE;
    for (int32_t dx = -16; dx <= 16; ++dx) {
        int32_t bottom = by + dx * sea.slope / BOAT_ONE;
        int32_t taper = dx < -10 ? -10 - dx : dx > 10 ? dx - 10 : 0;
        badger_gfx_rect(&frame.canvas, bx + dx, bottom - 10, 1, 10 - taper, 0xb304u);
        badger_gfx_rect(&frame.canvas, bx + dx, bottom - 11, 1, 2, 0xfda4u);
    }
    for (int32_t h = 11; h < 37; ++h) {
        int32_t mast_x = bx + h * sea.slope / BOAT_ONE;
        badger_gfx_rect(&frame.canvas, mast_x, by - h, 2, 1, 0xffffu);
        if (h > 17 && h < 34)
            badger_gfx_rect(&frame.canvas, mast_x + 2, by - h, (34 - h) / 2 + 1, 1, 0xf778u);
    }
    badger_gfx_rect(&frame.canvas, 0, BOAT_BOTTOM, 320, 12, 0x0843u);
#ifdef BOAT_BREAKING
    badger_gfx_text(&frame.canvas, 4, 3, "BREAKING WATER EXPERIMENT - HOME RESET", 1u, 0xffffu);
#elif defined(BOAT_PROFILE)
    badger_gfx_text(&frame.canvas, 4, 3, "BOAT 10 MIN SCRIPT - IMU MONITOR", 1u, 0xffffu);
#else
    badger_gfx_text(&frame.canvas, 4, 3, "BOAT  HOME RESTART", 1u, 0xffffu);
#endif
    const char *prompt = status == BOAT_INPUT_CALIBRATING ? "HOLD STILL - CALIBRATING GYRO" :
        status == BOAT_INPUT_POSE ? "LIFT SCREEN A LITTLE - AVOID STEEP ROLL" :
        status == BOAT_INPUT_UNRELIABLE ? "MOTION UNRELIABLE - HOLD STILL" : 0;
    if (prompt != 0) badger_gfx_text(&frame.canvas, 4, 15, prompt, 1u, 0xfda4u);
    if (status == BOAT_INPUT_CALIBRATING) {
        const char *reason = input.calibration_reason == BOAT_CAL_ACCEL ? "ACCEL OUT OF RANGE" :
            input.calibration_reason == BOAT_CAL_RATE ? "GYRO RATE TOO HIGH" :
            input.calibration_reason == BOAT_CAL_MOVING ? "SAMPLES VARY - KEEP STILL" :
            input.calibration_reason == BOAT_CAL_GAP ? "SENSOR GAP TOO LONG" : "COLLECTING STABLE SAMPLES";
        badger_gfx_text(&frame.canvas, 4, 27, reason, 1u, 0xffffu);
        badger_gfx_text(&frame.canvas, 4, 39, "COUNT       RATE       GAP MS", 1u, 0xffffu);
        number(40, 39, input.samples);
        number(112, 39, (uint32_t)input.calibration_rate);
        number(190, 39, input.sample_gap / 1000u);
    }
#ifdef BOAT_BREAKING
    if (status != BOAT_INPUT_CALIBRATING) {
        badger_gfx_text(&frame.canvas, 4, 39, "STEP US       FRAME US       SKIPS", 1u, 0xffffu);
        number(4, 51, boat_fluid_step_us);
        number(88, 51, boat_fluid_frame_us);
        number(172, 51, boat_skips);
    }
#endif
}

void bare_main(void) {
    int32_t error = badger_schedule_bootsel(8000u);
    if (error != 0) fail(error, "RECOVERY");
    error = nametag_clock_reference();
    if (error != 0) fail(error, "CLOCK");
    if (!badger_display_init_serviced(0, 0)) fail(-4101, "DISPLAY");
    display_ready = 1;
    badger_home_init();
    badger_i2c_result_t bus = badger_i2c_init_pins(18u, 19u);
    if (bus != BADGER_I2C_OK) fail((int32_t)bus, "I2C");
    uint8_t identity;
    lsm6ds3_result_t sensor = lsm6ds3_init(&identity);
    if (sensor != LSM6DS3_OK) fail((int32_t)sensor, "IMU");
    lsm6ds3_sample_t sample;
    if (!lsm6ds3_read(&sample)) fail(-4102, "IMU READ");
    boat_input_init(&input);
    boat_physics_init(&sea);
    uint32_t physics_at = tick(), render_at = physics_at, renewed = physics_at;
    uint32_t sample_at = physics_at + BOAT_STEP_US;
    int32_t gx, gy;
    boat_input_result_t status = boat_input_update(&input, &sample, physics_at, &gx, &gy);
    boat_input_status = (uint32_t)status;
    while (1) {
        uint32_t now = tick();
#ifdef BOAT_PROFILE
        if (boat_profile_begin(now)) {
            boat_physics_init(&sea);
            boat_steps = boat_frames = boat_skips = 0u;
            physics_at = render_at = now;
            sample_at = now + BOAT_STEP_US;
        }
        if (boat_profile_poll(now)) return;
#endif
        uint8_t pressed = diag_buttons_update(&buttons,
            badger_home_pressed() ? DIAG_HOME : 0u, now);
        if (pressed & DIAG_HOME) boat_physics_init(&sea);
        if ((int32_t)(now - sample_at) >= 0) {
#ifdef BOAT_PROFILE
            uint32_t sensor_started = tick();
#endif
            if (!lsm6ds3_read(&sample)) fail(-4102, "IMU READ");
            now = tick();
#ifdef BOAT_PROFILE
            boat_profile_record(BM_SENSOR, now - sensor_started);
            uint32_t input_started = tick();
#endif
            status = boat_input_update(&input, &sample, now, &gx, &gy);
            boat_input_status = (uint32_t)status;
            if (status == BOAT_INPUT_ARGUMENT) fail(-4104, "INPUT");
#ifdef BOAT_PROFILE
            boat_profile_record(BM_INPUT, tick() - input_started);
            boat_profile_sample((uint32_t)status, input.sample_gap, input.calibrated, input.bias);
#endif
            sample_at = now + BOAT_STEP_US;
        }
        if ((int32_t)(now - physics_at) >= 0) {
            uint32_t catchup = 0u;
            do {
#ifdef BOAT_PROFILE
                boat_profile_gravity(boat_steps, &gx, &gy);
                if (boat_steps < BOAT_MEASURE_STEPS) {
                    uint32_t step_started = tick();
#else
                if (status == BOAT_INPUT_READY) {
#endif
#ifdef BOAT_BREAKING
                    uint32_t step_started = tick();
#endif
                    if (!boat_physics_step(&sea, gx, gy)) fail(-4103, "PHYSICS");
#ifdef BOAT_BREAKING
                    now = tick();
                    boat_fluid_step_us = now - step_started;
#endif
#ifdef BOAT_PROFILE
                    boat_profile_record(BM_PHYSICS, tick() - step_started);
                    if (!boat_profile_check()) fail(-4110, "WATER INVARIANT");
#endif
                    ++boat_steps;
                }
                physics_at += BOAT_STEP_US;
                ++catchup;
            } while ((int32_t)(now - physics_at) >= 0 &&
#ifdef BOAT_BREAKING
                catchup < 1u);
#else
                catchup < 5u);
#endif
            if ((int32_t)(now - physics_at) >= 0) {
                uint32_t missed = (now - physics_at) / BOAT_STEP_US + 1u;
                boat_skips += missed;
                physics_at += missed * BOAT_STEP_US;
            }
            if (now - renewed >= 500000u) {
                error = badger_schedule_bootsel(8000u);
                if (error != 0) fail(error, "RECOVERY");
                renewed = now;
            }
        }
        if ((int32_t)(now - render_at) >= 0) {
#if defined(BOAT_PROFILE) || defined(BOAT_BREAKING)
            uint32_t frame_started = tick();
#endif
            draw(status);
#ifdef BOAT_PROFILE
            uint32_t rendered = tick();
#endif
            badger_frame_stats_t stats;
            badger_frame_result_t result = badger_frame_present(&presenter, &frame, writer, 0, &stats);
            if (result != BADGER_FRAME_OK) fail((int32_t)result, "PRESENT");
            if (!display_on) { badger_display_end(); display_on = 1; }
            ++boat_frames;
            render_at += 33333u;
            uint32_t finished = tick();
#ifdef BOAT_BREAKING
            boat_fluid_frame_us = finished - frame_started;
#endif
            uint32_t skipped = 0u;
            if ((int32_t)(finished - render_at) >= 0) {
                skipped = (finished - render_at) / 33333u + 1u;
                render_at += skipped * 33333u;
            }
#ifdef BOAT_PROFILE
            boat_profile_frame(frame_started, rendered, finished,
                stats.pixels, stats.regions, stats.full, skipped);
#endif
        }
    }
}

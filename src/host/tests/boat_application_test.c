#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "../../shared/boat_physics.h"
#ifdef BOAT_BREAKING_TEST
#include "../../shared/boat_fluid.h"
#endif

static jmp_buf stopped;
static uint32_t now_us, runtime, pixels, updates, display_enables, renewals, reads;
static uint32_t rx, ry, rw, rh, region_pixels;
static int fault, home;
static uint32_t ready_frames, calibration_frames, pose_frames, unreliable_frames;
static uint32_t resets;
static int stalled;
#ifdef BOAT_PROFILE_TEST
static uint32_t disconnections, recovery_requests;
#endif

#ifdef BOAT_BREAKING_TEST
static void test_sea_init(boat_fluid_t *state) {
    ++resets;
    boat_fluid_init(state);
}
#else
static void test_sea_init(boat_physics_t *state) {
    ++resets;
    boat_physics_init(state);
}
#endif
static uint16_t panel[320u * 240u];

static uint32_t test_tick(void) {
    now_us += 100u;
    if (fault == 15 && !stalled && now_us >= 950000u) {
        now_us += 250000u;
        stalled = 1;
    }
    if (now_us >= runtime) longjmp(stopped, 1);
    return now_us;
}

#define BOAT_TIME_US() test_tick()
#ifdef BOAT_BREAKING_TEST
#define BOAT_HALT() longjmp(stopped, 2)
#define boat_fluid_init test_sea_init
#include "../../tools/boat-breaking.c"
#undef boat_fluid_init
#else
#define boat_physics_init test_sea_init
#if defined(BOAT_PROFILE_TEST)
#define BOAT_PROFILE_SOF() ((now_us / 1000u) & 2047u)
#define BOAT_PROFILE_IDLE() longjmp(stopped, 2)
#include "../../tools/boat_profile.c"
#else
#define BOAT_HALT() longjmp(stopped, 2)
#include "../../tools/boat.c"
#endif
#endif
#undef boat_physics_init
#undef boat_physics_step

int32_t badger_schedule_bootsel(uint32_t delay_ms) {
#ifdef BOAT_PROFILE_TEST
    if (delay_ms == 10u) { ++recovery_requests; return 0; }
#endif
    assert(delay_ms == 8000u);
    ++renewals;
    return fault == 1 ? -1001 : 0;
}

int32_t nametag_clock_reference(void) { return fault == 2 ? -3001 : 0; }
int badger_display_init_serviced(int (*service)(void *), void *context) {
    assert(service == 0 && context == 0);
    return fault != 3;
}
void badger_display_begin_region(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    assert(width > 0u && height > 0u && x + width <= 320u && y + height <= 240u);
    rx = x; ry = y; rw = width; rh = height; region_pixels = 0u;
}
void badger_display_write_pixels(const uint16_t *source, uint32_t count) {
    assert(count == rh && region_pixels < rw * rh);
    assert(source == frame.canvas.pixels + (rx + region_pixels / rh) * 240u + ry);
    if (fault == 14) now_us += count / 2u;
    memcpy(panel + (rx + region_pixels / rh) * 240u + ry, source, count * sizeof(*source));
    region_pixels += count;
    pixels += count;
}
void badger_display_end_update(void) {
    assert(region_pixels == rw * rh);
    ++updates;
    switch (boat_input_status) {
    case BOAT_INPUT_READY: ++ready_frames; break;
    case BOAT_INPUT_CALIBRATING: ++calibration_frames; break;
    case BOAT_INPUT_POSE: ++pose_frames; break;
    case BOAT_INPUT_UNRELIABLE: ++unreliable_frames; break;
    default: assert(0);
    }
}
void badger_display_end(void) { ++display_enables; }
void badger_home_init(void) {}
int badger_home_pressed(void) {
    return home || (fault == 13 && now_us >= 1000000u && now_us < 1050000u);
}
badger_i2c_result_t badger_i2c_init_pins(uint32_t sda, uint32_t scl) {
    assert(sda == 18u && scl == 19u);
    return fault == 4 ? BADGER_I2C_LINES_NOT_HIGH : BADGER_I2C_OK;
}
lsm6ds3_result_t lsm6ds3_init(uint8_t *identity) {
    *identity = 0x6au;
    return fault == 5 ? LSM6DS3_ID_MISMATCH : LSM6DS3_OK;
}
int lsm6ds3_read(lsm6ds3_sample_t *sample) {
    ++reads;
    if (fault == 6 || (fault == 7 && reads > 10u)) return 0;
    memset(sample, 0, sizeof(*sample));
    if (fault == 12 && now_us >= 900000u) {
        sample->accel_x = 32767; sample->accel_y = 32767; sample->accel_z = -32768;
    } else if (fault == 8) sample->accel_z = -16384;
    else if (fault == 9) {
        sample->accel_x = -32768;
        sample->accel_y = -32768;
    } else if (fault == 10) {
        sample->accel_y = 8192;
        sample->accel_z = -14189;
        sample->accel_x = now_us > 300000u ? 1200 : 0;
    } else if (fault == 11) {
        sample->accel_y = 16384;
        sample->gyro_z = 2500;
    } else if (fault == 16) {
        sample->accel_y = 16384;
        sample->gyro_z = 1200;
    } else {
        sample->accel_y = 16384;
        sample->accel_x = now_us > 300000u ? 2400 : 0;
    }
    return 1;
}

static void reset(int failure) {
    fault = failure; home = stalled = 0;
    now_us = pixels = updates = display_enables = renewals = reads = 0u;
    runtime = 1500000u;
    display_ready = display_on = 0;
    boat_error = 0;
    boat_steps = boat_frames = boat_skips = boat_input_status = 0u;
    ready_frames = calibration_frames = pose_frames = unreliable_frames = 0u;
    resets = 0u;
    memset(&buttons, 0, sizeof(buttons));
    memset(&presenter, 0, sizeof(presenter));
    memset(panel, 0, sizeof(panel));
#ifdef BOAT_PROFILE_TEST
    memset(&usb, 0, sizeof(usb));
    usb.configured = 1u;
    usb.m2_generation = generation = 1u;
    begun = begin_at = last_frame = sof_previous = last_sof_at = 0u;
    reboot_at = disconnections = recovery_requests = 0u;
    reboot_wait = 0;
    boat_measure_init(words());
#endif
}

#ifndef BOAT_BREAKING_TEST
static void background_equivalence(void) {
    static badger_canvas_t expected;
    for (uint32_t layout = 0u; layout < 3u; ++layout) {
        boat_physics_init(&sea);
        for (uint32_t i = 0u; i < BOAT_COLUMNS; ++i)
            if (layout == 1u) sea.depth[i] = i % 2u ? BOAT_MAX_DEPTH : 0;
            else if (layout == 2u) sea.depth[i] = (int32_t)(i * 7919u % (BOAT_MAX_DEPTH + 1u));
        draw_background();
        badger_gfx_clear(&expected, 0x18edu);
        for (int32_t x = 0; x < 320; ++x) {
            int32_t surface = boat_surface(&sea, x * BOAT_ONE) / BOAT_ONE;
            badger_gfx_rect(&expected, x, surface, 1, BOAT_BOTTOM - surface, 0x2395u);
            badger_gfx_rect(&expected, x, surface, 1, 2, 0x9f5fu);
        }
        badger_gfx_rect(&expected, 0, BOAT_BOTTOM, 320, 12, 0x0843u);
        assert(memcmp(&expected, &frame.canvas, sizeof(expected)) == 0);
    }
}

#endif
#ifdef BOAT_PROFILE_TEST
int32_t live_usb_hw_poll(live_usb_control_t *control) { assert(control == &usb); return 0; }
int32_t live_usb_hw_init(void) { return 0; }
void live_usb_hw_disconnect(void) { ++disconnections; }
void nametag_clock_snapshot(uint32_t *out) {
    for (uint32_t i = 0u; i < 12u; ++i) out[i] = 100u + i;
}
int32_t badger_rom_chip_id(uint32_t id[2]) { id[0] = id[1] = 0u; return 0; }
void live_usb_control_init(live_usb_control_t *control, uint32_t build, const char serial[17]) {
    (void)control; (void)build; (void)serial;
}

int main(void) {
    background_equivalence();
    uint32_t record[NAMETAG_M2_WORDS];
    boat_measure_init(record);
    boat_measure_record(record, BM_FRAME_WORK, 999u);
    boat_measure_record(record, BM_FRAME_WORK, 1000u);
    boat_measure_record(record, BM_FRAME_WORK, 33000u);
    boat_measure_record(record, BM_FRAME_WORK, 65000u);
    assert(record[BM_METRIC_BASE + BM_FRAME_WORK * 4u] == 4u);
    assert(record[BM_METRIC_BASE + BM_FRAME_WORK * 4u + 1u] == 99999u);
    assert(record[BM_METRIC_BASE + BM_FRAME_WORK * 4u + 2u] == 999u);
    assert(record[BM_METRIC_BASE + BM_FRAME_WORK * 4u + 3u] == 65000u);
    assert(record[BM_HISTOGRAM_BASE] == 1u && record[BM_HISTOGRAM_BASE + 1u] == 1u &&
        record[BM_HISTOGRAM_BASE + 33u] == 1u && record[BM_HISTOGRAM_BASE + 63u] == 1u);
    reset(14);
    runtime = 610000000u;
    int stopped_by = setjmp(stopped);
    if (!stopped_by) boat_application();
    assert(stopped_by == 0 && boat_error == 0 && usb.m2_valid);
    assert(words()[BM_STAGE] == 3u && words()[BM_ERROR] == 0u);
    assert(boat_steps == 60000u && words()[BM_STEPS] == boat_steps && boat_skips == 0u);
    assert(words()[BM_DURATION] > 599000000u && words()[BM_DURATION] < 601000000u);
    assert(words()[BM_VOLUME_ERRORS] == 0u && words()[BM_VOLUME] == BOAT_COLUMNS * BOAT_REST_DEPTH);
    assert(words()[BM_METRIC_BASE + BM_PHYSICS * 4u] == boat_steps);
    assert(words()[BM_METRIC_BASE + BM_RENDER * 4u] == words()[BM_FRAMES]);
    assert(words()[BM_METRIC_BASE + BM_PRESENT * 4u] == words()[BM_FRAMES]);
    uint32_t histogram_count = 0u;
    for (uint32_t i = 0u; i < BOAT_MEASURE_BINS; ++i) histogram_count += words()[BM_HISTOGRAM_BASE + i];
    assert(histogram_count == words()[BM_FRAMES]);
    printf("boat profile: frame gaps %u, calibrated %u, maximum sample gap %u us\n",
        words()[BM_FRAME_GAPS], words()[BM_CALIBRATED], words()[BM_SAMPLE_GAP_MAX]);
    assert(words()[BM_CALIBRATED] == 1u);
    assert(words()[BM_MIN_DEPTH] < BOAT_REST_DEPTH && words()[BM_MAX_DEPTH] > BOAT_REST_DEPTH);
    assert(memcmp(panel, &frame.canvas, sizeof(panel)) == 0);
    ++sea.depth[0];
    assert(!boat_profile_check() && words()[BM_VOLUME_ERRORS] == 1u);
    usb.configured = 0u;
    assert(!boat_profile_service() && (int32_t)words()[BM_ERROR] == -4121);
    printf("boat profile: 60000 scripted steps, %u frames, exact volume, measured slow transfers, histogram counts and explicit invalidation PASS\n",
        words()[BM_FRAMES]);
    usb.reboot_requested = 1u;
    boat_profile_halt();
    assert(reboot_wait && disconnections == 0u && recovery_requests == 0u);
    now_us += 10000u;
    int halted = setjmp(stopped);
    if (!halted) boat_profile_halt();
    assert(halted == 2 && disconnections == 1u && recovery_requests == 1u);
}
#elif defined(BOAT_BREAKING_TEST)
int main(void) {
    reset(0);
    int stopped_by = setjmp(stopped);
    if (!stopped_by) bare_main();
    assert(stopped_by == 1 && boat_error == 0 && sea.steps > 30u && input.calibrated);
    assert(display_enables == 1u && boat_fluid_step_us > 0u && boat_fluid_frame_us > 0u);
    assert(memcmp(panel, &frame.canvas, sizeof(panel)) == 0);
    for (int failure = 1; failure <= 7; ++failure) {
        reset(failure);
        stopped_by = setjmp(stopped);
        if (!stopped_by) bare_main();
        assert(stopped_by == 2 && boat_error != 0);
        if (display_ready) assert(memcmp(panel, &frame.canvas, sizeof(panel)) == 0);
    }
    reset(8);
    stopped_by = setjmp(stopped);
    if (!stopped_by) bare_main();
    assert(stopped_by == 1 && boat_error == 0 && sea.steps == 0u);
    reset(13);
    stopped_by = setjmp(stopped);
    if (!stopped_by) bare_main();
    assert(stopped_by == 1 && boat_error == 0 && resets == 2u && input.calibrated);
    reset(15);
    stopped_by = setjmp(stopped);
    if (!stopped_by) bare_main();
    assert(stopped_by == 1 && boat_error == 0 && boat_skips >= 20u);
    assert(memcmp(panel, &frame.canvas, sizeof(panel)) == 0);
    puts("boat breaking app: calibrated input, exact panel, HOME, unsupported freeze, stalls and explicit recovery errors PASS");
}
#else
int main(void) {
    background_equivalence();
    reset(0);
    int stopped_by = setjmp(stopped);
    if (!stopped_by) bare_main();
    assert(stopped_by == 1 && boat_error == 0);
    assert(display_enables == 1u && renewals >= 3u);
    assert(boat_frames > 30u && boat_steps >= 40u && boat_steps < 100u && reads >= 140u);
    assert(boat_input_status == BOAT_INPUT_READY && input.calibrated);
    reset(14);
    stopped_by = setjmp(stopped);
    if (!stopped_by) bare_main();
    assert(stopped_by == 1 && boat_error == 0 && renewals >= 3u && boat_steps > 30u);
    assert(boat_input_status == BOAT_INPUT_READY && input.calibrated);
    assert(memcmp(panel, &frame.canvas, sizeof(panel)) == 0);
    reset(15);
    stopped_by = setjmp(stopped);
    if (!stopped_by) bare_main();
    assert(stopped_by == 1 && boat_error == 0 && boat_skips >= 20u && stalled);
    assert(unreliable_frames > 0u && boat_input_status == BOAT_INPUT_READY);
    assert(memcmp(panel, &frame.canvas, sizeof(panel)) == 0);
    assert(ready_frames > 0u && calibration_frames > 0u && pose_frames > 0u && resets == 1u);
    assert(memcmp(panel, &frame.canvas, sizeof(panel)) == 0);
    for (int failure = 1; failure <= 7; ++failure) {
        reset(failure);
        stopped_by = setjmp(stopped);
        if (!stopped_by) bare_main();
        assert(stopped_by == 2 && boat_error != 0);
        assert(display_enables > 0u || !display_ready);
        if (display_ready) assert(memcmp(panel, &frame.canvas, sizeof(panel)) == 0);
    }
    for (int pose = 8; pose <= 9; ++pose) {
        reset(pose);
        stopped_by = setjmp(stopped);
        if (!stopped_by) bare_main();
        assert(stopped_by == 1 && boat_error == 0);
        boat_physics_t rest;
        boat_physics_init(&rest);
        assert(memcmp(&sea, &rest, sizeof(rest)) == 0);
        assert(boat_frames > 30u);
        assert(boat_steps == 0u);
        assert(boat_input_status == (pose == 8 ? BOAT_INPUT_POSE : BOAT_INPUT_CALIBRATING));
    }
    reset(10);
    stopped_by = setjmp(stopped);
    if (!stopped_by) bare_main();
    assert(stopped_by == 1 && boat_error == 0 && boat_steps >= 40u);
    assert(boat_input_status == BOAT_INPUT_READY && input.calibrated);
    assert(memcmp(panel, &frame.canvas, sizeof(panel)) == 0);
    reset(11);
    stopped_by = setjmp(stopped);
    if (!stopped_by) bare_main();
    assert(stopped_by == 1 && boat_error == 0 && boat_steps == 0u && renewals >= 3u);
    assert(boat_input_status == BOAT_INPUT_CALIBRATING);
    reset(16);
    stopped_by = setjmp(stopped);
    if (!stopped_by) bare_main();
    assert(stopped_by == 1 && boat_error == 0 && boat_steps > 60u);
    assert(boat_input_status == BOAT_INPUT_READY && input.bias[2] == 1200);
    reset(12);
    stopped_by = setjmp(stopped);
    if (!stopped_by) bare_main();
    assert(stopped_by == 1 && boat_error == 0 && renewals >= 3u);
    assert(boat_input_status == BOAT_INPUT_UNRELIABLE && unreliable_frames > 0u);
    assert(memcmp(panel, &frame.canvas, sizeof(panel)) == 0);
    reset(13);
    stopped_by = setjmp(stopped);
    if (!stopped_by) bare_main();
    assert(stopped_by == 1 && boat_error == 0 && resets == 2u);
    assert(boat_input_status == BOAT_INPUT_READY && input.calibrated);
    reset(0);
    boat_physics_init(&sea);
    for (uint32_t i = 0u; i < BOAT_COLUMNS; ++i)
        sea.depth[i] = i % 2u ? BOAT_MAX_DEPTH : 0;
    sea.x = 20 * BOAT_ONE; sea.y = 16 * BOAT_ONE; sea.slope = -128;
    draw(BOAT_INPUT_READY);
    sea.x = 300 * BOAT_ONE; sea.y = BOAT_BOTTOM * BOAT_ONE; sea.slope = 128;
    draw(BOAT_INPUT_POSE);
    puts("boat application: complete panel updates, fixed steps, error display/recovery, unsupported/extreme IMU freeze and render clipping PASS");
}
#endif

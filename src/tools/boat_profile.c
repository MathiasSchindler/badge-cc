#include "../shared/boat_measure.h"
#include "../usb/live_usb_hw.h"
#include "boat_profile_build.h"

#ifndef BOAT_PROFILE_SOF
#define BOAT_PROFILE_SOF() (*(volatile uint32_t *)0x50110048u & 2047u)
#endif
#ifndef BOAT_PROFILE_IDLE
#define BOAT_PROFILE_IDLE() __asm__ volatile ("wfi")
#endif
#define BOAT_HALT() boat_profile_halt()
#define bare_main boat_application
#include "boat.c"
#undef bare_main

static live_usb_control_t usb;
static uint32_t generation, begun, begin_at, last_frame, sof_previous, last_sof_at;
static uint32_t reboot_at;
static int reboot_wait;
static uint32_t *words(void) { return usb.m2_words; }
static uint32_t sof(void) { return BOAT_PROFILE_SOF(); }

int boat_profile_service(void) {
    if (live_usb_hw_poll(&usb) != 0) { boat_profile_abort(-4120); return 0; }
    if (!usb.configured || usb.reboot_requested || usb.m2_generation != generation) {
        boat_profile_abort(-4121); return 0;
    }
    uint32_t frame_number = sof(), now = tick();
    uint32_t gap = (frame_number - sof_previous) & 2047u;
    if (gap != 0u) {
        if (begun) {
            words()[BM_SOF] += gap;
            if (gap > words()[BM_SOF_MAX_GAP]) words()[BM_SOF_MAX_GAP] = gap;
        }
        sof_previous = frame_number;
        last_sof_at = now;
    } else if (now - last_sof_at > 100000u) {
        boat_profile_abort(-4122); return 0;
    }
    return 1;
}

int boat_profile_begin(uint32_t now) {
    if (begun || !display_on) return 0;
    begun = 1u;
    begin_at = last_sof_at = now;
    sof_previous = sof();
    words()[BM_STAGE] = 2u;
    words()[BM_INITIAL_VOLUME] = BOAT_COLUMNS * BOAT_REST_DEPTH;
    nametag_clock_snapshot(words() + BM_CLOCK);
    return 1;
}

void boat_profile_record(uint32_t metric, uint32_t us) {
    if (begun) boat_measure_record(words(), metric, us);
}

void boat_profile_sample(uint32_t status, uint32_t gap, int calibrated, const int32_t bias[3]) {
    if (!begun) return;
    ++words()[BM_SAMPLES];
    ++words()[BM_STATUS_BASE + status];
    if (gap > words()[BM_SAMPLE_GAP_MAX]) words()[BM_SAMPLE_GAP_MAX] = gap;
    words()[BM_CALIBRATED] = (uint32_t)calibrated;
    for (uint32_t i = 0u; i < 3u; ++i) words()[BM_BIAS_X + i] = (uint32_t)bias[i];
}

void boat_profile_gravity(uint32_t step, int32_t *gx, int32_t *gy) {
    /* 12 seconds per cycle: slow sweep +/-80, then four seconds upright.
       This exercises sloshing independently of how the bench badge rests. */
    uint32_t phase = step % 1200u;
    if (phase < 200u) *gx = (int32_t)phase * 80 / 200;
    else if (phase < 600u) *gx = 80 - (int32_t)(phase - 200u) * 160 / 400;
    else if (phase < 800u) *gx = -80 + (int32_t)(phase - 600u) * 80 / 200;
    else *gx = 0;
    *gy = *gx == 0 ? 256 : 240;
}

int boat_profile_check(void) {
    if (!begun) return 1;
    uint32_t volume = 0u;
    for (uint32_t i = 0u; i < BOAT_COLUMNS; ++i) {
        if (sea.depth[i] < 0 || sea.depth[i] > BOAT_MAX_DEPTH) return 0;
        uint32_t depth = (uint32_t)sea.depth[i];
        volume += depth;
        if (depth < words()[BM_MIN_DEPTH]) words()[BM_MIN_DEPTH] = depth;
        if (depth > words()[BM_MAX_DEPTH]) words()[BM_MAX_DEPTH] = depth;
    }
    words()[BM_VOLUME] = volume;
    if (volume != words()[BM_INITIAL_VOLUME]) {
        ++words()[BM_VOLUME_ERRORS]; return 0;
    }
    if (sea.flow[0] != 0 || sea.flow[BOAT_COLUMNS] != 0 ||
        sea.x < 20 * BOAT_ONE || sea.x > 300 * BOAT_ONE ||
        sea.y < 16 * BOAT_ONE || sea.y > BOAT_BOTTOM * BOAT_ONE ||
        sea.slope < -128 || sea.slope > 128) return 0;
    uint32_t x = (uint32_t)sea.x, y = (uint32_t)sea.y;
    if (x < words()[BM_MIN_X]) words()[BM_MIN_X] = x;
    if (x > words()[BM_MAX_X]) words()[BM_MAX_X] = x;
    if (y < words()[BM_MIN_Y]) words()[BM_MIN_Y] = y;
    if (y > words()[BM_MAX_Y]) words()[BM_MAX_Y] = y;
    uint32_t slope = (uint32_t)(sea.slope < 0 ? -sea.slope : sea.slope);
    if (slope > words()[BM_MAX_SLOPE]) words()[BM_MAX_SLOPE] = slope;
    return 1;
}

void boat_profile_frame(uint32_t started, uint32_t rendered, uint32_t finished,
    uint32_t pixels, uint32_t regions, uint32_t full, uint32_t skipped) {
    if (!begun) { words()[BM_FIRST_FRAME_US] = finished - started; return; }
    boat_profile_record(BM_RENDER, rendered - started);
    boat_profile_record(BM_PRESENT, finished - rendered);
    boat_profile_record(BM_FRAME_WORK, finished - started);
    if (finished - started > 25000u) ++words()[BM_FRAME_OVER_25MS];
    if (last_frame != 0u) {
        boat_profile_record(BM_FRAME_INTERVAL, finished - last_frame);
        if (finished - last_frame > 40000u) ++words()[BM_FRAME_GAPS];
    }
    last_frame = finished;
    ++words()[BM_FRAMES];
    words()[BM_PIXELS] += pixels;
    words()[BM_REGIONS] += regions;
    words()[BM_FULL_FRAMES] += full;
    words()[BM_RENDER_SKIPS] += skipped;
}

void boat_profile_abort(int32_t error) {
    if (words()[BM_ERROR] == 0u) words()[BM_ERROR] = (uint32_t)error;
    words()[BM_STAGE] = 3u;
    usb.m2_valid = 1u;
}

void boat_profile_halt(void) {
    int broken = live_usb_hw_poll(&usb) != 0;
    if (usb.reboot_requested && !reboot_wait) {
        reboot_wait = 1;
        reboot_at = tick();
    }
    if (broken || (reboot_wait && tick() - reboot_at >= 10000u)) {
        live_usb_hw_disconnect();
        int32_t error = badger_schedule_bootsel(10u);
        if (error != 0) boat_profile_abort(error);
        while (1) BOAT_PROFILE_IDLE();
    }
}

int boat_profile_poll(uint32_t now) {
    if (!boat_profile_service()) fail(-4123, "USB SERVICE");
    if (!begun) return 0;
    words()[BM_DURATION] = now - begin_at;
    words()[BM_STEPS] = boat_steps;
    words()[BM_PHYSICS_SKIPS] = boat_skips;
    if (now - begin_at > 630000000u) fail(-4124, "PROFILE DEADLINE");
    if (boat_steps == BOAT_MEASURE_STEPS) {
        words()[BM_STAGE] = 3u;
        usb.m2_valid = 1u;
        return 1;
    }
    return 0;
}

void bare_main(void) {
    int32_t error = badger_schedule_bootsel(8000u);
    if (error != 0) while (1) BOAT_PROFILE_IDLE();
    uint32_t id[2];
    if (badger_rom_chip_id(id) != 0) while (1) BOAT_PROFILE_IDLE();
    char serial[17], half[9];
    diag_hex(half, id[1], 8u);
    for (uint32_t i = 0u; i < 8u; ++i) serial[i] = half[i];
    diag_hex(half, id[0], 8u);
    for (uint32_t i = 0u; i < 8u; ++i) serial[8u + i] = half[i];
    serial[16] = 0;
    live_usb_control_init(&usb, USB_REPORT_BUILD_ID, serial);
    if (live_usb_hw_init() != 0) while (1) BOAT_PROFILE_IDLE();
    while (!usb.m2_ready) {
        if (live_usb_hw_poll(&usb) != 0 || usb.reboot_requested) boat_profile_halt();
    }
    usb.m2_ready = 0u;
    generation = usb.m2_generation;
    boat_measure_init(words());
    words()[BM_STAGE] = 1u;
    last_sof_at = tick();
    sof_previous = sof();
    boat_application();
    while (1) boat_profile_halt();
}

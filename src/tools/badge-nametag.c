#include "display.h"
#include "graphics.h"
#include "home.h"
#include "diagnostics_logic.h"
#include "../shared/nametag.h"
#include "../shared/frame_diff.h"
#include "../arch/rp2350/nametag_clock.h"
#include "../arch/rp2350/bootrom_reboot.h"
#include "nametag_fonts.h"

static badger_frame_t frame;
static badger_frame_presenter_t presenter;
static badger_nametag_scene_t scene;
static badger_nametag_cache_t cache;
static badger_nametag_clock_us_t clock;
static diag_buttons_t buttons;

volatile int32_t badge_nametag_error;
volatile uint32_t badge_nametag_frames, badge_nametag_skips, badge_nametag_elapsed_ms;
volatile uint32_t badge_nametag_sleeping;
static int display_ready, display_on;

static uint32_t tick(void) {
    return *(volatile uint32_t *)0x400b000cu;
}

static int write_region(void *context, const badger_canvas_t *source,
    const badger_frame_region_t *region) {
    (void)context;
    badger_display_begin_region(region->x, region->y, region->width, region->height);
    for (uint32_t x = region->x; x < region->x + region->width; ++x)
        badger_display_write_pixels(source->pixels + x * BADGER_GFX_HEIGHT + region->y,
            region->height);
    badger_display_end_update();
    return 1;
}

static void fail(int32_t error, const char *message) {
    badge_nametag_error = error;
    if (display_ready) {
        badger_frame_invalidate(&presenter);
        badger_gfx_clear(&frame.canvas, 0u);
        badger_gfx_text(&frame.canvas, 8, 12, "NAMETAG ERROR", 2u, 0xffffu);
        badger_gfx_text(&frame.canvas, 8, 36, message, 1u, 0xffffu);
        badger_gfx_text(&frame.canvas, 8, 52, "RETURNING TO BOOTSEL", 1u, 0xffffu);
        const badger_frame_region_t region = {0u,0u,BADGER_GFX_WIDTH,BADGER_GFX_HEIGHT};
        write_region(0, &frame.canvas, &region);
        badger_display_end();
    }
    while (1) __asm__ volatile ("wfi");
}

static uint8_t home_update(void) {
    return diag_buttons_update(&buttons, badger_home_pressed() ? DIAG_HOME : 0u, tick());
}

static void wait_home_state(int pressed) {
    while (1) {
        home_update();
        if (((buttons.stable & DIAG_HOME) != 0u) == pressed &&
            buttons.candidate == buttons.stable) return;
        if (buttons.candidate == buttons.stable) badger_home_wait_event();
    }
}

static void sleep_until_home(uint32_t *deadline, uint32_t *renewed) {
    *(volatile uint32_t *)0x400db000u = 1u << 30u;
    badge_nametag_sleeping = 1u;
    display_on = 0;
    badger_home_wake_enable();
    badger_display_sleep();
    wait_home_state(0);
    wait_home_state(1);
    badger_home_wake_disable();
    int32_t error = badger_schedule_bootsel(8000u);
    badger_display_wake();
    if (error != 0) fail(error, "RECOVERY");
    badger_frame_invalidate(&presenter);
    clock.last_tick = tick();
    *deadline = *renewed = clock.last_tick;
    badge_nametag_sleeping = 0u;
}

void bare_main(void) {
    int32_t error = badger_schedule_bootsel(8000u);
    if (error != 0) fail(error, "RECOVERY");
    error = nametag_clock_reference();
    if (error != 0) fail(error, "CLOCK");
    badger_home_init();
    if (!badger_display_init_serviced(0, 0)) fail(-4001, "DISPLAY");
    display_ready = 1;
    badger_display_set_tearing(0);
    badger_nametag_error_t profile_error;
    badger_nametag_result_t prepared = badger_nametag_prepare(&scene,
        &badger_nametag_default_profile, &nametag_mono_fonts, BADGER_NAMETAG_B, &profile_error);
    if (prepared != BADGER_NAMETAG_OK) fail((int32_t)prepared, "PROFILE");
    badger_font_result_t rendered = badger_nametag_cache_prepare(&cache, &frame.canvas, &scene, 0);
    if (rendered != BADGER_FONT_OK) fail((int32_t)rendered, "FONT");
    badger_frame_invalidate(&presenter);
    uint32_t deadline = tick(), renewed = deadline;
    uint32_t loop_ms = badger_nametag_loop_ms(&scene);
    while (1) {
        if (home_update() & DIAG_HOME) sleep_until_home(&deadline, &renewed);
        uint32_t now = tick();
        if ((int32_t)(now - deadline) < 0) continue;
        uint32_t elapsed;
        if (badger_nametag_clock_update_us(&clock, now, loop_ms, &elapsed) != BADGER_NAMETAG_OK)
            fail(-4002, "TIMER");
        rendered = badger_nametag_cached_render(&frame.canvas, &cache, elapsed);
        if (rendered != BADGER_FONT_OK) fail((int32_t)rendered, "FONT");
        badger_frame_stats_t stats;
        badger_frame_result_t presented = badger_frame_present(&presenter, &frame,
            write_region, 0, &stats);
        if (presented != BADGER_FRAME_OK) fail((int32_t)presented, "PRESENT");
        if (!display_on) {
            badger_display_end();
            display_on = 1;
        }
        ++badge_nametag_frames;
        badge_nametag_elapsed_ms = elapsed;
        now = tick();
        if (now - renewed >= 500000u) {
            error = badger_schedule_bootsel(8000u);
            if (error != 0) fail(error, "RECOVERY");
            renewed = now;
        }
        deadline += 33333u;
        if ((int32_t)(now - deadline) >= 0) {
            uint32_t missed = (now - deadline) / 33333u + 1u;
            badge_nametag_skips += missed;
            deadline += missed * 33333u;
        }
    }
}

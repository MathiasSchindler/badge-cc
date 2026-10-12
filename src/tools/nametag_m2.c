#include "display.h"
#include "graphics.h"
#include "diagnostics_logic.h"
#include "../shared/nametag.h"
#include "../shared/frame_diff.h"
#include "../shared/nametag_measure.h"
#include "../shared/bytes.h"
#include "../usb/live_usb_hw.h"
#include "../arch/rp2350/bootrom_reboot.h"
#include "../arch/rp2350/nametag_clock.h"
#include "nametag_fonts.h"
#include "nametag_m2_build.h"

#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define LIFETIME_US 180000000u

static badger_frame_t frame;
static badger_frame_presenter_t presenter;
static badger_nametag_scene_t scene;
static badger_nametag_cache_t cache;
static live_usb_control_t usb;
static uint32_t generation, started, renewed, last_sof_tick, sof_total, sof_previous;
static uint32_t te_previous, te_rise, te_edges;
static int qualified;
static uint32_t startup_clocks[12];

static void startup_trace(uint32_t stage, int32_t error) {
    volatile uint32_t *trace = (volatile uint32_t *)0x2007f100u;
    uint32_t snapshot[12];
    nametag_clock_snapshot(snapshot);
    trace[0] = 0u;
    trace[1] = USB_REPORT_BUILD_ID;
    trace[2] = stage;
    trace[3] = (uint32_t)error;
    for (uint32_t i = 0u; i < 12u; ++i) trace[4u + i] = snapshot[i];
    __asm__ volatile ("dsb" ::: "memory");
    trace[0] = 0x3254534du;
}

static uint32_t tick(void) { return REG(0x400b000cu); }
static uint32_t te_level(void) { return (REG(0xd0000004u) >> 21u) & 1u; }
static uint32_t *words(void) { return usb.m2_words; }

static int service(void *context) {
    (void)context;
    uint32_t now = tick();
    if (live_usb_hw_poll(&usb) != 0) { words()[M2_ERROR] = M2_USB_ERROR; return 0; }
    if (!usb.configured || usb.reboot_requested || usb.m2_generation != generation) {
        words()[M2_ERROR] = M2_CANCELLED; return 0;
    }
    uint32_t frame = REG(0x50110048u) & 2047u;
    if (frame != sof_previous) {
        uint32_t step = (frame - sof_previous) & 2047u;
        if (step > words()[M2_SOF_MAX_STEP]) words()[M2_SOF_MAX_STEP] = step;
        if (qualified && now - last_sof_tick >= 2000000u) {
            words()[M2_ERROR] = M2_NO_SOF; return 0;
        }
        sof_total += step;
        sof_previous = frame;
        last_sof_tick = now;
    }
    if (qualified) {
        if (now - started >= LIFETIME_US) { words()[M2_ERROR] = M2_TIMEOUT; return 0; }
        if (now - last_sof_tick >= 100000u) { words()[M2_ERROR] = M2_NO_SOF; return 0; }
        if (now - renewed >= 500000u) {
            int32_t error = badger_schedule_bootsel(8000u);
            if (error != 0) { words()[M2_ERROR] = M2_CLOCK_ERROR; words()[M2_DETAIL] = (uint32_t)error; return 0; }
            renewed = now;
        }
    }
    return 1;
}

static int calibrate(uint32_t *span, uint32_t *frames) {
    uint32_t previous = REG(0x50110048u) & 2047u;
    uint32_t begin = tick(), first = 0u, intervals = 0u;
    int aligned = 0;
    for (uint32_t loops = 0u; loops < 2000000u && tick() - begin < 800000u; ++loops) {
        if (!service(0)) return 0;
        uint32_t frame = REG(0x50110048u) & 2047u, now = tick();
        if (frame != previous) {
            uint32_t step = (frame - previous) & 2047u;
            if (step != 1u) { words()[M2_ERROR] = M2_NO_SOF; return 0; }
            if (!aligned) { first = now; aligned = 1; }
            else intervals += step;
            previous = frame;
            if (intervals == 128u) { *span = now - first; *frames = intervals; return 1; }
        }
    }
    words()[M2_ERROR] = M2_NO_SOF;
    return 0;
}

typedef struct {
    uint32_t writer_ticks, service_ticks, service_calls;
} writer_profile_t;

static int writer_service(writer_profile_t *profile) {
    if (profile == 0) return service(0);
    uint32_t begin = tick();
    int ok = service(0);
    profile->service_ticks += tick() - begin;
    ++profile->service_calls;
    return ok;
}

static int write_region(void *context, const badger_canvas_t *source,
                          const badger_frame_region_t *region) {
    writer_profile_t *profile = context;
    uint32_t begin = profile != 0 ? tick() : 0u;
    if (!writer_service(profile)) return 0;
    uint32_t x = region->x, y = region->y, width = region->width, height = region->height;
    badger_display_begin_region(x, y, width, height);
    for (uint32_t column = x; column < x + width; ++column) {
        badger_display_write_pixels(source->pixels + column * BADGER_GFX_HEIGHT + y, height);
        if ((column - x) % 16u == 15u && !writer_service(profile)) {
            badger_display_end_update();
            return 0;
        }
    }
    badger_display_end_update();
    int ok = writer_service(profile);
    if (profile != 0) profile->writer_ticks += tick() - begin;
    return ok;
}

static int transfer(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    badger_frame_invalidate(&presenter);
    const badger_frame_region_t region = {x,y,width,height};
    return write_region(0, &frame.canvas, &region);
}

static int present(badger_frame_stats_t *stats, writer_profile_t *profile) {
    if (!service(0)) return 0;
    badger_frame_result_t result = badger_frame_present(&presenter, &frame, write_region, profile, stats);
    if (result != BADGER_FRAME_OK) {
        if (words()[M2_ERROR] == 0u) {
            words()[M2_ERROR] = M2_DISPLAY_ERROR;
            words()[M2_DETAIL] = (uint32_t)result;
        }
        return 0;
    }
    return service(0);
}

static void edge_sample(int enabled) {
    uint32_t level = te_level(), now = tick();
    if (enabled) ++words()[level ? M2_TE_HIGH_SAMPLES : M2_TE_LOW_SAMPLES];
    if (level == te_previous) return;
    te_previous = level;
    ++te_edges;
    if (level) {
        uint32_t count = words()[M2_TE_PERIOD_COUNT];
        if (enabled && te_rise != 0u && count < NAMETAG_M2_SAMPLES)
            words()[M2_TE_PERIOD_BASE + words()[M2_TE_PERIOD_COUNT]++] = now - te_rise;
        te_rise = now;
    } else {
        uint32_t count = words()[M2_TE_WIDTH_COUNT];
        if (enabled && te_rise != 0u && count < NAMETAG_M2_SAMPLES)
            words()[M2_TE_WIDTH_BASE + words()[M2_TE_WIDTH_COUNT]++] = now - te_rise;
    }
}

static int capture_te(uint32_t duration, int enabled) {
    uint32_t begin = tick(), polled = begin;
    for (uint32_t loops = 0u; loops < 2000000u; ++loops) {
        uint32_t now = tick();
        edge_sample(enabled);
        if (now - begin >= duration) return service(0);
        if (now - polled >= 250u) {
            if (!service(0)) return 0;
            polled = now;
        }
    }
    words()[M2_ERROR] = M2_TIMER_ERROR;
    return 0;
}

static int probe_te(void) {
    REG(0x400280acu) = 5u; /* GPIO21 function select. */
    REG(0x40038058u) = (REG(0x40038058u) & ~0x18cu) | 0x40u;
    REG(0xd0000040u) = 1u << 21u;
    te_previous = te_level();
    words()[M2_TE_INITIAL] = te_previous;
    badger_display_set_tearing(0);
    te_edges = 0u;
    if (!capture_te(50000u, 0)) return 0;
    words()[M2_TE_OFF_EDGES] = te_edges;
    te_rise = te_edges = 0u;
    badger_display_set_tearing(1);
    if (!capture_te(1000000u, 1)) return 0;
    words()[M2_TE_ON_EDGES] = te_edges;
    for (uint32_t sample = 0u; sample < 16u; ++sample) {
        uint32_t begin = tick(), polled = begin, previous = te_level(), edge = 0u;
        for (uint32_t loops = 0u; loops < 400000u && tick() - begin < 100000u; ++loops) {
            uint32_t level = te_level(), now = tick();
            if (level && !previous) { edge = now; break; }
            previous = level;
            if (now - polled >= 250u) {
                if (!service(0)) return 0;
                polled = now;
            }
        }
        if (edge == 0u) { ++words()[M2_TE_TIMEOUTS]; continue; }
        if (!transfer(0u, 0u, 320u, 240u)) return 0;
        words()[M2_TE_TRANSFER_BASE + sample * 2u] = tick() - edge;
        words()[M2_TE_TRANSFER_BASE + sample * 2u + 1u] = (REG(0x50110048u) & 2047u);
        if (!service(0)) return 0;
    }
    badger_display_set_tearing(0);
    return 1;
}

static int benchmarks(void) {
    for (uint32_t bench = 0u; bench < NAMETAG_M2_BENCHES; ++bench) {
        for (uint32_t sample = 0u; sample < NAMETAG_M2_SAMPLES; ++sample) {
            if (!service(0)) return 0;
            uint32_t phase = bench == M2_RENDER_TRANSFER ? 5500u :
                sample % 3u == 0u ? 0u : sample % 3u == 1u ? 5500u : 6000u;
            if (bench == M2_TEXT && badger_nametag_background(&frame.canvas, &scene, phase) != BADGER_FONT_OK) {
                words()[M2_ERROR] = M2_FONT_ERROR; return 0;
            }
            if (bench == M2_RENDER_TRANSFER) {
                badger_frame_stats_t setup;
                if (badger_nametag_cached_render(&frame.canvas, &cache, 5467u) != BADGER_FONT_OK) {
                    words()[M2_ERROR] = M2_FONT_ERROR; return 0;
                }
                if (!present(&setup, 0)) return 0;
            }
            uint32_t begin = tick();
            badger_font_result_t result = BADGER_FONT_OK;
            int ok = 1;
            if (bench == M2_FULL_TRANSFER) ok = transfer(0u, 0u, 320u, 240u);
            else if (bench == M2_FOOTER_TRANSFER) ok = transfer(0u, 176u, 320u, 64u);
            else if (bench == M2_TILE_TRANSFER) ok = transfer(128u, 88u, 64u, 64u);
            else if (bench == M2_COMMANDS) {
                badger_display_begin_region(0u, 0u, 320u, 240u);
                badger_display_end_update();
            } else if (bench == M2_BACKGROUND) result = badger_nametag_background(&frame.canvas, &scene, phase);
            else if (bench == M2_TEXT) result = badger_nametag_cached_text(&frame.canvas, &cache, phase);
            else {
                result = badger_nametag_cached_render(&frame.canvas, &cache, phase);
                if (result == BADGER_FONT_OK && bench == M2_RENDER_TRANSFER) {
                    badger_frame_stats_t stats;
                    writer_profile_t profile = {0u, 0u, 0u};
                    uint32_t presented = tick();
                    ok = present(&stats, &profile);
                    words()[M2_DIFF_PRESENT_BASE + sample] = tick() - presented;
                    if (ok) {
                        words()[M2_DIFF_BENCH_PIXELS] += stats.pixels;
                        words()[M2_DIFF_BENCH_REGIONS] += stats.regions;
                        words()[M2_DIFF_BENCH_FULL] += stats.full;
                        words()[M2_DIFF_WRITER_TICKS] += profile.writer_ticks;
                        words()[M2_DIFF_SERVICE_TICKS] += profile.service_ticks;
                        words()[M2_DIFF_SERVICE_CALLS] += profile.service_calls;
                    }
                }
            }
            words()[M2_BENCH_BASE + bench * NAMETAG_M2_SAMPLES + sample] = tick() - begin;
            if (result != BADGER_FONT_OK) { words()[M2_ERROR] = M2_FONT_ERROR; return 0; }
            if (!ok) return 0;
        }
    }
    return service(0);
}

static int animate(int contrast) {
    uint32_t begin = tick(), frame_at = begin, loop_tick = begin, loop_sof = sof_total;
    uint32_t duration = contrast ? 12000000u : 80000000u, loop = 0u;
    while (1) {
        if (!service(0)) return 0;
        uint32_t now = tick(), elapsed = now - begin;
        if (!contrast && loop < NAMETAG_M2_LOOPS && elapsed >= (loop + 1u) * 8000000u) {
            words()[M2_LOOP_BASE + loop * 2u] = now - loop_tick;
            words()[M2_LOOP_BASE + loop * 2u + 1u] = sof_total - loop_sof;
            loop_tick = now; loop_sof = sof_total; ++loop;
        }
        if (elapsed >= duration) break;
        if ((int32_t)(now - frame_at) < 0) continue;
        if (contrast) {
            badger_gfx_clear(&frame.canvas, 0u);
            uint32_t offset = (elapsed / 20000u) % 64u;
            for (uint32_t x = 0u; x < 320u; ++x)
                if ((x + offset) % 64u < 32u) badger_gfx_rect(&frame.canvas, (int32_t)x, 0, 1, 240, 0xffffu);
        } else if (badger_nametag_cached_render(&frame.canvas, &cache, elapsed / 1000u) != BADGER_FONT_OK) {
            words()[M2_ERROR] = M2_FONT_ERROR; return 0;
        }
        badger_gfx_text(&frame.canvas, 4, 2, usb.serial, 1u, 0xffffu);
        badger_frame_stats_t stats;
        if (!present(&stats, 0)) return 0;
        if (!contrast) {
            words()[M2_DIFF_ANIMATION_PIXELS] += stats.pixels;
            words()[M2_DIFF_ANIMATION_REGIONS] += stats.regions;
            words()[M2_DIFF_ANIMATION_FULL] += stats.full;
            ++words()[M2_DIFF_ANIMATION_FRAMES];
        }
        ++words()[M2_FRAMES];
        frame_at += 33333u;
        now = tick();
        if ((int32_t)(now - frame_at) >= 0) {
            uint32_t skipped = (now - frame_at) / 33333u + 1u;
            words()[M2_SKIPPED_FRAMES] += skipped;
            frame_at += skipped * 33333u;
        }
    }
    if (!service(0)) return 0;
    if (!contrast) {
        if (loop != NAMETAG_M2_LOOPS) { words()[M2_ERROR] = M2_TIMER_ERROR; return 0; }
        words()[M2_LOOP_COUNT] = NAMETAG_M2_LOOPS;
        words()[M2_LOOP_TICKS] = loop_tick - begin;
        uint32_t sum = 0u;
        for (uint32_t i = 0u; i < NAMETAG_M2_LOOPS; ++i) sum += words()[M2_LOOP_BASE + i * 2u + 1u];
        words()[M2_LOOP_SOF] = sum;
    }
    return 1;
}

static void measure(void) {
    badger_bytes_zero((uint8_t *)words(), NAMETAG_M2_WORDS * 4u);
    words()[M2_MAGIC] = NAMETAG_M2_MAGIC;
    words()[M2_VERSION] = 1u;
    words()[M2_STAGE] = M2_CALIBRATE;
    for (uint32_t i = 0u; i < 12u; ++i) words()[448u + i] = startup_clocks[i];
    nametag_clock_snapshot(words() + M2_BEFORE);
    generation = usb.m2_generation;
    sof_previous = REG(0x50110048u) & 2047u;
    last_sof_tick = tick(); sof_total = 0u; qualified = 0;
    if (!calibrate(words() + M2_OLD_TICKS, words() + M2_OLD_SOF)) return;
    words()[M2_STAGE] = M2_CLOCK;
    int32_t error = nametag_clock_reference();
    if (error != 0) { words()[M2_ERROR] = M2_CLOCK_ERROR; words()[M2_DETAIL] = (uint32_t)error; return; }
    started = renewed = last_sof_tick = tick();
    sof_previous = REG(0x50110048u) & 2047u;
    nametag_clock_snapshot(words() + M2_AFTER);
    if (!calibrate(words() + M2_NEW_TICKS, words() + M2_NEW_SOF)) return;
    uint32_t span = words()[M2_NEW_TICKS];
    if (span < 126720u || span > 129280u) { words()[M2_ERROR] = M2_TIMER_ERROR; return; }
    qualified = 1;
    error = nametag_clock_frequency(9u, words() + M2_SYS_KHZ_Q5);
    if (error == 0) error = nametag_clock_frequency(8u, words() + M2_REF_KHZ_Q5);
    if (error != 0) { words()[M2_ERROR] = M2_CLOCK_ERROR; words()[M2_DETAIL] = (uint32_t)error; return; }
    badger_nametag_error_t font_error;
    if (badger_nametag_prepare(&scene, &badger_nametag_default_profile, &nametag_mono_fonts,
                              BADGER_NAMETAG_B, &font_error) != BADGER_NAMETAG_OK) {
        words()[M2_ERROR] = M2_FONT_ERROR; return;
    }
    words()[M2_STAGE] = M2_DISPLAY;
    if (!badger_display_init_serviced(service, 0)) {
        if (words()[M2_ERROR] == 0u) words()[M2_ERROR] = M2_DISPLAY_ERROR;
        return;
    }
    uint32_t cache_started = tick();
    if (badger_nametag_cache_prepare(&cache, &frame.canvas, &scene, 0) != BADGER_FONT_OK) {
        words()[M2_ERROR] = M2_FONT_ERROR; return;
    }
    words()[M2_CACHE_PREPARE_TICKS] = tick() - cache_started;
    words()[M2_CACHE_BYTES] = (uint32_t)sizeof(cache);
    words()[M2_FLAGS] |= NAMETAG_M2_CACHED_TEXT;
    words()[M2_FLAGS] |= NAMETAG_M2_DIRTY_TRANSFER;
    words()[M2_FLAGS] |= NAMETAG_M2_PRESENT_PROFILE;
    words()[M2_DIFF_STATE_BYTES] = (uint32_t)sizeof(presenter);
    words()[M2_DIFF_GRID] = BADGER_FRAME_TILE | BADGER_FRAME_OFFSET << 8u;
    badger_frame_invalidate(&presenter);
    if (badger_nametag_cached_render(&frame.canvas, &cache, 0u) != BADGER_FONT_OK) {
        words()[M2_ERROR] = M2_FONT_ERROR; return;
    }
    if (!transfer(0u, 0u, 320u, 240u)) return;
    badger_display_end();
    words()[M2_STAGE] = M2_TE;
    if (!probe_te()) return;
    words()[M2_STAGE] = M2_BENCHMARK;
    if (!benchmarks()) return;
    words()[M2_STAGE] = M2_ANIMATION;
    if (!animate(0)) return;
    words()[M2_STAGE] = M2_CONTRAST;
    if (!animate(1)) return;
    if (!calibrate(words() + M2_FINAL_TICKS, words() + M2_FINAL_SOF)) return;
    nametag_clock_snapshot(words() + M2_FINAL_CLOCK);
    words()[M2_DURATION] = tick() - started;
    words()[M2_STAGE] = M2_DONE;
}

void bare_main(void) {
    int32_t boot_error = badger_schedule_bootsel(8000u);
    nametag_clock_snapshot(startup_clocks);
    startup_trace(1u, boot_error);
    if (boot_error != 0) while (1) __asm__ volatile ("wfi");
    uint32_t id[2];
    char serial[17], half[9];
    boot_error = badger_rom_chip_id(id);
    startup_trace(2u, boot_error);
    if (boot_error != 0) while (1) __asm__ volatile ("wfi");
    diag_hex(half, id[1], 8u);
    for (uint32_t i = 0u; i < 8u; ++i) serial[i] = half[i];
    diag_hex(half, id[0], 8u);
    for (uint32_t i = 0u; i < 8u; ++i) serial[8u + i] = half[i];
    serial[16] = 0;
    live_usb_control_init(&usb, USB_REPORT_BUILD_ID, serial);
    boot_error = live_usb_hw_init();
    startup_trace(3u, boot_error);
    if (boot_error != 0) while (1) __asm__ volatile ("wfi");
    uint32_t wait_started = 0u;
    int finished = 0, reboot_wait = 0;
    while (1) {
        if (live_usb_hw_poll(&usb) != 0) break;
        if (usb.m2_ready && !finished) {
            usb.m2_ready = 0u;
            measure();
            startup_trace(4u, (int32_t)words()[M2_ERROR]);
            qualified = 0;
            if (usb.m2_generation == generation && usb.configured && !usb.reboot_requested)
                usb.m2_valid = 1u;
            finished = 1;
        }
        if (finished && usb.reboot_requested) {
            if (!reboot_wait) { wait_started = tick(); reboot_wait = 1; }
            if (tick() - wait_started >= 10000u) break;
        }
    }
    live_usb_hw_disconnect();
    int32_t error = badger_schedule_bootsel(10u);
    if (error != 0) {
        badger_frame_invalidate(&presenter);
        badger_gfx_clear(&frame.canvas, 0u);
        badger_gfx_text(&frame.canvas, 4, 4, "RECOVERY ERROR: USE BOOT", 1u, 0xffffu);
        badger_display_begin();
        badger_display_write_pixels(frame.canvas.pixels, 320u * 240u);
        badger_display_end();
    }
    while (1) __asm__ volatile ("wfi");
}

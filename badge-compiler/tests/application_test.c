#include <stdint.h>
#ifndef __arm__
#include <assert.h>
#include <stdio.h>
#endif
#include "../../src/tools/compiler_test/application.c"

static uint32_t pixels, expected_pixels, failures, regions, services, ticks;
static uint32_t pixel_hash, transfer_hash, full_regions, hud_regions, small_regions;

static void check(int condition) {
    if (!condition) ++failures;
#ifndef __arm__
    assert(condition);
#endif
}

void badger_display_begin_region(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    check(width > 0u && height > 0u && x + width <= 320u && y + height <= 240u);
    pixels = 0u;
    expected_pixels = width * height;
    pixel_hash = mix(mix(mix(mix(2166136261u, x), y), width), height);
    ++regions;
    if (expected_pixels == 76800u) ++full_regions;
    if (expected_pixels == 4480u) ++hud_regions;
    if (expected_pixels <= 400u) ++small_regions;
}

void badger_display_write(uint16_t pixel) {
    ++pixels;
    pixel_hash = mix(pixel_hash, pixel);
}

void badger_display_end_update(void) {
    check(pixels == expected_pixels);
    transfer_hash = mix(transfer_hash, pixel_hash);
}

void badger_display_end(void) { badger_display_end_update(); }

static uint32_t test_clock(void *context) {
    (void)context;
    ticks += 1000u;
    return ticks;
}
static uint32_t short_clock(void *context) {
    (void)context;
    return ++ticks;
}
static void test_service(void *context) {
    (void)context;
    ++services;
}

int main(void) {
    const uint32_t seeds[] = {0u, 1u, 123456789u, 3276377847u, 4294967295u};
    for (uint32_t seed_index = 0u; seed_index < 5u; ++seed_index) {
        for (uint32_t module = 0u; module < 6u; ++module) {
            compiler_result_t result, repeated;
            uint32_t samples[7];
            regions = full_regions = hud_regions = small_regions = 0u;
            transfer_hash = 2166136261u;
            check(compiler_application_measure(module, seeds[seed_index], &result,
                                               test_clock, test_service, 0, samples));
            uint32_t first_transfer = transfer_hash;
            transfer_hash = 2166136261u;
            check(compiler_application_measure(module, seeds[seed_index], &repeated,
                                               test_clock, test_service, 0, samples));
            check(first_transfer == transfer_hash);
            check(result.cases == counts[module] && result.values[4] == 1u);
            for (uint32_t lane = 0u; lane < 6u; ++lane)
                check(result.values[lane] == repeated.values[lane]);
            if (module == 1u || module == 4u) {
                check(result.values[3] >= 1u);
                if (seeds[seed_index] % 4u != 0u) check(result.values[2] >= 1u);
            }
            if (module == 3u || module == 4u)
                check(full_regions > 0u && hud_regions > 0u && small_regions > 0u);
#ifndef __arm__
            printf("%u %u %08x %08x\n", seeds[seed_index], module,
                   result.values[0], first_transfer);
#endif
        }
    }
    compiler_result_t result;
    uint32_t samples[7];
    check(!compiler_application_measure(0u, 1u, &result, short_clock, test_service, 0, samples));
    compiler_application_prepare(0u, 1u);
    maze_canvas_storage.after[0] ^= 1u;
    check(!compiler_application_result(0u, &result));
    check(services > 0u);
    return failures != 0u;
}

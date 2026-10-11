#include "suite.h"
#include "kernels.h"
#include "../diagnostics_logic.h"
#include "../../platform/badger/display.h"
#include "../../arch/rp2350/bootrom_reboot.h"
#include "../../usb/live_usb_hw.h"
#include "../../shared/bytes.h"
#include "compiler_test_build.h"
#ifdef COMPILER_TEST_APPLICATION
#include "application.h"
#else
static badger_canvas_t canvas;
#endif

static live_usb_control_t usb;
static compiler_result_t result;
static uint8_t records[COMPILER_SUITE_BYTES], nonce[16];
static int32_t error;
static uint32_t started, last_watchdog;

static uint32_t now(void) {
    return *(volatile uint32_t *)0x400b000cu;
}

#ifdef COMPILER_TEST_TIMING
static uint32_t timing_clock(void *context) {
    (void)context;
    __asm__ volatile ("" ::: "memory");
    return now();
}

static void clear_clock_history(void) {
    /* Clear inherited sticky lock-loss flags without changing PLL dividers or bypass. */
    volatile uint32_t *sys = (volatile uint32_t *)0x40050000u;
    volatile uint32_t *usb_pll = (volatile uint32_t *)0x40058000u;
    *sys = (*sys & 0x0000013fu) | 0x40000000u;
    *usb_pll = (*usb_pll & 0x0000013fu) | 0x40000000u;
}

static void clock_configuration(uint32_t values[14]) {
    values[0] = *(volatile uint32_t *)0x4001003cu;
    values[1] = *(volatile uint32_t *)0x40010040u;
    values[2] = *(volatile uint32_t *)0x40010030u;
    values[3] = *(volatile uint32_t *)0x40010034u;
    values[4] = *(volatile uint32_t *)0x40108018u;
    values[5] = *(volatile uint32_t *)0x4010801cu;
    values[6] = *(volatile uint32_t *)0x400b0038u;
    values[7] = *(volatile uint32_t *)0x40050000u;
    values[8] = *(volatile uint32_t *)0x40050008u;
    values[9] = *(volatile uint32_t *)0x4005000cu;
    values[10] = *(volatile uint32_t *)0x40058000u;
    values[11] = *(volatile uint32_t *)0x40058008u;
    values[12] = *(volatile uint32_t *)0x4005800cu;
    values[13] = *(volatile uint32_t *)0x40010044u;
}
#endif

static void service(void *context) {
    (void)context;
    if (error == 0) error = live_usb_hw_poll(&usb);
    if (now() - started >= 30000000u) usb.reboot_requested = 1u;
    if (error == 0 && !usb.reboot_requested && now() - last_watchdog >= 500000u) {
        error = badger_schedule_bootsel(8000u);
        last_watchdog = now();
    }
}

static int display_service(void *context) {
    service(context);
    return error == 0 && !usb.reboot_requested && usb.configured;
}

void bare_main(void) {
    started = last_watchdog = now();
    error = badger_schedule_bootsel(8000u);
    uint32_t id[2];
    char serial[17], half[9];
    if (error == 0) error = badger_rom_chip_id(id);
    if (error == 0) {
        diag_hex(half, id[1], 8u);
        for (uint32_t i = 0u; i < 8u; ++i) serial[i] = half[i];
        diag_hex(half, id[0], 8u);
        for (uint32_t i = 0u; i < 8u; ++i) serial[8u + i] = half[i];
        serial[16] = '\0';
        live_usb_control_init(&usb, COMPILER_TEST_BUILD_ID, serial);
        error = live_usb_hw_init();
    }
    while (error == 0 && !usb.reboot_requested) {
        service(0);
        if (!usb.compiler_ready || !usb.configured) continue;
        usb.compiler_ready = 0u;
        uint32_t generation = usb.compiler_generation;
        badger_bytes_copy(nonce, usb.compiler_nonce, 16u);
        uint32_t seed = usb_report_u32(nonce);
#ifdef COMPILER_TEST_APPLICATION
        if (!badger_display_init_serviced(display_service, 0)) continue;
#endif
#ifdef COMPILER_TEST_TIMING
        uint32_t clocks[14], final_clocks[14], samples[COMPILER_TIMING_SAMPLES];
        uint32_t timing_failure = 0u;
        clear_clock_history();
        clock_configuration(clocks);
        for (uint32_t sample = 0u; sample < COMPILER_TIMING_SAMPLES; ++sample) {
            uint32_t before = timing_clock(0);
            samples[sample] = timing_clock(0) - before;
        }
        compiler_data_record(records + COMPILER_TIMING_OVERHEAD_PAGE * 64u,
                             COMPILER_TEST_BUILD_ID, nonce, COMPILER_TIMING_OVERHEAD_PAGE, samples);
#endif
        for (uint32_t module = 0u; module < COMPILER_SUITE_MODULES; ++module) {
#ifdef COMPILER_TEST_APPLICATION
            if (!compiler_application_measure(module, seed, &result, timing_clock,
                                               service, 0, samples)) {
                timing_failure = module + 1u;
                compiler_record(records + (module + 1u) * 64u, COMPILER_TEST_BUILD_ID,
                                nonce, module, &result);
                compiler_data_record(records + (COMPILER_TIMING_FIRST_PAGE + module) * 64u,
                                     COMPILER_TEST_BUILD_ID, nonce,
                                     COMPILER_TIMING_FIRST_PAGE + module, samples);
                break;
            }
            compiler_record(records + (module + 1u) * 64u, COMPILER_TEST_BUILD_ID,
                            nonce, module, &result);
            compiler_data_record(records + (COMPILER_TIMING_FIRST_PAGE + module) * 64u,
                                 COMPILER_TEST_BUILD_ID, nonce,
                                 COMPILER_TIMING_FIRST_PAGE + module, samples);
#else
            compiler_module_run(module, seed, &result, &canvas, service, 0);
            compiler_record(records + (module + 1u) * 64u, COMPILER_TEST_BUILD_ID,
                            nonce, module, &result);
#ifdef COMPILER_TEST_TIMING
            if (!compiler_measure(module, seed, &result, &canvas, timing_clock, service, 0, samples)) {
                timing_failure = module + 1u;
                break;
            }
            compiler_data_record(records + (COMPILER_TIMING_FIRST_PAGE + module) * 64u,
                                 COMPILER_TEST_BUILD_ID, nonce, COMPILER_TIMING_FIRST_PAGE + module, samples);
#endif
#endif
            service(0);
            if (error != 0 || usb.reboot_requested || !usb.configured ||
                usb.compiler_generation != generation) break;
        }
        if (error != 0 || usb.reboot_requested || !usb.configured ||
            usb.compiler_generation != generation) continue;
#ifdef COMPILER_TEST_TIMING
#ifndef COMPILER_TEST_APPLICATION
        for (uint32_t kernel = 0u; kernel < COMPILER_KERNELS && timing_failure == 0u; ++kernel) {
            uint32_t page = COMPILER_KERNEL_FIRST_PAGE + kernel * 2u;
            if (!compiler_kernel_run(kernel, seed, &result) ||
                !compiler_kernel_measure(kernel, seed, &result, timing_clock, service, 0, samples)) {
                timing_failure = 8u + kernel;
                break;
            }
            compiler_record(records + page * 64u, COMPILER_TEST_BUILD_ID, nonce, page - 1u, &result);
            compiler_data_record(records + (page + 1u) * 64u, COMPILER_TEST_BUILD_ID,
                                 nonce, page + 1u, samples);
            service(0);
            if (error != 0 || usb.reboot_requested || !usb.configured ||
                usb.compiler_generation != generation) break;
        }
#endif
        if (error != 0 || usb.reboot_requested || !usb.configured ||
            usb.compiler_generation != generation) continue;
        clock_configuration(final_clocks);
        for (uint32_t i = 0u; i < 14u; ++i)
            if (clocks[i] != final_clocks[i]) timing_failure = 7u;
        for (uint32_t page = 0u; page < 2u; ++page)
            compiler_data_record(records + (COMPILER_TIMING_CLOCK_PAGE + page) * 64u,
                                 COMPILER_TEST_BUILD_ID, nonce, COMPILER_TIMING_CLOCK_PAGE + page,
                                 clocks + page * 7u);
        if (timing_failure != 0u) {
            compiler_summary(records, COMPILER_TEST_BUILD_ID, nonce, 2u, timing_failure);
            badger_bytes_copy(usb.compiler_records, records, COMPILER_SUITE_BYTES);
            usb.compiler_valid = 1u;
            continue;
        }
#endif
#ifndef COMPILER_TEST_APPLICATION
        badger_gfx_text(&canvas, 8, 112, "MODULES COMPLETE / HOST WILL COMPARE", 1u, 0xffffu);
        if (!badger_display_init_serviced(display_service, 0)) continue;
        badger_display_begin();
        for (uint32_t i = 0u; i < BADGER_GFX_WIDTH * BADGER_GFX_HEIGHT; ++i) {
            if ((i & 255u) == 0u) service(0);
            if (error != 0 || usb.reboot_requested || !usb.configured ||
                usb.compiler_generation != generation) break;
            badger_display_write(canvas.pixels[i]);
        }
        badger_display_end();
        if (error != 0 || usb.reboot_requested || !usb.configured ||
            usb.compiler_generation != generation) continue;
#endif
        compiler_summary(records, COMPILER_TEST_BUILD_ID, nonce, USB_REPORT_OK, 0u);
        badger_bytes_copy(usb.compiler_records, records, COMPILER_SUITE_BYTES);
        usb.compiler_valid = 1u;
    }
    live_usb_hw_disconnect();
    badger_schedule_bootsel(10u);
    while (1) __asm__ volatile ("wfi");
}

#include "board.h"
#include "fs.h"
#include "build_id.h"
#include "../src/usb/live_usb_hw.h"
#include "../src/arch/rp2350/bootrom_reboot.h"
#include "../src/arch/rp2350/nametag_clock.h"
#include "../src/tools/diagnostics_logic.h"

static live_usb_control_t control;
static logi_board_t board;
static fs_result_t fs_result;
volatile int32_t logi_host_error;

static uint32_t read_register(void *context, uint32_t address) {
    (void)context;
    return *(volatile uint32_t *)(uintptr_t)address;
}

static void write_register(void *context, uint32_t address, uint32_t value) {
    (void)context;
    *(volatile uint32_t *)(uintptr_t)address = value;
}

static uint32_t now(void *context) {
    (void)context;
    return *(volatile uint32_t *)0x400b000cu;
}

static void reboot(void) __attribute__((noreturn));
static void reboot(void) {
    live_usb_hw_disconnect();
    int32_t result = badger_schedule_bootsel(10u);
    if (result) logi_host_error = result;
    while (1) __asm__ volatile ("wfi");
}

void bare_main(void) {
    logi_host_error = badger_schedule_bootsel(8000u);
    if (!logi_host_error) logi_host_error = nametag_clock_reference();
    if (logi_host_error) while (1) __asm__ volatile ("wfi");
    uint32_t id[2];
    logi_host_error = badger_rom_chip_id(id);
    if (logi_host_error) while (1) __asm__ volatile ("wfi");
    char serial[17], half[9];
    diag_hex(half, id[1], 8u);
    for (uint32_t i = 0u; i < 8u; ++i) serial[i] = half[i];
    diag_hex(half, id[0], 8u);
    for (uint32_t i = 0u; i < 8u; ++i) serial[8u + i] = half[i];
    serial[16] = 0;
    live_usb_control_init(&control, USB_REPORT_BUILD_ID, serial);
    logi_io_t io = {.read = read_register, .write = write_register, .now = now};
    logi_host_error = logi_board_init(&board, &io);
    int32_t usb_error = live_usb_hw_init();
    if (usb_error) { logi_host_error = usb_error; while (1) __asm__ volatile ("wfi"); }
    int32_t fs_clock_error = fs_pio_clock();
    uint32_t frequency = 0u;
    if (!fs_clock_error && (nametag_clock_frequency(9u, &frequency) ||
        frequency < 143900u * 32u || frequency > 144100u * 32u)) fs_clock_error = FS_CLOCK;
    fs_result.error = fs_clock_error;
    fs_result.clock_ready = !fs_clock_error;
    fs_wire_init();
    uint16_t fs_generation = 0u;
    uint32_t previous = now(0), elapsed = 0u, milliseconds = 0u, renewed = previous;
    uint32_t published = previous, last_poll = previous, observed_setups = 0u, last_host = previous;
    while (1) {
        usb_error = live_usb_hw_poll(&control);
        if (usb_error) { logi_host_error = usb_error; reboot(); }
        if (control.reboot_requested) {
            /* Let the native controller finish its status ACK before disconnecting. */
            uint32_t start = now(0);
            while (now(0) - start < 10000u) {}
            reboot();
        }
        if (control.logi_fs_ready) {
            control.logi_fs_ready = 0u;
            uint32_t start = now(0);
            while (now(0) - start < 10000u) {}
            if (!fs_clock_error) {
                const fs_bus_t bus = {.context = &fs_result, .now = fs_pio_now,
                                      .reset = fs_pio_reset, .exchange = fs_pio_exchange};
                (void)fs_probe(&bus, &fs_result);
                fs_pio_trace_finish(&fs_result);
                fs_result.clock_ready = 1u;
                fs_pio_stop();
                int32_t restored = logi_board_init(&board, &io);
                if (restored) {
                    logi_host_error = restored;
                    if (!fs_result.error) fs_result.error = restored;
                }
            }
            if (!++fs_generation) ++fs_generation;
            fs_result.generation = fs_generation;
            fs_report(&fs_result, USB_REPORT_BUILD_ID, 0u, control.logi_fs_status);
            fs_trace_report(&fs_result, USB_REPORT_BUILD_ID, control.logi_fs_trace);
            control.logi_fs_valid = 1u;
            control.logi_status_valid = 0u;
        }
        uint32_t tick = now(0);
        elapsed += tick - previous;
        previous = tick;
        milliseconds += elapsed / 1000u;
        elapsed %= 1000u;
        if (tick - last_poll >= 1000u) { logi_board_poll(&board); last_poll = tick; }
        if (tick - published >= 10000u || !control.logi_status_valid) {
            logi_board_status(&board, control.logi_status, USB_REPORT_BUILD_ID,
                              milliseconds, live_usb_hw_stats()->hardware_errors);
            control.logi_status_valid = 1u;
            fs_report(&fs_result, USB_REPORT_BUILD_ID, control.logi_status[52], control.logi_fs_status);
            fs_trace_report(&fs_result, USB_REPORT_BUILD_ID, control.logi_fs_trace);
            control.logi_fs_valid = 1u;
            published = tick;
        }
        uint32_t setups = live_usb_hw_stats()->setups;
        if (setups != observed_setups) { observed_setups = setups; last_host = tick; }
        if (tick - last_host >= 60000000u) reboot();
        if (tick - renewed >= 500000u) {
            int32_t result = badger_schedule_bootsel(8000u);
            if (result) { logi_host_error = result; reboot(); }
            renewed = tick;
        }
    }
}

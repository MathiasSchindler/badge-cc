#include "board.h"
#include "fs.h"
#include "fs_lcd.h"
#include "build_id.h"
#include "../src/usb/live_usb_hw.h"
#include "../src/arch/rp2350/bootrom_reboot.h"
#include "../src/arch/rp2350/nametag_clock.h"
#include "../src/tools/diagnostics_logic.h"

static live_usb_control_t control;
static logi_board_t board;
static fs_result_t fs_result;
static fs_keypad_t keypad;
static fs_lcd_t lcd;
static const fs_bus_t bus = {.context = &fs_result, .now = fs_pio_now,
                            .reset = fs_pio_reset, .exchange = fs_pio_exchange};
static uint16_t keypad_generation;
_Static_assert(FS_KEYPAD_EVENTS == LIVE_USB_LOGI_KEYPAD_SLOTS, "keypad ring capacity mismatch");
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

static void restore_passive(const logi_io_t *io) {
    fs_pio_trace_finish(&fs_result);
    fs_pio_stop();
    int32_t restored = logi_board_init(&board, io);
    if (restored) {
        logi_host_error = restored;
        if (!fs_result.error) fs_result.error = restored;
        if (!keypad.error && control.logi_keypad_active) {
            keypad.error = restored;
            keypad.stage = 4u;
        }
    }
    control.logi_status_valid = 0u;
}

static void publish_keypad(void) {
    fs_keypad_status(&keypad, USB_REPORT_BUILD_ID, keypad_generation, control.logi_keypad_status);
    fs_lcd_status(&lcd, control.logi_keypad_status);
    control.logi_keypad_valid = 1u;
}

static void finish_keypad(const logi_io_t *io) {
    (void)fs_keypad_stop(&keypad);
    if (lcd.stage && keypad.error) lcd.stage = FS_LCD_FAILED;
    restore_passive(io);
    control.logi_keypad_active = 0u;
    publish_keypad();
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
    uint32_t keypad_started = 0u;
    uint32_t previous = now(0), elapsed = 0u, milliseconds = 0u, renewed = previous;
    uint32_t published = previous, last_poll = previous, observed_setups = 0u, last_host = previous;
    while (1) {
        usb_error = live_usb_hw_poll(&control);
        if (usb_error) {
            logi_host_error = usb_error;
            if (control.logi_keypad_active) { keypad.error = usb_error; finish_keypad(&io); }
            reboot();
        }
        if (control.reboot_requested) {
            /* Let the native controller finish its status ACK before disconnecting. */
            uint32_t start = now(0);
            while (now(0) - start < 10000u) {}
            if (control.logi_keypad_active) {
                keypad.error = FS_CANCELLED;
                finish_keypad(&io);
            }
            reboot();
        }
        if (control.logi_fs_ready) {
            control.logi_fs_ready = 0u;
            uint32_t start = now(0);
            while (now(0) - start < 10000u) {}
            if (control.logi_keypad_seconds) {
                if (!++keypad_generation) ++keypad_generation;
                control.logi_keypad_active = 1u;
                badger_bytes_zero((uint8_t *)&lcd, sizeof(lcd));
                if (control.logi_lcd_requested) fs_lcd_begin(&lcd, control.logi_lcd_requested);
                if (fs_clock_error) {
                    keypad.error = fs_clock_error;
                    keypad.stage = 4u;
                } else {
                    (void)fs_keypad_start(&bus, &fs_result, &keypad);
                }
                if (lcd.stage) keypad.host.budget = 62000000u;
                keypad_started = now(0);
                if (keypad.error) finish_keypad(&io);
            } else if (!fs_clock_error) {
                (void)fs_probe(&bus, &fs_result);
                restore_passive(&io);
            }
            fs_result.clock_ready = !fs_clock_error;
            if (!++fs_generation) ++fs_generation;
            fs_result.generation = fs_generation;
            fs_report(&fs_result, USB_REPORT_BUILD_ID, 0u, control.logi_fs_status);
            fs_trace_report(&fs_result, USB_REPORT_BUILD_ID, control.logi_fs_trace);
            control.logi_fs_valid = 1u;
            control.logi_status_valid = 0u;
        }
        uint32_t tick = now(0);
        if (control.logi_keypad_active) {
            if (lcd.stage && lcd.images == 9u &&
                tick - lcd.hold_started >= (uint32_t)control.logi_keypad_seconds * 1000000u)
                lcd.stopping = 1u;
            if (!control.configured) {
                keypad.error = FS_CANCELLED;
                finish_keypad(&io);
            } else if (!lcd.stage &&
                       tick - keypad_started >= (uint32_t)control.logi_keypad_seconds * 1000000u) {
                finish_keypad(&io);
            } else if (lcd.stage && lcd.stopping && !lcd.waiting && !keypad.output_length &&
                       !lcd.rendering && lcd.desired == lcd.displayed) {
                lcd.stage = FS_LCD_DONE;
                finish_keypad(&io);
            } else if (lcd.stage && ((!lcd.hold_started && tick - keypad_started >= 30000000u) ||
                       (lcd.stopping && tick - lcd.hold_started >=
                        (1u + control.logi_keypad_seconds) * 1000000u) ||
                       tick - keypad_started >= (31u + control.logi_keypad_seconds) * 1000000u)) {
                keypad.error = FS_DEADLINE;
                finish_keypad(&io);
            } else {
                fs_packet_t packet;
                int32_t result = fs_lcd_step(&lcd, &keypad);
                if (!result) result = fs_keypad_step(&keypad, &packet);
                if (result < 0) { keypad.error = result; finish_keypad(&io); }
                else if (result > 0) {
                    fs_keypad_event(&keypad, &packet, USB_REPORT_BUILD_ID, keypad_generation,
                                    control.logi_keypad_events[(keypad.reports - 1u) % FS_KEYPAD_EVENTS]);
                    int32_t error = fs_lcd_input(&lcd, &packet);
                    if (error) keypad.error = error;
                    if (keypad.error) finish_keypad(&io);
                }
            }
        }
        elapsed += tick - previous;
        previous = tick;
        milliseconds += elapsed / 1000u;
        elapsed %= 1000u;
        if (!control.logi_keypad_active && tick - last_poll >= 1000u) {
            logi_board_poll(&board); last_poll = tick;
        }
        if (tick - published >= 10000u || !control.logi_status_valid) {
            logi_board_status(&board, control.logi_status, USB_REPORT_BUILD_ID,
                              milliseconds, live_usb_hw_stats()->hardware_errors);
            control.logi_status_valid = 1u;
            fs_report(&fs_result, USB_REPORT_BUILD_ID, control.logi_status[52], control.logi_fs_status);
            fs_trace_report(&fs_result, USB_REPORT_BUILD_ID, control.logi_fs_trace);
            control.logi_fs_valid = 1u;
            publish_keypad();
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

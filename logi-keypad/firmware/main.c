#include "trace.h"
#include "build_id.h"
#include "../../src/usb/live_usb_hw.h"
#include "../../src/platform/badger/display.h"
#include "../../src/platform/badger/graphics.h"
#include "../../src/platform/badger/diagnostic_hw.h"
#include "../../src/arch/rp2350/bootrom_reboot.h"
#include "../../src/arch/rp2350/nametag_clock.h"

static live_usb_control_t control;
static keypad_trace_t trace;
static badger_canvas_t canvas;
static diag_buttons_t buttons;
static uint8_t dirty, display_ready, usb_ready;
static uint32_t rejected;
volatile int32_t keypad_trace_error;

static void publish(void) {
    keypad_trace_status(&trace, control.keypad_status, USB_REPORT_BUILD_ID, control.rejected);
    control.keypad_status_valid = 1u;
    rejected = control.rejected;
}

static int service(void *context) {
    (void)context;
    keypad_trace_error = live_usb_hw_poll(&control);
    if (keypad_trace_error != 0) return 0;
    if (live_usb_hw_stats()->hardware_errors) {
        keypad_trace_error = -4201;
        return 0;
    }
    if (control.keypad_ready) {
        control.keypad_ready = 0u;
        /* A rejected sequence is reported in the status record and on screen. */
        (void)keypad_trace_accept(&trace, control.keypad_record);
        publish();
        dirty = 1u;
    }
    if (!control.keypad_status_valid || rejected != control.rejected) {
        publish();
        dirty = 1u;
    }
    return 1;
}

static void text(int32_t x, int32_t y, const char *value, uint16_t color) {
    badger_gfx_text(&canvas, x, y, value, 1u, color);
}

static void number(int32_t x, int32_t y, uint32_t value) {
    char digits[11];
    uint32_t at = 10u;
    digits[at] = 0;
    do { digits[--at] = (char)('0' + value % 10u); value /= 10u; } while (value);
    text(x, y, digits + at, 0xffffu);
}

static int present(uint8_t serviced) {
    badger_display_begin();
    for (uint32_t x = 0u; x < BADGER_GFX_WIDTH; ++x) {
        badger_display_write_pixels(canvas.pixels + x * BADGER_GFX_HEIGHT, BADGER_GFX_HEIGHT);
        if (serviced && !service(0)) {
            badger_display_end_update();
            return 0;
        }
    }
    badger_display_end();
    return 1;
}

static void stop(int32_t error) __attribute__((noreturn));
static void stop(int32_t error) {
    keypad_trace_error = error;
    if (usb_ready) live_usb_hw_disconnect();
    if (display_ready && error) {
        char digits[12];
        badger_gfx_clear(&canvas, 0x0801u);
        text(8, 24, "KEYPAD TRACE ERROR", 0xf9c7u);
        diag_decimal(digits, error);
        text(8, 48, digits, 0xffffu);
        text(8, 72, "RETURNING TO BOOTSEL", 0xffffu);
        (void)present(0u);
    }
    int32_t reboot = badger_schedule_bootsel(error ? 2000u : 10u);
    if (reboot != 0) {
        keypad_trace_error = reboot;
        if (display_ready) {
            text(8, 100, "REBOOT FAILED / USE BOOT BUTTON", 0xf9c7u);
            (void)present(0u);
        }
    }
    while (1) __asm__ volatile ("wfi");
}

static void screen(void) {
    badger_gfx_clear(&canvas, 0x0863u);
    badger_gfx_text(&canvas, 8, 6, "KEYPAD USB TRACE", 2u, 0xffffu);
    text(8, 30, "DEVICE / RECORDED REPLAY / NOT HOST", 0xfda8u);
    text(8, 46, control.configured ? "USB CONFIGURED" : "WAITING FOR MAC", 0x5ecbu);
    text(8, 62, "TOTAL", 0x9cf3u); number(44, 62, trace.total);
    text(116, 62, "UNKNOWN", 0x9cf3u); number(164, 62, trace.unknown);
    text(8, 76, "OVERWRITTEN", 0x9cf3u); number(80, 76, trace.total - trace.count);
    text(164, 76, trace.frozen ? "HOLD" : "FOLLOW", 0xfda8u);
    if (trace.error || control.rejected) {
        text(8, 90, "ERROR / USB REJECTED", 0xf9c7u);
        number(134, 90, trace.error); number(170, 90, control.rejected);
    }
    const keypad_trace_event_t *event = keypad_trace_selected(&trace);
    if (event) {
        text(8, 104, "EVENT", 0x9cf3u); number(44, 104, trace.selected);
        text(116, 104, "MS", 0x9cf3u); number(134, 104, badger_bytes_load_u32(event->wire + 8u));
        if (event->key > 0) {
            text(8, 118, "LCD KEY", 0xffffu); number(56, 118, (uint32_t)event->key);
        } else if (event->key == 0) text(8, 118, "LCD RELEASE (IDENTITY NOT ENCODED)", 0xffffu);
        else if (event->navigation >= 0) {
            const char *names[] = {"NAV RELEASE", "NAV LEFT", "NAV RIGHT", "NAV LEFT+RIGHT"};
            text(8, 118, names[(uint8_t)event->navigation], 0xffffu);
        } else text(8, 118, "UNKNOWN / COMMAND RESPONSE (RAW)", 0xfda8u);
        for (uint32_t row = 0u; row < 4u; ++row) {
            char bytes[25];
            uint32_t used = 0u;
            for (uint32_t i = row * 8u; i < (row + 1u) * 8u && i < event->wire[4]; ++i) {
                char hex[9];
                diag_hex(hex, event->wire[16u + i], 2u);
                bytes[used++] = hex[0]; bytes[used++] = hex[1]; bytes[used++] = ' ';
            }
            bytes[used] = 0;
            text(8, 134 + (int32_t)row * 12, bytes, 0xffffu);
        }
    } else text(8, 118, "SEND A CAPTURE FROM THE MAC", 0xffffu);
    text(8, 190, "LEFT/RIGHT: HISTORY  ENTER: HOLD/FOLLOW", 0x9cf3u);
    text(8, 204, "UP: CLEAR  DOWN: LATEST  HOME: BOOTSEL", 0x9cf3u);
    text(8, 222, control.serial, 0x9cf3u);
    if (!present(1u)) stop(keypad_trace_error);
}

void bare_main(void) {
    int32_t error = badger_schedule_bootsel(8000u);
    if (error) stop(error);
    error = nametag_clock_reference();
    if (error) stop(error);
    uint32_t id[2];
    error = badger_rom_chip_id(id);
    if (error) stop(error);
    char serial[17], half[9];
    diag_hex(half, id[1], 8u);
    for (uint32_t i = 0u; i < 8u; ++i) serial[i] = half[i];
    diag_hex(half, id[0], 8u);
    for (uint32_t i = 0u; i < 8u; ++i) serial[8u + i] = half[i];
    serial[16] = 0;
    live_usb_control_init(&control, USB_REPORT_BUILD_ID, serial);
    keypad_trace_init(&trace);
    publish();
    error = live_usb_hw_init();
    if (error) stop(error);
    usb_ready = 1u;
    if (!badger_display_init_serviced(service, 0)) stop(keypad_trace_error);
    display_ready = 1u;
    diag_gpio_init();
    uint32_t renewed = diag_time_us(), frame_at = renewed;
    uint8_t configured = control.configured;
    dirty = 1u;
    while (1) {
        if (!service(0)) stop(keypad_trace_error);
        if (control.reboot_requested) stop(0);
        uint32_t now = diag_time_us();
        uint8_t pressed = diag_buttons_update(&buttons, diag_buttons_raw(), now);
        if (pressed & DIAG_HOME) stop(0);
        if (pressed & DIAG_LEFT) keypad_trace_browse(&trace, -1);
        if (pressed & DIAG_RIGHT) keypad_trace_browse(&trace, 1);
        if (pressed & DIAG_ENTER) {
            trace.frozen ^= 1u;
            if (!trace.frozen) trace.selected = trace.total;
        }
        if (pressed & DIAG_UP) keypad_trace_init(&trace);
        if (pressed & DIAG_DOWN) { trace.frozen = 0u; trace.selected = trace.total; }
        if (pressed) { publish(); dirty = 1u; }
        if (configured != control.configured) { configured = control.configured; dirty = 1u; }
        if (dirty && now - frame_at >= 100000u) {
            dirty = 0u;
            screen();
            frame_at = now;
        }
        if (now - renewed >= 500000u) {
            error = badger_schedule_bootsel(8000u);
            if (error) stop(error);
            renewed = now;
        }
    }
}

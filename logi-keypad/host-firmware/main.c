#include "keypad_host.h"
#include "../firmware/trace.h"
#include "build_id.h"
#include "../../src/usb/live_usb_hw.h"
#include "../../src/platform/badger/display.h"
#include "../../src/platform/badger/graphics.h"
#include "../../src/platform/badger/diagnostic_hw.h"
#include "../../src/arch/rp2350/bootrom_reboot.h"
#include "../../src/arch/rp2350/nametag_clock.h"

#ifndef KEYPAD_HOST_HALT
#define KEYPAD_HOST_HALT() __asm__ volatile("wfi")
#endif

static usb_host_t usb;
static keypad_host_t keypad;
static keypad_trace_t trace;
static badger_canvas_t canvas;
static diag_buttons_t buttons;
static const char *stage = "DISCONNECT MAC / EXTERNAL 5V REQUIRED";
static uint32_t renewed, previous_time, remainder, uptime_ms, input_at;
static uint32_t failure_sie, failure_buffer;
static uint32_t frame_at, attach_at, previous_speed;
static int32_t recovery_error;
static uint8_t pressed, home, armed, cleaning, dirty, display_ready, nav_on, restore_failed;
static host_result_t error;
volatile int32_t keypad_host_error;

static uint32_t read_register(void *context, uint32_t address) {
    (void)context;
    return *(volatile uint32_t *)(uintptr_t)address;
}

static void write_register(void *context, uint32_t address, uint32_t value) {
    (void)context;
    *(volatile uint32_t *)(uintptr_t)address = value;
}

static uint32_t time_us(void *context) {
    (void)context;
    return diag_time_us();
}

static int service(void *context) {
    (void)context;
    if (recovery_error) return 0;
    uint32_t now = diag_time_us();
    remainder += now - previous_time;
    previous_time = now;
    uptime_ms += remainder / 1000u;
    remainder %= 1000u;
    uint8_t edges = diag_buttons_update(&buttons, diag_buttons_raw(), now);
    pressed |= edges;
    if (edges & DIAG_HOME) home = 1u;
    if (now - renewed >= 500000u) {
        int32_t result = badger_schedule_bootsel(8000u);
        if (result != 0) {
            recovery_error = keypad_host_error = result;
            return 0;
        }
        renewed = now;
    }
    return (!home || cleaning) && (!armed || diag_gpio_level(12u));
}

static void text(int32_t x, int32_t y, const char *value, uint16_t color) {
    badger_gfx_text(&canvas, x, y, value, 1u, color);
}

static void number(int32_t x, int32_t y, uint32_t value) {
    char digits[11];
    uint32_t at = 10u;
    digits[at] = 0;
    do {
        digits[--at] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value);
    text(x, y, digits + at, 0xffffu);
}

static int report(void *context, const uint8_t *raw, uint16_t length, uint8_t feature) {
    (void)context;
    uint8_t wire[64];
    if (!keypad_trace_record(wire, raw, length, feature, uptime_ms, trace.total + 1u)) return 0;
    wire[7] = 2u;
    badger_bytes_store_u32(wire + 60u, diag_crc32(wire, 60u));
    if (!keypad_trace_accept(&trace, wire)) return 0;
    dirty = 1u;
    return 1;
}

static host_result_t poll_input(void) {
    if (!keypad.ready) return HOST_OK;
    uint32_t now = diag_time_us();
    if (now - input_at < (uint32_t)keypad.endpoints.input_interval * 1000u) return HOST_OK;
    input_at = now;
    host_result_t result = keypad_host_poll(&keypad);
    return result == HOST_NAK ? HOST_OK : result;
}

static void screen(uint8_t poll) {
    badger_gfx_clear(&canvas, 0x0863u);
    badger_gfx_text(&canvas, 8, 6, "KEYPAD NATIVE HOST", 2u, 0xffffu);
    text(8, 28, "EXPERIMENTAL / FULL SPEED / NO VBUS OUTPUT", 0xfda8u);
    text(8, 42, stage, error == HOST_OK ? 0x5ecbu : 0xf9c7u);
    if (!armed)
        text(8, 56,
             restore_failed ? "POWER CYCLE KEYPAD / ENTER TO RETRY"
                            : "ENTER: ARM WITH REVIEWED POWER/ADAPTER",
             0xfda8u);
    else {
        text(8, 56, "SPEED", 0x9cf3u);
        number(44, 56, usb_host_speed(&usb));
        text(68, 56, "IN/OUT", 0x9cf3u);
        number(110, 56, keypad.endpoints.input_size);
        number(140, 56, keypad.endpoints.output_size);
        text(174, 56, nav_on ? "NAV ENABLED" : "NAV ORIGINAL", 0x9cf3u);
    }
    text(8, 70, "TOTAL", 0x9cf3u);
    number(44, 70, trace.total);
    text(110, 70, "UNKNOWN", 0x9cf3u);
    number(158, 70, trace.unknown);
    text(8, 82, "OVERWRITTEN", 0x9cf3u);
    number(80, 82, trace.total - trace.count);
    text(164, 82, trace.frozen ? "HOLD" : "FOLLOW", 0xfda8u);
    const keypad_trace_event_t *event = keypad_trace_selected(&trace);
    if (event) {
        text(8, 98, "EVENT", 0x9cf3u);
        number(44, 98, trace.selected);
        text(110, 98, "MS", 0x9cf3u);
        number(128, 98, badger_bytes_load_u32(event->wire + 8u));
        if (event->key > 0) {
            text(8, 110, "LCD KEY", 0xffffu);
            number(56, 110, (uint32_t)event->key);
        } else if (event->key == 0)
            text(8, 110, "LCD RELEASE / NO ENCODED IDENTITY", 0xffffu);
        else if (event->navigation >= 0) {
            const char *names[] = {"NAV RELEASE", "NAV LEFT", "NAV RIGHT", "NAV LEFT+RIGHT"};
            text(8, 110, names[(uint8_t)event->navigation], 0xffffu);
        } else
            text(8, 110, "UNKNOWN / COMMAND RESPONSE RAW", 0xfda8u);
        for (uint32_t row = 0u; row < 4u; ++row) {
            char bytes[25];
            uint32_t used = 0u;
            for (uint32_t i = row * 8u; i < (row + 1u) * 8u && i < event->wire[4]; ++i) {
                char hex[9];
                diag_hex(hex, event->wire[16u + i], 2u);
                bytes[used++] = hex[0];
                bytes[used++] = hex[1];
                bytes[used++] = ' ';
            }
            bytes[used] = 0;
            text(8, 124 + (int32_t)row * 10, bytes, 0xffffu);
        }
    } else
        text(8, 110, "NO KEYPAD INPUT RECORDED", 0x9cf3u);
    if (error != HOST_OK) {
        char digits[12], hex[9];
        diag_decimal(digits, recovery_error ? recovery_error : error);
        text(8, 168, digits, 0xf9c7u);
        diag_hex(hex, failure_sie, 8u);
        text(68, 168, hex, 0xf9c7u);
        diag_hex(hex, failure_buffer, 8u);
        text(128, 168, hex, 0xf9c7u);
        text(8, 180, "ERROR / SIE STATUS / BUFFER", 0x9cf3u);
    } else {
        text(8, 168, "NAK (NORMAL)", 0x9cf3u);
        number(86, 168, usb.naks);
        text(164, 168, "HID CRC", 0x9cf3u);
        char hex[9];
        diag_hex(hex, keypad.report_crc, 8u);
        text(212, 168, hex, 0xffffu);
    }
    text(8, 194, "LEFT/RIGHT: HISTORY  DOWN: FOLLOW/HOLD", 0x9cf3u);
    text(8, 206, "UP: CLEAR  ENTER: NAV  HOME: BOOTSEL", 0x9cf3u);
    char build[9];
    diag_hex(build, USB_REPORT_BUILD_ID, 8u);
    text(8, 222, build, 0x9cf3u);
    text(74, 222, "NO HUBS / NO LCD UPLOAD", 0x9cf3u);
    badger_display_begin();
    for (uint32_t x = 0u; x < BADGER_GFX_WIDTH; ++x) {
        badger_display_write_pixels(canvas.pixels + x * BADGER_GFX_HEIGHT, BADGER_GFX_HEIGHT);
        if (!service(0) && !cleaning) break;
        if (poll) {
            host_result_t result = poll_input();
            if (result != HOST_OK) {
                error = result;
                break;
            }
        }
    }
    badger_display_end();
}

static void progress(void *context, const char *value) {
    (void)context;
    stage = value;
    screen(0u);
}

static void reset_keypad(void) {
    keypad_host_t fresh = {.usb = &usb, .report = report, .progress = progress};
    badger_bytes_copy((uint8_t *)&keypad, (const uint8_t *)&fresh, sizeof(keypad));
    nav_on = 0u;
}

static void fail_session(host_result_t result) {
    failure_sie = usb.last_status;
    failure_buffer = usb.last_buffer;
    if (keypad.changed && !usb.stop_failed && diag_gpio_level(12u) && usb_host_speed(&usb) == 2u) {
        cleaning = 1u;
        (void)keypad_host_restore(&keypad);
        cleaning = 0u;
    }
    restore_failed = keypad.changed != 0u;
    keypad.ready = 0u;
    usb_host_off(&usb);
    armed = 0u;
    error = result;
    stage = usb_host_error(result);
    dirty = 1u;
    keypad_host_error = recovery_error ? recovery_error : result;
}

static void reboot(void) __attribute__((noreturn));
static void reboot(void) {
    if (keypad.changed) {
        cleaning = 1u;
        host_result_t restored = keypad_host_restore(&keypad);
        cleaning = 0u;
        if (restored != HOST_OK) {
            stage = "NAV RESTORE FAILED / UNPLUG KEYPAD";
            error = restored;
        }
    }
    usb_host_off(&usb);
    armed = 0u;
    if (display_ready && error != HOST_OK) {
        cleaning = 1u;
        screen(0u);
        cleaning = 0u;
    }
    int32_t result = badger_schedule_bootsel(error == HOST_OK ? 10u : 3000u);
    if (result != 0) {
        recovery_error = keypad_host_error = result;
        error = HOST_RECOVERY;
        stage = "REBOOT FAILED / USE PHYSICAL BOOT";
        if (display_ready) {
            cleaning = 1u;
            screen(0u);
            cleaning = 0u;
        }
    }
    while (1) KEYPAD_HOST_HALT();
}

static void step(void) {
    if (!service(0) && !home && armed) fail_session(HOST_CANCELLED);
    if (recovery_error) {
        fail_session(HOST_RECOVERY);
        reboot();
    }
    if (home) reboot();
    uint8_t edges = pressed;
    pressed = 0u;
    if (edges & DIAG_LEFT) keypad_trace_browse(&trace, -1);
    if (edges & DIAG_RIGHT) keypad_trace_browse(&trace, 1);
    if (edges & DIAG_UP) keypad_trace_init(&trace);
    if (edges & DIAG_DOWN) {
        trace.frozen ^= 1u;
        if (!trace.frozen) trace.selected = trace.total;
    }
    if (edges) dirty = 1u;
    if ((edges & DIAG_ENTER) && !armed) {
        if (!diag_gpio_level(12u)) {
            stage = "NO EXTERNAL VBUS / CANNOT ARM";
            error = HOST_CANCELLED;
        } else {
            reset_keypad();
            int32_t initialized = live_usb_hw_clock_init();
            if (initialized != 0) {
                recovery_error = initialized;
                fail_session(HOST_RECOVERY);
                reboot();
            }
            usb_host_start(&usb);
            error = HOST_OK;
            keypad_host_error = 0;
            armed = 1u;
            restore_failed = 0u;
            previous_speed = 0u;
            stage = "WAITING FOR FULL-SPEED KEYPAD";
        }
    } else if ((edges & DIAG_ENTER) && keypad.ready) {
        stage = nav_on ? "RESTORING ORIGINAL NAV SETTINGS" : "ENABLING TEMPORARY NAV REPORTS";
        screen(0u);
        host_result_t result = nav_on ? keypad_host_restore(&keypad) : keypad_host_navigation(&keypad);
        if (result != HOST_OK)
            fail_session(result);
        else {
            nav_on ^= 1u;
            stage = nav_on ? "LIVE LCD + NAVIGATION" : "LIVE LCD / ORIGINAL NAV SETTINGS";
            dirty = 1u;
        }
    }
    if (armed && !keypad.ready) {
        uint32_t speed = usb_host_speed(&usb), now = diag_time_us();
        if (speed != previous_speed) {
            previous_speed = speed;
            attach_at = now;
            dirty = 1u;
        }
        if (speed && now - attach_at >= 100000u) {
            host_result_t result = speed == 2u ? keypad_host_enumerate(&keypad) : HOST_SPEED_ERROR;
            input_at = diag_time_us();
            if (result != HOST_OK) fail_session(result);
            dirty = 1u;
        }
    }
    if (armed && keypad.ready) {
        host_result_t result = poll_input();
        if (result != HOST_OK) fail_session(result);
    }
    uint32_t now = diag_time_us();
    if (armed && now - frame_at >= 500000u) dirty = 1u;
    if (dirty && now - frame_at >= 100000u) {
        dirty = 0u;
        screen(1u);
        if (armed && error != HOST_OK) fail_session(error);
        frame_at = now;
    }
}

void bare_main(void) {
    keypad_host_error = badger_schedule_bootsel(8000u);
    if (!keypad_host_error) keypad_host_error = nametag_clock_reference();
    if (!keypad_host_error) keypad_host_error = live_usb_hw_clock_init();
    if (keypad_host_error)
        while (1) KEYPAD_HOST_HALT();
    host_io_t io = {.read = read_register, .write = write_register, .now = time_us, .service = service};
    usb_host_init(&usb, &io);
    diag_gpio_init();
    previous_time = renewed = diag_time_us();
    if (!badger_display_init_serviced(service, 0)) reboot();
    display_ready = 1u;
    reset_keypad();
    keypad_trace_init(&trace);
    dirty = 1u;
    while (display_ready) step();
    reboot();
}

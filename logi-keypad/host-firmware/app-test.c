#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "../../src/platform/badger/graphics.h"

static void capture_text(badger_canvas_t *target, int32_t x, int32_t y,
                          const char *value, uint32_t scale, uint16_t color);
static void test_halt(void) __attribute__((noreturn));
#define badger_gfx_text capture_text
#define KEYPAD_HOST_HALT() test_halt()
#include "main.c"
#undef badger_gfx_text

static uint32_t fake_time, registers[0x120u / 4u], dpram[4096u / 4u], written, last_pixels, reboot_delay;
static uint8_t fake_buttons, vbus, allow_halt, fail_renew;
static jmp_buf halt_target;
static unsigned key_labels, release_labels;

static void capture_text(badger_canvas_t *target, int32_t x, int32_t y,
                          const char *value, uint32_t scale, uint16_t color) {
    assert(x >= 0 && y >= 0 && x + (int32_t)(strlen(value) * 6u * scale) <= 320);
    assert(y + (int32_t)(7u * scale) <= 240);
    if (!strcmp(value, "LCD KEY")) ++key_labels;
    if (!strcmp(value, "LCD RELEASE / NO ENCODED IDENTITY")) ++release_labels;
    badger_gfx_text(target, x, y, value, scale, color);
}

uint32_t diag_time_us(void) { fake_time += 200u; return fake_time; }
uint8_t diag_buttons_raw(void) { return fake_buttons; }
uint8_t diag_gpio_level(uint32_t pin) { assert(pin == 12u); return vbus; }
void diag_gpio_init(void) {}
int32_t badger_schedule_bootsel(uint32_t delay) {
    reboot_delay = delay;
    return fail_renew && delay == 8000u ? -4999 : 0;
}
int32_t nametag_clock_reference(void) { return 0; }
int32_t live_usb_hw_clock_init(void) { return 0; }
int badger_display_init_serviced(int (*callback)(void *), void *context) { return callback(context); }
void badger_display_begin(void) { written = 0u; }
void badger_display_write_pixels(const uint16_t *pixels, uint32_t count) {
    assert(pixels == canvas.pixels + written && count == 240u);
    written += count;
    assert(written <= 76800u);
}
void badger_display_end(void) { last_pixels = written; }

static void test_halt(void) {
    assert(allow_halt);
    longjmp(halt_target, 1);
}

static uint32_t fake_read(void *context, uint32_t address) {
    (void)context;
    if (address >= HOST_USB_BASE && address < HOST_USB_BASE + sizeof(registers))
        return registers[(address - HOST_USB_BASE) / 4u];
    assert(address >= HOST_DPRAM_BASE && address < HOST_DPRAM_BASE + sizeof(dpram));
    return dpram[(address - HOST_DPRAM_BASE) / 4u];
}

static void fake_write(void *context, uint32_t address, uint32_t value) {
    (void)context;
    if (address >= HOST_USB_BASE && address < HOST_USB_BASE + sizeof(registers)) {
        uint32_t offset = address - HOST_USB_BASE;
        if (offset == HOST_SIE_CTRL) assert(!(value & ((1u << 11u) | (1u << 16u) | HOST_SOF)));
        if (offset == 0x78u) assert(value == 2u);
        registers[offset / 4u] = value;
        return;
    }
    assert(address >= HOST_DPRAM_BASE && address < HOST_DPRAM_BASE + sizeof(dpram));
    dpram[(address - HOST_DPRAM_BASE) / 4u] = value;
}

static void button(uint8_t mask) {
    fake_buttons = mask;
    step(); fake_time += 21000u; step();
    fake_buttons = 0u;
    step(); fake_time += 21000u; step();
}

int main(void) {
    fake_time = previous_time = renewed = 1000000u;
    host_io_t io = {.read = fake_read, .write = fake_write, .now = time_us, .service = service};
    usb_host_init(&usb, &io);
    reset_keypad();
    keypad_trace_init(&trace);
    display_ready = dirty = 1u;
    step();
    assert(!armed && registers[0x40u / 4u] == 0u && last_pixels == 76800u);
    button(DIAG_ENTER);
    assert(!armed && error == HOST_CANCELLED && registers[0x40u / 4u] == 0u);
    vbus = 1u;
    button(DIAG_ENTER);
    assert(armed && registers[0x40u / 4u] == 3u && usb.sie == HOST_PULLDOWN);
    uint8_t raw[32] = {0x13,0xff,2,0,0,1,1};
    assert(report(0, raw, sizeof(raw), 0u));
    screen(0u);
    assert(key_labels && trace.total == 1u && keypad_trace_selected(&trace)->wire[7] == 2u);
    raw[6] = 0u;
    assert(report(0, raw, sizeof(raw), 0u));
    screen(0u);
    assert(release_labels && last_pixels == 76800u);
    button(DIAG_LEFT); assert(trace.frozen && trace.selected == 1u);
    button(DIAG_DOWN); assert(!trace.frozen && trace.selected == 2u);
    button(DIAG_UP); assert(!trace.total && !trace.count);
    vbus = 0u; step();
    assert(!armed && error == HOST_CANCELLED && registers[0x40u / 4u] == 0u);
    vbus = 1u; button(DIAG_ENTER); assert(armed);
    registers[HOST_SIE_STATUS / 4u] = 1u << 8u;
    step(); fake_time += 100001u; step();
    assert(!armed && error == HOST_SPEED_ERROR);
    error = HOST_OK;
    home = 1u;
    allow_halt = 1u;
    if (!setjmp(halt_target)) step();
    assert(reboot_delay == 10u && registers[0x40u / 4u] == 0u);
    home = 0u;
    fail_renew = 1u;
    fake_time += 500001u;
    if (!setjmp(halt_target)) step();
    assert(recovery_error == -4999 && keypad_host_error == -4999 && reboot_delay == 3000u);
    assert(error == HOST_RECOVERY && last_pixels == 76800u);
    puts("Native app: safe arming/VBUS loss, TFT bounds/raw event rendering, buttons/history,");
    puts("unsupported speed, Home BOOTSEL and visible watchdog failure PASS");
    return 0;
}

#include "board.h"
#include "display.h"
#include "graphics.h"
#include "input.h"
#ifdef CLICKER_CONTROLLER
#include "controller.h"
#endif

#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define INPUT_STATUS(pin) REG(0x40028000u + (pin) * 8u)
#define PAD(pin) REG(0x40038004u + (pin) * 4u)
#define CONTROL(pin) REG(0x40028004u + (pin) * 8u)
#define TIMER_TICKS REG(0x400b000cu)

extern const uint16_t clicker_splash[320u * 240u];
static badger_canvas_t canvas;
static clicker_input_t input;

static void buttons_init(void) {
    for (uint32_t i = 0u; i < CLICKER_BUTTON_COUNT; ++i) {
        uint32_t pin = clicker_button_pins[i];
        REG(0xd0000040u) = 1u << pin;
        PAD(pin) = (1u << 7u) | (1u << 6u) | (1u << 3u) | (1u << 1u);
        CONTROL(pin) = 5u;
    }
}

static uint8_t buttons_read(void) {
    uint8_t pressed = 0u;
    for (uint32_t i = 0u; i < CLICKER_BUTTON_COUNT; ++i) {
        if ((INPUT_STATUS(clicker_button_pins[i]) & (1u << 17u)) == 0u) {
            pressed |= (uint8_t)(1u << i);
        }
    }
    return pressed;
}

#ifndef CLICKER_CONTROLLER
static void present_strip(uint32_t y, uint32_t height) {
    badger_display_begin_region(0u, y, 320u, height);
    for (uint32_t x = 0u; x < 320u; ++x) {
        badger_display_write_pixels(&canvas.pixels[x * 240u + y], height);
    }
    badger_display_end_update();
}

static void show_buttons(uint8_t pressed) {
    static const char *const actions[] = {"PREV", "BLANK", "NEXT", "PREV", "NEXT"};
    badger_gfx_rect(&canvas, 0, 160, 320, 40, 0x0843u);
    for (uint32_t i = 0u; i < CLICKER_BUTTON_COUNT; ++i) {
        int32_t x = (int32_t)(i * 64u + 3u);
        uint16_t color = (pressed & (1u << i)) ? 0x07ffu : 0xffffu;
        badger_gfx_text(&canvas, x, 164, clicker_button_names[i], 1u, color);
        badger_gfx_text(&canvas, x, 181, actions[i], 1u, color);
    }
    present_strip(160u, 40u);
}

static void show_report(uint8_t key) {
    const char *label = key == CLICKER_KEY_PREVIOUS ? "PREV PRESS" :
                        key == CLICKER_KEY_NEXT ? "NEXT PRESS" :
                        key == CLICKER_KEY_BLANK ? "BLANK PRESS" : "ALL KEYS RELEASED";
    badger_gfx_rect(&canvas, 0, 200, 320, 40, 0x0843u);
    badger_gfx_text(&canvas, 4, 204, input.overflow ? "INPUT OVERFLOW - RESET" :
                    label, 1u, input.overflow ? 0xf800u : 0x07ffu);
    badger_gfx_text(&canvas, 4, 224, "LOCAL REPORT TEST - NOT TRANSMITTED", 1u, 0xffffu);
    present_strip(200u, 40u);
}
#endif

void bare_main(void) {
    buttons_init();
    clicker_input_init(&input);
    badger_display_init();
    for (uint32_t i = 0u; i < 320u * 240u; ++i) canvas.pixels[i] = clicker_splash[i];
    badger_display_begin();
    badger_display_write_pixels(canvas.pixels, 320u * 240u);
    badger_display_end();
    uint32_t splash_at = TIMER_TICKS;
    while (TIMER_TICKS - splash_at < 750000u) {}
#ifdef CLICKER_CONTROLLER
    clicker_controller_run(&canvas, &input, buttons_read);
#else
    badger_gfx_rect(&canvas, 0, 124, 320, 116, 0x0843u);
    badger_gfx_text(&canvas, 4, 128, CLICKER_BOARD_NAME " CLICKER", 1u, 0xffffu);
    badger_gfx_text(&canvas, 4, 144, "BUTTON TEST - BLUETOOTH NOT IMPLEMENTED", 1u, 0xffe0u);
    badger_display_begin();
    badger_display_write_pixels(canvas.pixels, 320u * 240u);
    badger_display_end();

    /* This milestone consumes reports locally, never through a radio. */
    clicker_input_set_ready(&input, 1u);
    uint8_t shown = 0xffu;
    uint8_t report[CLICKER_REPORT_SIZE];
    uint32_t report_at = TIMER_TICKS;
    for (;;) {
        uint32_t now = TIMER_TICKS;
        clicker_input_sample(&input, buttons_read(), now);
        if (shown != input.stable) {
            shown = input.stable;
            show_buttons(shown);
        }
        if (now - report_at >= 150000u && clicker_input_peek(&input, report)) {
            show_report(report[2]);
            clicker_input_accept(&input);
            report_at = now;
        }
    }
#endif
}

#include "wifi_bringup_view.h"
#include "diagnostics_logic.h"
#include "display.h"
#include "wifi_board.h"

static badger_canvas_t canvas;
static badger_canvas_t previous;
static uint8_t first_frame = 1u;
static wifi_probe_report_t report;
static diag_buttons_t buttons;

static void show_report(void *context, const wifi_probe_report_t *current) {
    (void)context;
    wifi_bringup_render(current, &canvas);
    for (uint32_t x = 0u; x < 320u; x += 16u) {
        for (uint32_t y = 0u; y < 240u; y += 16u) {
            uint8_t dirty = first_frame;
            for (uint32_t dx = 0u; dx < 16u && !dirty; ++dx) {
                for (uint32_t dy = 0u; dy < 16u; ++dy) {
                    uint32_t i = (x + dx) * 240u + y + dy;
                    if (canvas.pixels[i] != previous.pixels[i]) {
                        dirty = 1u;
                        break;
                    }
                }
            }
            if (!dirty) continue;
            badger_display_begin_region(x, y, 16u, 16u);
            for (uint32_t dx = 0u; dx < 16u; ++dx) {
                for (uint32_t dy = 0u; dy < 16u; ++dy) {
                    uint32_t i = (x + dx) * 240u + y + dy;
                    badger_display_write(canvas.pixels[i]);
                    previous.pixels[i] = canvas.pixels[i];
                }
            }
            badger_display_end_update();
        }
    }
    if (first_frame) badger_display_end();
    first_frame = 0u;
}

void bare_main(void) {
    badger_display_init();
    wifi_board_init();
    const wifi_wire_t *wire = wifi_board_wire();
    report.stage = WIFI_PROBE_IDLE;
    report.result = WIFI_BUS_RADIO_OFF;
    show_report(0, &report);
    wifi_bus_result_t timer_result = wifi_wire_delay(wire, 10u);
    if (timer_result != WIFI_BUS_OK) {
        report.result = timer_result;
        show_report(0, &report);
    }
    uint8_t require_release = 1u;
    while (1) {
        uint8_t raw = wifi_board_buttons();
        uint8_t pressed = diag_buttons_update(&buttons, raw, wire->now_us(wire->context));
        if (require_release) {
            if (raw == 0u && buttons.stable == 0u) require_release = 0u;
            continue;
        }
        if ((pressed & 2u) != 0u) {
            wifi_wire_power_off(wire);
            report.powered = 0u;
            report.result = WIFI_BUS_RADIO_OFF;
            show_report(0, &report);
        } else if ((pressed & 1u) != 0u) {
            wifi_probe_run(wire, &report, show_report, 0);
            require_release = 1u;
        }
    }
}

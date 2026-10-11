#include "wifi_bringup_view.h"
#include "diagnostics_logic.h"

#define BG 0x0863u
#define PANEL 0x1946u
#define WHITE 0xffffu
#define MUTED 0x9cf3u
#define GREEN 0x5ecbu
#define AMBER 0xfda8u
#define RED 0xf9c7u
#define BLUE 0x5d9fu

static void text(badger_canvas_t *canvas, int32_t x, int32_t y,
                  const char *value, uint16_t color) {
    badger_gfx_text(canvas, x, y, value, 1u, color);
}

static void hex_row(badger_canvas_t *canvas, uint32_t row, const char *name,
                     uint32_t value) {
    char buffer[9];
    diag_hex(buffer, value, 8u);
    text(canvas, 10, 88 + (int32_t)row * 14, name, MUTED);
    text(canvas, 172, 88 + (int32_t)row * 14, buffer, WHITE);
}

void wifi_bringup_render(const wifi_probe_report_t *report, badger_canvas_t *canvas) {
    char buffer[12];
    badger_gfx_clear(canvas, BG);
    badger_gfx_rect(canvas, 0, 0, 320, 32, PANEL);
    badger_gfx_text(canvas, 8, 9, "WIFI BUS BRINGUP", 2u, WHITE);
    text(canvas, 10, 40, "M1A / NO SDK / NO LIBC / NO RADIO BLOB", BLUE);
    text(canvas, 10, 57, wifi_probe_stage_name(report->stage), WHITE);
    const char *status = report->result != WIFI_BUS_OK ?
                        wifi_bus_result_name(report->result) :
                        report->stage == WIFI_PROBE_FINISHED ?
                        "BUS CHECKS OK / NOT WIFI READY" : "IN PROGRESS";
    uint16_t color = report->result == WIFI_BUS_RADIO_OFF ? AMBER :
                     report->result != WIFI_BUS_OK ? RED :
                     report->stage == WIFI_PROBE_FINISHED ? GREEN : BLUE;
    text(canvas, 10, 73, status, color);
    hex_row(canvas, 0u, "RESET TEST", report->reset_pattern);
    hex_row(canvas, 1u, "32 BIT TEST", report->normal_pattern);
    hex_row(canvas, 2u, "BUS CONTROL", report->bus_control);
    hex_row(canvas, 3u, "ALP CLOCK CSR", report->clock_csr);
    hex_row(canvas, 4u, "CHIP ID OBSERVED", report->chip_id);
    hex_row(canvas, 5u, "LAST READ", report->last_value);
    text(canvas, 10, 175, "TRIES", MUTED);
    diag_decimal(buffer, report->reset_attempts);
    text(canvas, 46, 175, buffer, WHITE);
    text(canvas, 80, 175, "READS", MUTED);
    diag_decimal(buffer, report->passed_reads);
    text(canvas, 116, 175, buffer, WHITE);
    text(canvas, 146, 175, "MS", MUTED);
    diag_decimal(buffer, (int32_t)(report->elapsed_us / 1000u));
    text(canvas, 166, 175, buffer, WHITE);
    text(canvas, 250, 175, report->powered ? "ON" : "OFF", report->powered ? BLUE : MUTED);
    text(canvas, 10, 191, "NO FIRMWARE LOAD / NO RF / NO ASSOCIATION", AMBER);
    badger_gfx_rect(canvas, 0, 211, 320, 29, PANEL);
    text(canvas, 8, 216, "LEFT GPIO7 RUN OR RETRY / RIGHT GPIO9 OFF", WHITE);
    text(canvas, 8, 229, "REPORT STAGE AND VALUES / BOOTSEL RECOVERY", MUTED);
}

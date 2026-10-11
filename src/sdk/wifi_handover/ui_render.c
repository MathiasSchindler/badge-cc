#include "ui.h"
#include <stdio.h>

#define BG 0x0863u
#define PANEL 0x1946u
#define WHITE 0xffffu
#define MUTED 0x9cf3u
#define GREEN 0x5ecbu
#define AMBER 0xfda8u
#define RED 0xf9c7u
#define BLUE 0x5d9fu

static void text(badger_canvas_t *canvas, int x, int y, const char *value,
                 unsigned scale, uint16_t color) {
    char uppercase[54];
    unsigned i = 0u;
    while (value[i] != '\0' && i + 1u < sizeof(uppercase)) {
        char c = value[i];
        uppercase[i++] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
    }
    uppercase[i] = '\0';
    badger_gfx_text(canvas, x, y, uppercase, scale, color);
}

static void row(badger_canvas_t *canvas, unsigned index, const char *label,
                const char *value, uint16_t color) {
    int y = 47 + (int)index * 19;
    text(canvas, 8, y, label, 1u, MUTED);
    text(canvas, 117, y, value, 1u, color);
}

static void footer(badger_canvas_t *canvas, const char *first, const char *second) {
    badger_gfx_rect(canvas, 0, 211, 320, 29, PANEL);
    text(canvas, 7, 216, first, 1u, WHITE);
    text(canvas, 7, 229, second, 1u, MUTED);
}

static void numeric_row(badger_canvas_t *canvas, unsigned index, const char *label,
                        uint32_t value, const char *unit) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%lu %s", (unsigned long)value, unit);
    row(canvas, index, label, buffer, WHITE);
}

static void paddles(const handover_ui_view_t *view, badger_canvas_t *canvas) {
    const uint16_t colors[] = {GREEN, BLUE, AMBER, RED};
    const int x = 8, y = 38, size = 164, paddle = 30;
    badger_gfx_rect(canvas, x, y, size, size, MUTED);
    badger_gfx_rect(canvas, x + 1, y + 1, size - 2, size - 2, BG);
    for (unsigned slot = 0; slot < EXP_MAX_PLAYERS; ++slot) {
        uint16_t color = (view->wifi.online_mask & (1u << slot)) ? colors[slot] : PANEL;
        uint16_t position = view->wifi.positions[slot];
        if (position > EXP_POSITION_MAX) position = EXP_POSITION_MAX;
        int offset = 4 + (int)position * (size - paddle - 8) / (int)EXP_POSITION_MAX;
        if (slot == 0u) badger_gfx_rect(canvas, x + offset, y + size - 6, paddle, 5, color);
        if (slot == 1u) badger_gfx_rect(canvas, x + size - 6, y + offset, 5, paddle, color);
        if (slot == 2u) badger_gfx_rect(canvas, x + offset, y + 1, paddle, 5, color);
        if (slot == 3u) badger_gfx_rect(canvas, x + 1, y + offset, 5, paddle, color);
    }
    text(canvas, 37, 99, "SHARED STATE", 1u, WHITE);
    text(canvas, 30, 118, "NO BALL PHYSICS", 1u, MUTED);
    const char *const edges[] = {"BOTTOM", "RIGHT", "TOP", "LEFT"};
    for (unsigned slot = 0; slot < EXP_MAX_PLAYERS; ++slot) {
        char line[24];
        snprintf(line, sizeof(line), "%u %s%s", slot + 1u, edges[slot],
                 view->wifi.slot == slot ? " YOU" : "");
        text(canvas, 182, 47 + (int)slot * 34, line, 1u, colors[slot]);
        text(canvas, 182, 60 + (int)slot * 34,
             view->wifi.online_mask & (1u << slot) ? "ONLINE" : "WAIT / STALE",
             1u, view->wifi.online_mask & (1u << slot) ? WHITE : AMBER);
    }
    text(canvas, 182, 187, view->wifi.power_save ? "POWER SAVE ON" : "POWER SAVE OFF", 1u, MUTED);
    footer(canvas, "LEFT RIGHT MOVE YOUR EDGE PADDLE", "HOLD BOTH 1 SECOND FOR METRICS");
}

static void metrics(const handover_ui_view_t *view, badger_canvas_t *canvas) {
    const experiment_metrics_t *metrics = &view->wifi.metrics;
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "P%u TO P%u %s",
             view->wifi.slot + 1u, view->metric_target + 1u,
             metrics->running ? "RUNNING" : "IDLE / RESULTS");
    text(canvas, 8, 40, buffer, 1u, BLUE);
    snprintf(buffer, sizeof(buffer), "%lu / %lu / %lu",
             (unsigned long)metrics->sent, (unsigned long)metrics->received,
             (unsigned long)metrics->lost);
    row(canvas, 1u, "SENT RX LOST", buffer, WHITE);
    numeric_row(canvas, 2u, "LATEST RTT", metrics->latest_us, "US");
    numeric_row(canvas, 3u, "MIN / LOW", metrics->min_us, "US");
    numeric_row(canvas, 4u, "MEDIAN", metrics->median_us, "US");
    snprintf(buffer, sizeof(buffer), "%lu / %lu US",
             (unsigned long)metrics->p95_us, (unsigned long)metrics->p99_us);
    row(canvas, 5u, "P95 / P99", buffer, WHITE);
    numeric_row(canvas, 6u, "MAX RTT", metrics->max_us, "US");
    numeric_row(canvas, 7u, "SEND ERRORS", metrics->local_errors, "");
    text(canvas, 8, 198, view->wifi.status, 1u,
         view->wifi.error_code != 0 ? RED : MUTED);
    footer(canvas, "LEFT RUN OR STOP / RIGHT NEXT TARGET",
           "HOLD BOTH SESSION / PERCENTILES AT END");
}

void handover_ui_render(const handover_ui_view_t *view, badger_canvas_t *canvas) {
    char buffer[40];
    badger_gfx_clear(canvas, BG);
    badger_gfx_rect(canvas, 0, 0, 320, 32, PANEL);
    text(canvas, 8, 9, "BLE TO WIFI LAB", 2u, WHITE);
    if (view->mode == UI_PADDLES) {
        paddles(view, canvas);
        return;
    }
    if (view->mode == UI_METRICS) {
        metrics(view, canvas);
        return;
    }
    switch (view->mode) {
        case UI_STARTUP:
            text(canvas, 8, 55, "STARTING RADIO", 2u, BLUE);
            text(canvas, 8, 87, view->status, 1u, WHITE);
            footer(canvas, "RP2350 / TFT / CYW43439", "NO ACCELEROMETER OR TOUCH REQUIRED");
            break;
        case UI_ROLE:
            text(canvas, 8, 53, "ONE HOST PLUS THREE CLIENTS", 1u, WHITE);
            text(canvas, 8, 83, "LEFT   CREATE SESSION", 2u, GREEN);
            text(canvas, 8, 111, "RIGHT  FIND SESSION", 2u, BLUE);
            text(canvas, 8, 151, "USE ONE BADGE AS HOST", 1u, MUTED);
            text(canvas, 8, 171, "GPIO7 LEFT / GPIO9 RIGHT", 1u, MUTED);
            text(canvas, 8, 191, "UNAUTHENTICATED BLE / CONTROLLED LAB ONLY", 1u, AMBER);
            footer(canvas, "2025 / 2026 / COMPATIBLE TUFTY2350", "NO EXISTING WIFI OR INTERNET NEEDED");
            break;
        case UI_DISCOVERY:
            row(canvas, 0u, "BLE HOSTS", view->status, BLUE);
            snprintf(buffer, sizeof(buffer), "%u OF %u", view->host_count == 0u ? 0u :
                     view->selected_host + 1u, view->host_count);
            row(canvas, 2u, "SELECTION", buffer, WHITE);
            snprintf(buffer, sizeof(buffer), "%08lX", (unsigned long)view->session_id);
            row(canvas, 3u, "SESSION", view->host_count ? buffer : "NONE FOUND", GREEN);
            snprintf(buffer, sizeof(buffer), "%d DBM", view->rssi);
            row(canvas, 4u, "RSSI", view->host_count ? buffer : "WAIT", MUTED);
            text(canvas, 8, 164, "MATCH THIS SESSION CODE ON THE HOST", 1u, AMBER);
            text(canvas, 8, 185, "HOST MUST HAVE ADMISSION OPEN", 1u, MUTED);
            footer(canvas, "LEFT NEXT HOST / RIGHT JOIN SELECTED", "HOLD BOTH 1 SECOND TO CANCEL");
            break;
        case UI_PROGRESS:
            text(canvas, 8, 53, view->host ? "CREATING WIFI" : "JOINING SESSION", 2u, BLUE);
            text(canvas, 8, 88, view->status, 1u, WHITE);
            text(canvas, 8, 110, view->detail, 1u, MUTED);
            text(canvas, 8, 148, "BLE CONFIG THEN LOCAL WIFI UDP", 1u, MUTED);
            text(canvas, 8, 174, "STATIC IP PROVIDED OVER BLE / NO DHCP", 1u, MUTED);
            footer(canvas, "PLEASE WAIT / ERRORS ARE SHOWN HERE", "HOLD BOTH 1 SECOND TO CANCEL");
            break;
        case UI_SESSION:
            snprintf(buffer, sizeof(buffer), "%08lX", (unsigned long)view->session_id);
            row(canvas, 0u, "SESSION", buffer, GREEN);
            row(canvas, 1u, "NETWORK", view->ssid, WHITE);
            snprintf(buffer, sizeof(buffer), "192.168.4.%u", view->wifi.slot + 1u);
            row(canvas, 2u, "LOCAL IP", buffer, WHITE);
            snprintf(buffer, sizeof(buffer), "%u / %s", view->channel,
                     view->wifi.power_save ? "ON" : "OFF");
            row(canvas, 3u, "CHANNEL / PM", buffer, BLUE);
            if (view->host) {
                snprintf(buffer, sizeof(buffer), "%s %lu S", view->admission_open ? "OPEN" : "CLOSED",
                         (unsigned long)view->admission_seconds);
                row(canvas, 4u, "BLE ADMISSION", buffer, view->admission_open ? AMBER : MUTED);
            } else numeric_row(canvas, 4u, "HANDOVER", view->handover_ms, "MS");
            numeric_row(canvas, 5u, "WIFI JOIN", view->wifi.join_ms, "MS");
            row(canvas, 6u, "STATUS", view->status,
                view->wifi.error_code != 0 ? RED : BLUE);
            text(canvas, 8, 184, "PSK AND PLAYER TOKENS ARE NOT DISPLAYED", 1u, MUTED);
            text(canvas, 8, 198, "RESET HOST TO CREATE A NEW SESSION", 1u, MUTED);
            footer(canvas, "LEFT TOGGLE LOCAL WIFI POWER SAVE",
                   view->host ? "RIGHT OPEN BLE 60S / HOLD BOTH FOR PADDLES" :
                                "HOLD BOTH FOR PADDLES");
            break;
        case UI_ERROR:
            text(canvas, 8, 50, "EXPERIMENT ERROR", 2u, RED);
            text(canvas, 8, 85, view->status, 1u, RED);
            text(canvas, 8, 107, view->detail, 1u, AMBER);
            text(canvas, 8, 154, "NO AUTOMATIC HOST MIGRATION", 1u, MUTED);
            if (view->fatal_error) footer(canvas, "FATAL ERROR / RESET BADGE TO RETRY",
                                          "DISPLAY REMAINS AVAILABLE FOR DIAGNOSIS");
            else footer(canvas, "LEFT RETRY / RIGHT RETURN TO ROLES",
                        "HOLD BOTH CANCEL / RESET IF RADIO STALLED");
            break;
        default: break;
    }
}

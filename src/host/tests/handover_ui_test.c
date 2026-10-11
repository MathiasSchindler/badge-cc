#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../sdk/wifi_handover/controls.h"
#include "../../sdk/wifi_handover/ui.h"

static badger_canvas_t canvas;

static void test_controls(void) {
    handover_controls_t controls = {0};
    assert(handover_controls_update(&controls, 1u, 0u) == CONTROL_NONE);
    assert(handover_controls_update(&controls, 0u, 1000u) == CONTROL_NONE);
    assert(handover_controls_update(&controls, 1u, 2000u) == CONTROL_NONE);
    assert(handover_controls_update(&controls, 1u, 22000u) == CONTROL_NONE);
    assert(handover_controls_direction(&controls) == -1);
    assert(handover_controls_update(&controls, 1u, 101999u) == CONTROL_NONE);
    assert(handover_controls_update(&controls, 1u, 102000u) == CONTROL_LEFT);
    assert(handover_controls_update(&controls, 1u, 502000u) == CONTROL_NONE);

    controls = (handover_controls_t){0};
    uint32_t start = UINT32_MAX - 500000u;
    assert(handover_controls_update(&controls, 1u, start) == CONTROL_NONE);
    assert(handover_controls_update(&controls, 1u, start + 20000u) == CONTROL_NONE);
    assert(handover_controls_update(&controls, 3u, start + 30000u) == CONTROL_NONE);
    assert(handover_controls_direction(&controls) == 0);
    assert(handover_controls_update(&controls, 3u, start + 50000u) == CONTROL_NONE);
    assert(handover_controls_update(&controls, 3u, start + 1049999u) == CONTROL_NONE);
    assert(handover_controls_update(&controls, 3u, start + 1050000u) == CONTROL_BOTH);
    assert(handover_controls_update(&controls, 3u, start + 2000000u) == CONTROL_NONE);
    assert(handover_controls_update(&controls, 2u, start + 2010000u) == CONTROL_NONE);
    assert(handover_controls_update(&controls, 2u, start + 2030000u) == CONTROL_NONE);
    assert(handover_controls_direction(&controls) == 0);
    assert(handover_controls_update(&controls, 0u, start + 2050000u) == CONTROL_NONE);
    assert(handover_controls_update(&controls, 0u, start + 2070000u) == CONTROL_NONE);
    assert(!controls.release_required);
    assert(handover_controls_update(&controls, 2u, start + 2090000u) == CONTROL_NONE);
    assert(handover_controls_update(&controls, 2u, start + 2110000u) == CONTROL_NONE);
    assert(handover_controls_update(&controls, 2u, start + 2190000u) == CONTROL_RIGHT);
    assert(handover_controls_direction(&controls) == 1);
}

static void init_view(handover_ui_view_t *view) {
    memset(view, 0, sizeof(*view));
    view->host = true;
    view->admission_open = true;
    view->admission_seconds = 60u;
    view->session_id = 0x12345678u;
    view->channel = 6u;
    view->metric_target = 1u;
    view->wifi.online_mask = 15u;
    view->host_count = 2u;
    view->rssi = -48;
    view->wifi.positions[0] = 0u;
    view->wifi.positions[1] = 1000u;
    view->wifi.positions[2] = 500u;
    view->wifi.positions[3] = 250u;
    snprintf(view->ssid, sizeof(view->ssid), "U26W-12345678");
    snprintf(view->status, sizeof(view->status), "READY");
    view->wifi.metrics.sent = 1000u;
    view->wifi.metrics.received = 999u;
    view->wifi.metrics.lost = 1u;
    view->wifi.metrics.latest_us = 4200u;
    view->wifi.metrics.min_us = 2500u;
    view->wifi.metrics.median_us = 5000u;
    view->wifi.metrics.p95_us = 8200u;
    view->wifi.metrics.p99_us = 11000u;
    view->wifi.metrics.max_us = 58000u;
}

static void test_layout(void) {
    static badger_canvas_t expected;
    handover_ui_view_t view;
    init_view(&view);
    for (unsigned mode = UI_STARTUP; mode <= UI_ERROR; ++mode) {
        view.mode = (handover_ui_mode_t)mode;
        handover_ui_render(&view, &canvas);
    }
    view.mode = UI_PADDLES;
    handover_ui_render(&view, &canvas);
    assert(canvas.pixels[12u * 240u + 196u] == 0x5ecbu);
    assert(canvas.pixels[166u * 240u + 168u] == 0x5d9fu);
    assert(canvas.pixels[75u * 240u + 39u] == 0xfda8u);
    assert(canvas.pixels[9u * 240u + 73u] == 0xf9c7u);
    view.wifi.online_mask = 0u;
    handover_ui_render(&view, &canvas);
    assert(canvas.pixels[12u * 240u + 196u] == 0x1946u);
    for (unsigned i = 0u; i < 4u; ++i) view.wifi.positions[i] = UINT16_MAX;
    handover_ui_render(&view, &canvas);
    view.mode = UI_METRICS;
    view.wifi.error_code = -1;
    snprintf(view.wifi.status, sizeof(view.wifi.status), "target offline");
    handover_ui_render(&view, &canvas);
    snprintf(view.wifi.status, sizeof(view.wifi.status), "TARGET OFFLINE");
    handover_ui_render(&view, &expected);
    assert(memcmp(&canvas, &expected, sizeof(canvas)) == 0);
    unsigned red_pixels = 0u;
    for (unsigned x = 8u; x < 100u; ++x) {
        for (unsigned y = 198u; y < 205u; ++y) {
            if (canvas.pixels[x * 240u + y] == 0xf9c7u) ++red_pixels;
        }
    }
    assert(red_pixels != 0u);
}

static void preview(const char *prefix) {
    handover_ui_view_t view;
    init_view(&view);
    for (unsigned mode = UI_STARTUP; mode <= UI_ERROR; ++mode) {
        char path[1024];
        int length = snprintf(path, sizeof(path), "%s-%u.ppm", prefix, mode);
        assert(length > 0 && (unsigned)length < sizeof(path));
        FILE *file = fopen(path, "wb");
        assert(file != NULL);
        view.mode = (handover_ui_mode_t)mode;
        handover_ui_render(&view, &canvas);
        assert(fprintf(file, "P6\n320 240\n255\n") > 0);
        for (unsigned y = 0; y < 240u; ++y) {
            for (unsigned x = 0; x < 320u; ++x) {
                uint16_t pixel = canvas.pixels[x * 240u + y];
                unsigned char rgb[3] = {
                    (unsigned char)(((pixel >> 11u) & 31u) * 255u / 31u),
                    (unsigned char)(((pixel >> 5u) & 63u) * 255u / 63u),
                    (unsigned char)((pixel & 31u) * 255u / 31u)
                };
                assert(fwrite(rgb, 1u, sizeof(rgb), file) == sizeof(rgb));
            }
        }
        assert(fclose(file) == 0);
    }
}

int main(int argc, char **argv) {
    test_controls();
    test_layout();
    if (argc == 2) preview(argv[1]);
    puts("handover UI: buttons, chord timing, timer wrap, edge mapping, rendering PASS");
    return 0;
}

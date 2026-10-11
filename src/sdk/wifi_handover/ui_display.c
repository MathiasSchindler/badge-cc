#include "ui.h"
#include "../../platform/badger/display.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/time.h"
#include <stdatomic.h>

static mutex_t view_mutex;
static handover_ui_view_t shared_view;
static badger_canvas_t canvas;
static badger_canvas_t previous;
static _Atomic bool panic_requested;

static void display_main(void) {
    bool first = true;
    badger_display_init();
    while (true) {
        handover_ui_view_t view = {0};
        if (atomic_load_explicit(&panic_requested, memory_order_acquire)) {
            view.mode = UI_ERROR;
            view.fatal_error = true;
            const char message[] = "FIRMWARE PANIC / RESET BADGE";
            for (unsigned i = 0u; i < sizeof(message); ++i) view.status[i] = message[i];
        } else {
            if (!mutex_try_enter(&view_mutex, NULL)) {
                sleep_ms(1u);
                continue;
            }
            view = shared_view;
            mutex_exit(&view_mutex);
        }
        handover_ui_render(&view, &canvas);
        for (unsigned x = 0u; x < 320u; x += 16u) {
            for (unsigned y = 0u; y < 240u; y += 16u) {
                bool dirty = first;
                for (unsigned dx = 0u; dx < 16u && !dirty; ++dx) {
                    for (unsigned dy = 0u; dy < 16u; ++dy) {
                        unsigned index = (x + dx) * 240u + y + dy;
                        if (canvas.pixels[index] != previous.pixels[index]) {
                            dirty = true;
                            break;
                        }
                    }
                }
                if (!dirty) continue;
                badger_display_begin_region(x, y, 16u, 16u);
                for (unsigned dx = 0u; dx < 16u; ++dx) {
                    for (unsigned dy = 0u; dy < 16u; ++dy) {
                        unsigned index = (x + dx) * 240u + y + dy;
                        badger_display_write(canvas.pixels[index]);
                        previous.pixels[index] = canvas.pixels[index];
                    }
                }
                badger_display_end_update();
            }
        }
        if (first) badger_display_end();
        first = false;
        sleep_ms(50u);
    }
}

void handover_ui_init(void) {
    mutex_init(&view_mutex);
    shared_view.mode = UI_STARTUP;
    multicore_launch_core1(display_main);
}

void handover_ui_publish(const handover_ui_view_t *view) {
    mutex_enter_blocking(&view_mutex);
    shared_view = *view;
    mutex_exit(&view_mutex);
}

void handover_ui_panic(void) {
    atomic_store_explicit(&panic_requested, true, memory_order_release);
}

#ifndef WIFI_HANDOVER_UI_H
#define WIFI_HANDOVER_UI_H

#include "experiment.h"
#include "../../platform/badger/graphics.h"

typedef enum {
    UI_STARTUP, UI_ROLE, UI_DISCOVERY, UI_PROGRESS, UI_PADDLES,
    UI_METRICS, UI_SESSION, UI_ERROR
} handover_ui_mode_t;

typedef struct {
    handover_ui_mode_t mode;
    bool host;
    bool admission_open;
    bool credentials_ready;
    bool fatal_error;
    uint8_t reserved_mask;
    uint8_t selected_host;
    uint8_t host_count;
    uint8_t metric_target;
    int8_t rssi;
    uint32_t session_id;
    uint32_t admission_seconds;
    uint32_t handover_ms;
    uint8_t channel;
    char ssid[EXP_SSID_SIZE];
    char status[40];
    char detail[40];
    experiment_wifi_view_t wifi;
} handover_ui_view_t;

void handover_ui_init(void);
void handover_ui_publish(const handover_ui_view_t *view);
void handover_ui_panic(void);
void handover_ui_render(const handover_ui_view_t *view, badger_canvas_t *canvas);

#endif

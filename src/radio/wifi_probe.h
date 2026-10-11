#ifndef UNIVERSE26_WIFI_PROBE_H
#define UNIVERSE26_WIFI_PROBE_H

#include "wifi_gspi.h"

typedef enum {
    WIFI_PROBE_IDLE,
    WIFI_PROBE_RESET,
    WIFI_PROBE_TEST16,
    WIFI_PROBE_CONFIG32,
    WIFI_PROBE_TEST32,
    WIFI_PROBE_PADDING,
    WIFI_PROBE_CLOCK,
    WIFI_PROBE_BACKPLANE,
    WIFI_PROBE_FINISHED
} wifi_probe_stage_t;

typedef struct {
    wifi_probe_stage_t stage;
    wifi_bus_result_t result;
    uint32_t reset_pattern;
    uint32_t normal_pattern;
    uint32_t bus_control;
    uint32_t chip_id;
    uint32_t last_value;
    uint32_t elapsed_us;
    uint16_t passed_reads;
    uint8_t reset_attempts;
    uint8_t clock_csr;
    uint8_t powered;
} wifi_probe_report_t;

typedef void (*wifi_probe_progress_t)(void *context, const wifi_probe_report_t *report);

wifi_bus_result_t wifi_probe_run(const wifi_wire_t *wire, wifi_probe_report_t *report,
                                 wifi_probe_progress_t progress, void *context);
const char *wifi_probe_stage_name(wifi_probe_stage_t stage);

#endif

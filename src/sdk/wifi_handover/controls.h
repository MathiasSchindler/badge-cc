#ifndef WIFI_HANDOVER_CONTROLS_H
#define WIFI_HANDOVER_CONTROLS_H

#include "../../tools/diagnostics_logic.h"

typedef enum {
    CONTROL_NONE, CONTROL_LEFT, CONTROL_RIGHT, CONTROL_BOTH
} handover_control_event_t;

typedef struct {
    diag_buttons_t buttons;
    uint32_t pending_at;
    uint32_t chord_at;
    uint8_t pending;
    uint8_t chord;
    uint8_t release_required;
} handover_controls_t;

handover_control_event_t handover_controls_update(handover_controls_t *controls,
                                                   uint8_t raw, uint32_t now_us);
int handover_controls_direction(const handover_controls_t *controls);

#endif

#include "controls.h"

handover_control_event_t handover_controls_update(handover_controls_t *controls,
                                                   uint8_t raw, uint32_t now_us) {
    uint8_t pressed = diag_buttons_update(&controls->buttons, raw & 3u, now_us);
    if (controls->release_required) {
        controls->pending = 0u;
        controls->chord = 0u;
        if (controls->buttons.stable == 0u && controls->buttons.candidate == 0u) {
            controls->release_required = 0u;
        }
        return CONTROL_NONE;
    }
    if (controls->buttons.stable == 3u) {
        controls->pending = 0u;
        if (!controls->chord) {
            controls->chord_at = now_us;
            controls->chord = 1u;
        } else if (now_us - controls->chord_at >= 1000000u) {
            controls->release_required = 1u;
            return CONTROL_BOTH;
        }
        return CONTROL_NONE;
    }
    if (controls->chord) {
        controls->chord = 0u;
        controls->release_required = controls->buttons.stable != 0u;
        return CONTROL_NONE;
    }
    if (pressed != 0u) {
        controls->pending = pressed;
        controls->pending_at = now_us;
    }
    if (controls->pending != 0u && now_us - controls->pending_at >= 80000u) {
        uint8_t pending = controls->pending;
        controls->pending = 0u;
        return pending == 1u ? CONTROL_LEFT : CONTROL_RIGHT;
    }
    return CONTROL_NONE;
}

int handover_controls_direction(const handover_controls_t *controls) {
    if (controls->release_required || controls->buttons.candidate == 3u) return 0;
    return controls->buttons.stable == 1u ? -1 :
           controls->buttons.stable == 2u ? 1 : 0;
}

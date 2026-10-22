#ifndef CLEAN_LIFECYCLE_USB_H
#define CLEAN_LIFECYCLE_USB_H

#include "../../src/usb/live_usb.h"

static inline int clean_lifecycle_snapshot(live_usb_control_t *control, uint32_t generation,
        const uint8_t report[64], const uint8_t trace[WIFI_TRACE_SIZE], uint8_t final) {
    if (!generation || generation != control->probe_generation ||
        !control->configured || control->reboot_requested) return 0;
    if (final) {
        /* The shared USB framework normally permits one immutable snapshot. */
        control->probe_inflight = 1u;
        control->trace_valid = 0u;
    }
    live_usb_probe_finish(control, generation, report);
    live_usb_trace_finish(control, generation, trace);
    return control->report_valid && control->trace_valid;
}

#endif

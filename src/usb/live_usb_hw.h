#ifndef UNIVERSE26_LIVE_USB_HW_H
#define UNIVERSE26_LIVE_USB_HW_H

#include "live_usb.h"

typedef struct {
    uint32_t resets;
    uint32_t setups;
    uint32_t completions;
    uint32_t hardware_errors;
} live_usb_stats_t;

/* Establish USB clocks/reset without enabling either USB role or VBUS. */
int32_t live_usb_hw_clock_init(void);
int32_t live_usb_hw_init(void);
int32_t live_usb_hw_poll(live_usb_control_t *control);
void live_usb_hw_disconnect(void);
const live_usb_stats_t *live_usb_hw_stats(void);

#endif

#ifndef PICO2W_USB_HARDWARE_H
#define PICO2W_USB_HARDWARE_H

#include "control.h"

typedef struct {
    uint32_t resets;
    uint32_t setups;
    uint32_t completions;
    uint32_t hardware_errors;
} usb_stats_t;

int32_t usb_hw_init(void);
int32_t usb_hw_poll(usb_control_t *control);
void usb_hw_disconnect(void);
const usb_stats_t *usb_hw_stats(void);

#endif

#ifndef PICO2W_USB_CONTROL_H
#define PICO2W_USB_CONTROL_H

#include "report.h"

/* Lab-only identifiers; not a production allocation. */
#define USB_VID 0xcafeu
#define USB_PID 0x4027u
#define USB_SUBMIT 0x50u
#define USB_REPORT 0x51u
#define USB_BOOTSEL 0x52u
#define USB_INFO 0x53u
#define USB_PACKET_SIZE 64u

typedef enum {
    USB_ACTION_NONE,
    USB_ACTION_IN,
    USB_ACTION_OUT,
    USB_ACTION_ADDRESS,
    USB_ACTION_STALL
} usb_action_t;

typedef enum {
    USB_PHASE_IDLE,
    USB_PHASE_DATA_IN,
    USB_PHASE_DATA_OUT,
    USB_PHASE_STATUS_IN,
    USB_PHASE_STATUS_OUT,
    USB_PHASE_STALL
} usb_phase_t;

typedef struct {
    usb_phase_t phase;
    usb_action_t action;
    uint32_t build_id;
    uint32_t chip_id[2];
    uint32_t requests;
    uint32_t rejected;
    uint8_t configured;
    uint8_t address;
    uint8_t pending_address;
    uint8_t address_pending;
    uint8_t pending_configuration;
    uint8_t configuration_pending;
    uint8_t reboot_pending;
    uint8_t reboot_requested;
    uint8_t report_pending;
    uint8_t report_valid;
    uint16_t length;
    uint8_t data[USB_PACKET_SIZE];
    uint8_t report[USB_REPORT_SIZE];
    char serial[17];
} usb_control_t;

void usb_control_init(usb_control_t *control, uint32_t build_id,
                       const uint32_t chip_id[2]);
void usb_control_reset(usb_control_t *control);
void usb_control_setup(usb_control_t *control, const uint8_t setup[8]);
void usb_control_complete(usb_control_t *control, uint8_t in,
                           const uint8_t *data, uint16_t length);

#endif

#ifndef U26_USB_HOST_H
#define U26_USB_HOST_H

#include <stdint.h>

#define HOST_USB_BASE 0x50110000u
#define HOST_DPRAM_BASE 0x50100000u
#define HOST_SIE_CTRL 0x4cu
#define HOST_SIE_STATUS 0x50u
#define HOST_NAK_POLL 0x6cu
#define HOST_EPX_BUFFER 0x80u
#define HOST_EPX_CONTROL 0x100u
#define HOST_EPX_DATA 0x180u
#define HOST_PULLDOWN (1u << 15u)
#define HOST_SOF (1u << 9u)
#define HOST_START 1u
#define HOST_STOP (1u << 4u)
#define HOST_RESET (1u << 13u)
#define HOST_STOP_ON_NAK (1u << 26u)
#define HOST_STOPPED_NAK (1u << 27u)
#define HOST_COMPLETE (1u << 18u)
#define HOST_FULL (1u << 15u)
#define HOST_LAST (1u << 14u)
#define HOST_PID (1u << 13u)
#define HOST_AVAILABLE (1u << 10u)
#define HOST_ERRORS 0xaf800000u

typedef enum {
    HOST_OK = 0, HOST_NAK = 1, HOST_ARGUMENT = -4301, HOST_TIMEOUT = -4302,
    HOST_DETACHED = -4303, HOST_STALL = -4304, HOST_BUS_ERROR = -4305,
    HOST_CANCELLED = -4306, HOST_BUFFER_ERROR = -4307, HOST_STOP_TIMEOUT = -4308,
    HOST_SPEED_ERROR = -4309, HOST_DESCRIPTOR = -4310, HOST_WRONG_DEVICE = -4311,
    HOST_HID_ERROR = -4312, HOST_REPLY_TIMEOUT = -4313, HOST_HID_REPLY = -4314,
    HOST_HUB = -4315, HOST_RECOVERY = -4316
} host_result_t;

typedef enum { HOST_SETUP, HOST_IN, HOST_OUT } host_packet_kind_t;

typedef struct {
    void *context;
    uint32_t (*read)(void *context, uint32_t address);
    void (*write)(void *context, uint32_t address, uint32_t value);
    uint32_t (*now)(void *context);
    int (*service)(void *context);
} host_io_t;

typedef struct {
    host_io_t io;
    uint32_t sie, last_status, last_buffer, packets, naks;
    uint8_t address, ep0_size, stop_failed;
} usb_host_t;

void usb_host_init(usb_host_t *host, const host_io_t *io);
void usb_host_off(usb_host_t *host);
/* Call after hardware reset; role configuration does not supply clocks or VBUS. */
void usb_host_start(usb_host_t *host);
uint32_t usb_host_speed(usb_host_t *host);
host_result_t usb_host_wait(usb_host_t *host, uint32_t us);
host_result_t usb_host_reset(usb_host_t *host);
host_result_t usb_host_packet(usb_host_t *host, host_packet_kind_t kind,
                              uint8_t endpoint, uint8_t interrupt, uint8_t pid,
                              uint8_t *data, uint16_t length, uint16_t *received);
host_result_t usb_host_control(usb_host_t *host, const uint8_t setup[8],
                               uint8_t *data, uint16_t capacity, uint16_t *received);
const char *usb_host_error(host_result_t result);

#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <libusb.h>
#include "../../src/usb/live_usb.h"

#define UNIVERSE26_USB_LIVE_TRANSPORT_H
static int present, configured, config_error, info_ok, protocol_ok, requests, sets;
static int token;
static libusb_device_handle *find_badge(libusb_context *context, const char *serial) {
    (void)context; (void)serial;
    return present ? (libusb_device_handle *)&token : NULL;
}
static int transfer(libusb_device_handle *device, uint8_t type, uint8_t request,
                    uint8_t *data, uint16_t length) {
    (void)device;
    assert(type == 0xc0u && request == LIVE_USB_INFO && length == 12u);
    ++requests;
    const uint8_t info[] = {'U','2','6','L',1u,0u,64u,0u,1u,2u,3u,4u};
    memcpy(data, info, sizeof(info));
    if (!protocol_ok) data[0] = 0u;
    return info_ok;
}
int libusb_init(libusb_context **context) { *context = NULL; return 0; }
void libusb_exit(libusb_context *context) { (void)context; }
void libusb_close(libusb_device_handle *device) { (void)device; }
int libusb_get_configuration(libusb_device_handle *device, int *configuration) {
    (void)device; *configuration = configured; return config_error;
}
int libusb_set_configuration(libusb_device_handle *device, int configuration) {
    (void)device; assert(configuration == 1); ++sets; return config_error;
}
const char *libusb_error_name(int error) { (void)error; return "injected USB error"; }

#define main usb_ready_main
#include "usb_ready.c"
#undef main

int main(void) {
    char *args[] = {"usb-ready", "0123456789ABCDEF"};
    assert(usb_ready_main(1, args) == 2);
    assert(usb_ready_main(2, args) == 75);
    present = 1;
    config_error = -1;
    assert(usb_ready_main(2, args) == 75 && requests == 0);
    config_error = 0;
    assert(usb_ready_main(2, args) == 75 && requests == 1 && sets == 1);
    info_ok = 1;
    assert(usb_ready_main(2, args) == 1);
    protocol_ok = 1;
    assert(usb_ready_main(2, args) == 0);
    configured = 1;
    int before = sets;
    assert(usb_ready_main(2, args) == 0 && sets == before);
    puts("Read-only USB protocol/configuration readiness controls pass");
    return 0;
}

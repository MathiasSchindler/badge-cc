#include <stdlib.h>
#include "../../src/host/tools/usb_live_transport.h"

int main(int argc, char **argv) {
    if (argc != 2 || strlen(argv[1]) != 16u ||
        strspn(argv[1], "0123456789ABCDEF") != 16u) {
        fputs("Usage: usb-ready SERIAL\n", stderr);
        return 2;
    }
    libusb_context *context = NULL;
    int status = libusb_init(&context);
    if (status) {
        fprintf(stderr, "USB initialization: %s\n", libusb_error_name(status));
        return 1;
    }
    libusb_device_handle *device = find_badge(context, argv[1]);
    int result = 75;
    if (!device) {
        fprintf(stderr, "USB application not enumerated: %s\n", argv[1]);
    } else {
        int configuration = 0;
        status = libusb_get_configuration(device, &configuration);
        if (!status && configuration != 1) status = libusb_set_configuration(device, 1);
        if (status) fprintf(stderr, "USB configuration: %s\n", libusb_error_name(status));
        else {
            uint8_t info[12];
            const uint8_t prefix[] = {'U', '2', '6', 'L', 1u, 0u, 64u, 0u};
            if (transfer(device, 0xc0u, LIVE_USB_INFO, info, sizeof(info))) {
                if (memcmp(info, prefix, sizeof(prefix))) {
                    fputs("USB application protocol mismatch\n", stderr);
                    result = 1;
                } else {
                    printf("USB application ready: %s build=%02x%02x%02x%02x\n",
                           argv[1], info[11], info[10], info[9], info[8]);
                    result = 0;
                }
            }
        }
        libusb_close(device);
    }
    libusb_exit(context);
    return result;
}

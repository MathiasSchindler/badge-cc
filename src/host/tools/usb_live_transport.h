#ifndef UNIVERSE26_USB_LIVE_TRANSPORT_H
#define UNIVERSE26_USB_LIVE_TRANSPORT_H

#include <libusb.h>
#include <stdio.h>
#include <string.h>
#include "../../usb/live_usb.h"

static libusb_device_handle *find_badge(libusb_context *context, const char *serial) {
    libusb_device **devices;
    ssize_t count = libusb_get_device_list(context, &devices);
    if (count < 0) {
        fprintf(stderr, "USB enumeration: %s\n", libusb_error_name((int)count));
        return NULL;
    }
    libusb_device_handle *selected = NULL;
    for (ssize_t i = 0; i < count; ++i) {
        struct libusb_device_descriptor descriptor;
        int status = libusb_get_device_descriptor(devices[i], &descriptor);
        if (status != 0 || descriptor.idVendor != LIVE_USB_VID || descriptor.idProduct != LIVE_USB_PID) continue;
        libusb_device_handle *candidate;
        status = libusb_open(devices[i], &candidate);
        if (status != 0) {
            fprintf(stderr, "Cannot open live-report device: %s\n", libusb_error_name(status));
            continue;
        }
        unsigned char actual[64];
        int length = libusb_get_string_descriptor_ascii(candidate, descriptor.iSerialNumber, actual, sizeof(actual));
        if (length == 16 && memcmp(actual, serial, 16u) == 0) {
            if (selected != NULL) {
                fputs("Ambiguous serial: more than one live-report device matches\n", stderr);
                libusb_close(candidate);
                libusb_close(selected);
                selected = NULL;
                break;
            }
            selected = candidate;
        } else {
            if (length < 0) fprintf(stderr, "Cannot read live-report serial: %s\n", libusb_error_name(length));
            libusb_close(candidate);
        }
    }
    libusb_free_device_list(devices, 1);
    return selected;
}

static int transfer(libusb_device_handle *device, uint8_t type, uint8_t request,
                     uint8_t *data, uint16_t length) {
    int result = libusb_control_transfer(device, type, request, 0u, 0u, data, length, 1000u);
    if (result != length) {
        fprintf(stderr, "USB request %02x: expected %u bytes, got %d (%s)\n",
                request, length, result, result < 0 ? libusb_error_name(result) : "short transfer");
        return 0;
    }
    return 1;
}

#endif

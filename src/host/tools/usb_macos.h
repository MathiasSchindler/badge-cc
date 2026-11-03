#ifndef UNIVERSE26_USB_MACOS_H
#define UNIVERSE26_USB_MACOS_H

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static inline int usb_macos_number(io_service_t device, CFStringRef key, int expected) {
    CFTypeRef property = IORegistryEntryCreateCFProperty(device, key, kCFAllocatorDefault, 0);
    int value = 0;
    int match = property && CFGetTypeID(property) == CFNumberGetTypeID() &&
        CFNumberGetValue(property, kCFNumberIntType, &value) && value == expected;
    if (property) CFRelease(property);
    return match;
}

static inline int usb_macos_serial(io_service_t device, const char *serial) {
    CFTypeRef property = IORegistryEntryCreateCFProperty(device, CFSTR("USB Serial Number"),
                                                        kCFAllocatorDefault, 0);
    char value[128];
    int match = property && CFGetTypeID(property) == CFStringGetTypeID() &&
        CFStringGetCString(property, value, sizeof(value), kCFStringEncodingUTF8) &&
        !strcmp(value, serial);
    if (property) CFRelease(property);
    return match;
}

static inline IOUSBDeviceInterface500 **usb_macos_open(int vendor, int product, const char *serial) {
    io_iterator_t iterator = IO_OBJECT_NULL;
    kern_return_t result = IOServiceGetMatchingServices(kIOMainPortDefault,
                                                        IOServiceMatching("IOUSBHostDevice"), &iterator);
    if (result != KERN_SUCCESS) {
        fprintf(stderr, "USB enumeration failed: 0x%08x.\n", (unsigned)result);
        return NULL;
    }
    io_service_t selected = IO_OBJECT_NULL, device;
    unsigned matches = 0u;
    while ((device = IOIteratorNext(iterator))) {
        if (usb_macos_number(device, CFSTR("idVendor"), vendor) &&
            usb_macos_number(device, CFSTR("idProduct"), product) &&
            usb_macos_serial(device, serial)) {
            ++matches;
            if (!selected) { selected = device; IOObjectRetain(selected); }
        }
        IOObjectRelease(device);
    }
    IOObjectRelease(iterator);
    if (matches != 1u) {
        fprintf(stderr, "Expected one USB %04x:%04x device with serial %s; found %u.\n",
                vendor, product, serial, matches);
        if (selected) IOObjectRelease(selected);
        return NULL;
    }
    IOCFPlugInInterface **plugin = NULL;
    SInt32 score;
    result = IOCreatePlugInInterfaceForService(selected, kIOUSBDeviceUserClientTypeID,
                                              kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(selected);
    if (result != KERN_SUCCESS || !plugin) {
        fprintf(stderr, "USB plugin creation failed: 0x%08x.\n", (unsigned)result);
        return NULL;
    }
    IOUSBDeviceInterface500 **usb = NULL;
    HRESULT query = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID500),
                                              (LPVOID *)&usb);
    (*plugin)->Release(plugin);
    if (query != S_OK || !usb) {
        fprintf(stderr, "USB interface query failed: 0x%08x.\n", (unsigned)query);
        return NULL;
    }
    IOReturn opened = (*usb)->USBDeviceOpen(usb);
    if (opened != kIOReturnSuccess) {
        fprintf(stderr, "USB open failed (not seizing): 0x%08x.\n", (unsigned)opened);
        (*usb)->Release(usb);
        return NULL;
    }
    return usb;
}

static inline int usb_macos_transfer(IOUSBDeviceInterface500 **usb, uint8_t input,
                                     uint8_t request, uint8_t *wire, uint16_t length) {
    IOUSBDevRequestTO packet = {.bmRequestType = input ? 0xc0 : 0x40,
        .bRequest = request, .wLength = length, .pData = wire,
        .noDataTimeout = 1000, .completionTimeout = 1000};
    IOReturn result = (*usb)->DeviceRequestTO(usb, &packet);
    if (result != kIOReturnSuccess || packet.wLenDone != length) {
        fprintf(stderr, "USB request 0x%02x failed: 0x%08x, %u/%u bytes.\n",
                request, (unsigned)result, (unsigned)packet.wLenDone, length);
        return 0;
    }
    return 1;
}

static inline int usb_macos_close(IOUSBDeviceInterface500 **usb) {
    IOReturn closed = (*usb)->USBDeviceClose(usb);
    (*usb)->Release(usb);
    if (closed != kIOReturnSuccess) {
        fprintf(stderr, "USB close failed: 0x%08x.\n", (unsigned)closed);
        return 0;
    }
    return 1;
}

#endif

#ifndef COMPILER_USB_MOCK_H
#define COMPILER_USB_MOCK_H
#include <stdint.h>
#include <sys/types.h>
#include <sys/time.h>
typedef struct libusb_context { int active; } libusb_context;
typedef struct libusb_device { int role; } libusb_device;
typedef struct libusb_device_handle { int role; } libusb_device_handle;
struct libusb_device_descriptor { uint16_t idVendor, idProduct; uint8_t iSerialNumber; };
#define LIBUSB_ERROR_PIPE (-9)
#define LIBUSB_ERROR_NO_DEVICE (-4)
int libusb_init(libusb_context **context);
void libusb_exit(libusb_context *context);
ssize_t libusb_get_device_list(libusb_context *context, libusb_device ***devices);
void libusb_free_device_list(libusb_device **devices, int unref);
int libusb_get_device_descriptor(libusb_device *device, struct libusb_device_descriptor *descriptor);
int libusb_open(libusb_device *device, libusb_device_handle **handle);
int libusb_get_string_descriptor_ascii(libusb_device_handle *handle, uint8_t index, unsigned char *data, int length);
int libusb_get_configuration(libusb_device_handle *handle, int *configuration);
int libusb_set_configuration(libusb_device_handle *handle, int configuration);
int libusb_claim_interface(libusb_device_handle *handle, int interface);
int libusb_release_interface(libusb_device_handle *handle, int interface);
void libusb_close(libusb_device_handle *handle);
libusb_device *libusb_get_device(libusb_device_handle *handle);
libusb_device *libusb_ref_device(libusb_device *device);
void libusb_unref_device(libusb_device *device);
int libusb_handle_events_timeout(libusb_context *context, struct timeval *timeout);
int libusb_control_transfer(libusb_device_handle *handle, uint8_t type, uint8_t request, uint16_t value,
                           uint16_t index, unsigned char *data, uint16_t length, unsigned timeout);
const char *libusb_error_name(int error);
#endif

#ifndef U26_KEYPAD_HOST_H
#define U26_KEYPAD_HOST_H

#include "usb_host.h"
#include "../keypad-input.h"

typedef struct {
    uint8_t configuration, interface, input, output, input_size, output_size;
    uint8_t input_interval, output_interval;
    uint16_t report_size;
} keypad_endpoints_t;

typedef struct {
    usb_host_t *usb;
    keypad_endpoints_t endpoints;
    void *context;
    int (*report)(void *context, const uint8_t *raw, uint16_t length, uint8_t feature);
    void (*progress)(void *context, const char *stage);
    uint32_t report_crc;
    uint8_t input_pid, output_pid, feature, software_id, changed, original[2], ready;
    uint8_t hid_error;
} keypad_host_t;

host_result_t keypad_host_configuration(const uint8_t *bytes, uint16_t length,
                                        keypad_endpoints_t *endpoints);
int keypad_host_known_hid(const uint8_t *bytes, uint16_t length);
host_result_t keypad_host_enumerate(keypad_host_t *keypad);
host_result_t keypad_host_poll(keypad_host_t *keypad);
host_result_t keypad_host_navigation(keypad_host_t *keypad);
host_result_t keypad_host_restore(keypad_host_t *keypad);

#endif

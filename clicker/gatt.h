#ifndef CLICKER_GATT_H
#define CLICKER_GATT_H
#include <stdint.h>
#define CLICKER_GATT_MTU 185u
#define CLICKER_DEVICE_NAME "GitHub Badge Presentation Clicker"
#define CLICKER_ADVERTISED_NAME "GitHub Badge Clicker"
typedef struct {
    uint16_t mtu, report_ccc, boot_ccc, service_ccc;
    uint8_t encrypted, authenticated, protocol, suspended, leds, dirty, report[8];
    uint32_t errors;
} clicker_gatt_t;
void clicker_gatt_init(clicker_gatt_t *gatt);
uint32_t clicker_gatt_request(clicker_gatt_t *gatt, const uint8_t *request,
                              uint32_t length, uint8_t response[CLICKER_GATT_MTU]);
uint16_t clicker_gatt_notification_handle(const clicker_gatt_t *gatt);
#endif

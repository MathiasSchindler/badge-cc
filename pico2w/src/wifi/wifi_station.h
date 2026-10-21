#ifndef PICO2W_WIFI_STATION_H
#define PICO2W_WIFI_STATION_H

#include "types.h"

typedef enum {
    WIFI_STATION_IDLE = 0,
    WIFI_STATION_ASSET_CHECK = 1,
    WIFI_STATION_BUS_PROBE = 2,
    WIFI_STATION_FIRMWARE_LOAD = 3,
    WIFI_STATION_REGULATORY = 4,
    WIFI_STATION_CONFIGURE = 5,
    WIFI_STATION_JOIN = 6,
    WIFI_STATION_CONNECTED = 7,
    WIFI_STATION_FAILED = 8
} wifi_station_stage_t;

typedef enum {
    WIFI_STATION_ERROR_NONE = 0,
    WIFI_STATION_ERROR_ARGUMENT = 1,
    WIFI_STATION_ERROR_ASSET = 2,
    WIFI_STATION_ERROR_BUS = 3,
    WIFI_STATION_ERROR_CONTROLLER = 4,
    WIFI_STATION_ERROR_TIMEOUT = 5,
    WIFI_STATION_ERROR_PROTOCOL = 6,
    WIFI_STATION_ERROR_DISCONNECTED = 7
} wifi_station_error_t;

typedef struct {
    uint32_t step;
    uint32_t address;
    uint32_t expected;
    uint32_t actual;
    uint32_t completed;
    uint32_t command;
    uint32_t event_type;
    uint32_t event_status;
    uint32_t event_reason;
} wifi_station_diagnostic_t;

/* Non-secret operation metadata; latched on failure, never packet/key contents. */
const wifi_station_diagnostic_t *wifi_station_diagnostic(void);

/* Callback services USB/status during blocking initialization and bus waits.
 * Do not reenter Wi-Fi from it. Registration survives station initialization. */
void wifi_station_set_service(void (*callback)(void *context), void *context);
/* Returns 1 when joining has started, not when WPA2 authorization has completed.
 * Failures remain latched; recovery requires explicit initialization or reboot. */
int wifi_station_init(const char *ssid, const char *passphrase);
void wifi_station_service(void);
/* Service polls one packet; every 500 ms it performs bounded CDC link queries.
 * It is not strictly nonblocking (CDC waits may take seconds on a failed radio).
 * Registered host callbacks keep USB/status serviced during these waits.
 * Status getters below are pure and may be called by that callback. */
int wifi_station_read(void *buffer, unsigned int capacity, unsigned int *length);
int wifi_station_send(const void *buffer, unsigned int length);
int wifi_station_get_mac(unsigned char mac[6]);
int wifi_station_link_up(void);
wifi_station_stage_t wifi_station_stage(void);
wifi_station_error_t wifi_station_error(void);
uint32_t wifi_station_controller_status(void);

#endif

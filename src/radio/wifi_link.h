#ifndef UNIVERSE26_WIFI_LINK_H
#define UNIVERSE26_WIFI_LINK_H

#include <stdint.h>

#define WIFI_LINK_TEST_ID 4u
#define WIFI_LINK_AP 0u
#define WIFI_LINK_STATION 1u

typedef enum {
    WIFI_LINK_IDLE, WIFI_LINK_FIRMWARE, WIFI_LINK_CONFIGURE,
    WIFI_LINK_START, WIFI_LINK_WAIT_PEER, WIFI_LINK_CONNECTED,
    WIFI_LINK_FAILED
} wifi_link_stage_t;

typedef enum {
    WIFI_LINK_OK, WIFI_LINK_RADIO_ERROR, WIFI_LINK_BAD_REPLY,
    WIFI_LINK_JOIN_TIMEOUT, WIFI_LINK_BAD_CONFIG
} wifi_link_error_t;

typedef struct {
    uint8_t role;
    wifi_link_stage_t stage;
    wifi_link_error_t error;
    uint8_t authorized;
    uint32_t last_command;
    uint32_t controller_status;
    uint32_t peer_count;
    uint8_t mac[6];
    uint8_t peer[6];
} wifi_link_report_t;

typedef struct {
    void *context;
    int (*control)(void *context, uint32_t command, uint8_t set, uint8_t interface,
                    const uint8_t *request, uint32_t request_size,
                    uint8_t *reply, uint32_t capacity, uint32_t *reply_size);
} wifi_link_ops_t;

typedef struct {
    wifi_link_report_t report;
    wifi_link_ops_t ops;
    char ssid[24];
    char password[33];
    uint8_t buffer[512];
    uint8_t reply[512];
} wifi_link_t;

void wifi_link_init(wifi_link_t *link, const wifi_link_ops_t *ops, uint8_t role,
                    const uint8_t nonce[16], const uint8_t mac[6]);
int wifi_link_configure(wifi_link_t *link);
int wifi_link_poll(wifi_link_t *link);
const char *wifi_link_stage_name(wifi_link_stage_t stage);
const char *wifi_link_error_name(wifi_link_error_t error);

#endif

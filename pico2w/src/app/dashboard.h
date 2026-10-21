#ifndef PICO2W_DASHBOARD_H
#define PICO2W_DASHBOARD_H

#include "net/http.h"

#define DASHBOARD_PACKETS 8u

typedef struct {
    uint32_t frequency_hz;
    uint32_t sf;
    uint32_t bandwidth_khz;
} dashboard_config_t;

typedef struct {
    uint32_t sequence;
    uint32_t uptime_ms;
    uint32_t frequency_hz;
    uint32_t sf;
    uint32_t bandwidth_khz;
    int32_t rssi_qdb;
    int32_t snr_qdb;
    uint32_t length;
    uint8_t data[255];
} dashboard_packet_t;

typedef struct {
    uint32_t build_id;
    uint32_t uptime_ms;
    uint32_t received;
    uint32_t crc_errors;
    uint32_t header_errors;
    uint32_t overwritten;
    uint32_t network_drops;
    uint32_t retransmits;
    int32_t wifi_error;
    int32_t lora_error;
    uint32_t lora_state;
    uint32_t lora_status;
    uint32_t lora_command;
    uint32_t lora_device_errors;
    uint32_t lora_polls;
    uint32_t lora_response;
    char ip[16];
    dashboard_config_t config;
    uint32_t packet_count;
    dashboard_packet_t packets[DASHBOARD_PACKETS];
    void *context;
    int32_t (*configure)(void *context, const dashboard_config_t *config);
    uint8_t json[HTTP_RESPONSE_MAX - 1024u];
} dashboard_t;

uint32_t dashboard_http(void *context, const http_request_t *request,
                         uint8_t *response, uint32_t capacity);
int dashboard_parse_config(const uint8_t *data, uint32_t length,
                             dashboard_config_t *config);

#endif

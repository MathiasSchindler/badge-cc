#ifndef PICO2W_HEALTH_H
#define PICO2W_HEALTH_H

#include "../lora/link_test.h"
#include "../net/http.h"

#define HEALTH_MAGIC 0x484c3250u
#define HEALTH_MIN_MS 60000u
#define HEALTH_HEARTBEAT_MS 300000u

typedef struct {
    uint32_t wifi_stage, wifi_error, dhcp_state, network_error, ip, connected;
    uint32_t radio_state;
    int32_t radio_error;
} health_snapshot_t;

typedef struct {
    health_snapshot_t current;
    uint32_t id[2], sequence, sent, last_attempt_ms, last_success_ms, last_rx_ms;
    uint32_t rx_sequence, peer_seen, boot_ms, uptime_ms;
    int32_t tx_error, restore_error;
    uint8_t pending, attempted;
    uint8_t json[4096];
} health_t;

void health_init(health_t *h, const uint32_t id[2], uint32_t now);
void health_update(health_t *h, const health_snapshot_t *snapshot);
int health_due(const health_t *h, uint32_t now);
void health_payload(health_t *h, uint32_t now, uint8_t payload[LINK_PACKET_SIZE]);
uint32_t health_http(health_t *h, const http_request_t *request, uint8_t *out, uint32_t capacity);

#endif

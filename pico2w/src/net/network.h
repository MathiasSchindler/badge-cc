#ifndef PICO2W_NETWORK_H
#define PICO2W_NETWORK_H

#include "http.h"

#define NET_CLIENTS 4u
#define NET_FRAME_MAX 1514u

typedef struct {
    uint8_t state;
    uint8_t mac[6];
    uint32_t ip;
    uint16_t port;
    uint16_t mss;
    uint16_t window;
    uint32_t receive_next;
    uint32_t send_next;
    uint32_t send_acked;
    uint32_t sent_at;
    uint32_t active_at;
    uint8_t attempts;
    uint8_t flags;
    uint16_t outstanding;
    uint32_t request_size;
    uint32_t response_size;
    uint32_t response_offset;
    uint8_t request[HTTP_REQUEST_MAX];
    uint8_t response[HTTP_RESPONSE_MAX];
} net_client_t;

typedef struct {
    void *context;
    int (*send)(void *context, const uint8_t *frame, uint32_t length);
    uint32_t (*http)(void *context, const http_request_t *request,
                      uint8_t *response, uint32_t capacity);
} net_ops_t;

typedef struct {
    net_ops_t ops;
    uint8_t mac[6];
    uint8_t dhcp_state;
    uint8_t dhcp_attempts;
    uint8_t error;
    uint32_t ip;
    uint32_t server;
    uint32_t offered;
    uint32_t xid;
    uint32_t lease_at;
    uint32_t lease_ms;
    uint32_t dhcp_at;
    uint32_t now;
    uint32_t isn;
    uint32_t received;
    uint32_t dropped;
    uint32_t transmitted;
    uint32_t retransmits;
    uint32_t http_requests;
    uint16_t packet_id;
    char address[16];
    uint8_t tx[NET_FRAME_MAX];
    net_client_t clients[NET_CLIENTS];
} network_t;

void network_init(network_t *net, const net_ops_t *ops, const uint8_t mac[6],
                    uint32_t seed, uint32_t now);
void network_input(network_t *net, const uint8_t *frame, uint32_t length);
void network_tick(network_t *net, uint32_t now);

#endif

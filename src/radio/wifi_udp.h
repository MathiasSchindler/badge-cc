#ifndef UNIVERSE26_WIFI_UDP_H
#define UNIVERSE26_WIFI_UDP_H

#include <stdint.h>

#define WIFI_UDP_SAMPLES 100u
#define WIFI_UDP_FRAME_MAX 1514u
#define WIFI_UDP_PORT 30526u
#define WIFI_UDP_INTERVAL_TICKS 10000u
#define WIFI_UDP_TIMEOUT_TICKS 100000u

typedef enum {
    WIFI_UDP_IDLE, WIFI_UDP_ARP, WIFI_UDP_ECHO, WIFI_UDP_DONE, WIFI_UDP_FAILED
} wifi_udp_stage_t;

typedef enum {
    WIFI_UDP_OK, WIFI_UDP_BAD_CONFIG, WIFI_UDP_ARP_TIMEOUT,
    WIFI_UDP_TX_ERROR, WIFI_UDP_DEADLINE, WIFI_UDP_RADIO_ERROR
} wifi_udp_error_t;

typedef enum {
    WIFI_UDP_NO_FRAME, WIFI_UDP_SEND_FRAME, WIFI_UDP_DROPPED, WIFI_UDP_ERROR
} wifi_udp_result_t;

typedef enum {
    WIFI_UDP_DROP_NONE, WIFI_UDP_DROP_LENGTH, WIFI_UDP_DROP_ADDRESS,
    WIFI_UDP_DROP_PROTOCOL, WIFI_UDP_DROP_CHECKSUM, WIFI_UDP_DROP_PAYLOAD,
    WIFI_UDP_DROP_STALE
} wifi_udp_drop_t;

typedef struct {
    wifi_udp_stage_t stage;
    wifi_udp_error_t error;
    wifi_udp_drop_t last_drop;
    uint8_t role;
    uint8_t mac[6];
    uint8_t peer_mac[6];
    uint8_t arp_ready;
    uint8_t pending;
    uint32_t ip;
    uint32_t peer_ip;
    uint32_t sent;
    uint32_t received;
    uint32_t lost;
    uint32_t echoed;
    uint32_t dropped;
    uint32_t duplicates;
    uint32_t arp_tx;
    uint32_t arp_rx;
    uint32_t last_rtt;
    uint32_t rtt_sum;
    uint32_t rtt_min;
    uint32_t rtt_max;
    uint32_t samples[WIFI_UDP_SAMPLES];
    uint32_t started;
    uint32_t next_due;
    uint32_t deadline;
    uint32_t pending_sequence;
    uint32_t pending_ticks;
    uint32_t arp_attempts;
    uint32_t arp_expires;
    uint32_t peer_sequence;
} wifi_udp_t;

void wifi_udp_init(wifi_udp_t *net, uint8_t role, const uint8_t mac[6],
                    const uint8_t peer_mac[6], uint32_t now);
wifi_udp_result_t wifi_udp_step(wifi_udp_t *net, uint32_t now,
                                uint8_t frame[WIFI_UDP_FRAME_MAX], uint32_t *length);
wifi_udp_result_t wifi_udp_receive(wifi_udp_t *net, uint32_t now,
                                   const uint8_t *frame, uint32_t length,
                                   uint8_t reply[WIFI_UDP_FRAME_MAX], uint32_t *reply_length);
const char *wifi_udp_stage_name(wifi_udp_stage_t stage);
const char *wifi_udp_error_name(wifi_udp_error_t error);

#endif

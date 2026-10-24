#ifndef CLEAN_NETWORK_H
#define CLEAN_NETWORK_H

#include "../../src/shared/bytes.h"

#define CLEAN_NETWORK_MODE 33u
#define CLEAN_NETWORK_AP_MODE 4u
#define CLEAN_NETWORK_MAGIC 0x324e5342u
#define CLEAN_NETWORK_DETAIL 1280u
#define CLEAN_NETWORK_AP_DETAIL 32u
#define CLEAN_NETWORK_SLOTS 4u
#define CLEAN_NETWORK_MTU 1514u
#define CLEAN_NETWORK_PORT 30526u
#define CLEAN_NETWORK_PAYLOAD_MAX (CLEAN_NETWORK_MTU - 42u)
#define CLEAN_NETWORK_METRICS_MAGIC 0x314d3842u
#define CLEAN_NETWORK_EVENTS 4u

typedef struct {
    uint32_t magic, state, error, seconds, elapsed_us;
    uint32_t generated, acknowledged, delivered, replies, retries, lost;
    uint32_t duplicates, corrupt, stale, arp_tx, arp_rx;
    uint32_t tx_full, rx_full, tx_high, rx_high, tx_packets, rx_packets;
    uint32_t abandoned, idle_periods, service_max_us, tx_hash, rx_hash;
    uint8_t mac[6], peer[6], session[6], role, bound;
    uint32_t epoch;
    uint32_t large_generated, large_acknowledged, large_delivered, latency_max_us, latency_samples;
    uint32_t metrics_magic, ack_attempts[4], latency_total_low, latency_total_high;
    uint32_t latency_histogram[4], service_gaps, radio_timeouts, event_count, events[CLEAN_NETWORK_EVENTS];
} clean_network_report_t;
_Static_assert(sizeof(clean_network_report_t) == 224u, "Network metrics preserve the original 152-byte prefix");
_Static_assert(CLEAN_NETWORK_AP_DETAIL + sizeof(clean_network_report_t) <= 256u,
               "Network AP evidence must fit the vendor trace");
_Static_assert(CLEAN_NETWORK_DETAIL + sizeof(clean_network_report_t) <= 1616u,
               "Network evidence must precede controller fault storage");

typedef struct {
    uint8_t frame[CLEAN_NETWORK_SLOTS][CLEAN_NETWORK_MTU];
    uint32_t length[CLEAN_NETWORK_SLOTS], head, count;
} clean_network_queue_t;

typedef struct {
    clean_network_report_t report;
    clean_network_queue_t tx, rx;
    uint8_t scratch[CLEAN_NETWORK_MTU];
    uint32_t started, last_service, arp_at, send_at, pending_at;
    uint32_t sequence, peer_sequence, pending, attempts;
    uint8_t neighbor, idle;
} clean_network_t;

void clean_network_init(clean_network_t *n, uint8_t role, const uint8_t mac[6],
                        const uint8_t peer[6], const uint8_t session[6],
                        uint32_t epoch, uint32_t seconds, uint32_t now);
void clean_network_reset(clean_network_t *n, uint32_t epoch, uint32_t now);
int clean_network_input(clean_network_t *n, const uint8_t *frame, uint32_t length);
void clean_network_step(clean_network_t *n, uint32_t now);
const uint8_t *clean_network_output(const clean_network_t *n, uint32_t *length);
void clean_network_sent(clean_network_t *n);
void clean_network_radio_timeout(clean_network_t *n, uint32_t now);
int clean_network_valid(const clean_network_report_t *r);
#endif

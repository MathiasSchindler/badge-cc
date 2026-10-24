#ifndef CLEAN_NETWORK_JSON_H
#define CLEAN_NETWORK_JSON_H
#include <stdio.h>
#include "network.h"
static inline void clean_network_print(const clean_network_report_t *r) {
    printf("{\"magic\":%u,\"state\":%u,\"error\":%u,\"seconds\":%u,\"elapsed_us\":%u,"
           "\"generated\":%u,\"acknowledged\":%u,\"delivered\":%u,\"replies\":%u,"
           "\"retries\":%u,\"lost\":%u,\"duplicates\":%u,\"corrupt\":%u,\"stale\":%u,"
           "\"arp_tx\":%u,\"arp_rx\":%u,\"tx_full\":%u,\"rx_full\":%u,"
           "\"tx_high\":%u,\"rx_high\":%u,\"tx_packets\":%u,\"rx_packets\":%u,"
           "\"abandoned\":%u,\"idle_periods\":%u,\"service_max_us\":%u,"
           "\"tx_hash\":%u,\"rx_hash\":%u,\"epoch\":%u,\"role\":%u,\"bound\":%u,"
           "\"large_generated\":%u,\"large_acknowledged\":%u,\"large_delivered\":%u,"
           "\"latency_max_us\":%u,\"latency_samples\":%u,\"mac\":\"",
           r->magic,r->state,r->error,r->seconds,r->elapsed_us,r->generated,r->acknowledged,
           r->delivered,r->replies,r->retries,r->lost,r->duplicates,r->corrupt,r->stale,
           r->arp_tx,r->arp_rx,r->tx_full,r->rx_full,r->tx_high,r->rx_high,r->tx_packets,
           r->rx_packets,r->abandoned,r->idle_periods,r->service_max_us,
           r->tx_hash,r->rx_hash,r->epoch,r->role,r->bound,r->large_generated,
           r->large_acknowledged,r->large_delivered,r->latency_max_us,r->latency_samples);
    for (uint32_t i=0u;i<6u;++i) printf("%s%02x",i?":":"",r->mac[i]);
    fputs("\",\"peer\":\"",stdout);
    for (uint32_t i=0u;i<6u;++i) printf("%s%02x",i?":":"",r->peer[i]);
    fputs("\",\"session\":\"",stdout);
    for (uint32_t i=0u;i<6u;++i) printf("%02x",r->session[i]);
    fputs("\",\"diagnostics\":{\"available\":",stdout);
    if (r->metrics_magic != CLEAN_NETWORK_METRICS_MAGIC) { fputs("false}}",stdout); return; }
    printf("true,\"ack_attempts\":[%u,%u,%u,%u],\"latency_total_us\":%llu,"
           "\"latency_histogram\":[%u,%u,%u,%u],\"service_gaps\":%u,"
           "\"event_count\":%u,\"events_truncated\":%s,\"radio_timeouts\":",
           r->ack_attempts[0],r->ack_attempts[1],r->ack_attempts[2],r->ack_attempts[3],
           (unsigned long long)(((uint64_t)r->latency_total_high << 32u) | r->latency_total_low),
           r->latency_histogram[0],r->latency_histogram[1],r->latency_histogram[2],
           r->latency_histogram[3],r->service_gaps,r->event_count,
           r->event_count > CLEAN_NETWORK_EVENTS ? "true" : "false");
    if (r->role) printf("%u",r->radio_timeouts); else fputs("null",stdout);
    fputs(",\"events\":[",stdout);
    uint32_t first = r->event_count > CLEAN_NETWORK_EVENTS ? r->event_count - CLEAN_NETWORK_EVENTS : 0u;
    for (uint32_t i=first;i<r->event_count;++i) {
        uint32_t e=r->events[i%CLEAN_NETWORK_EVENTS];
        printf("%s{\"kind\":%u,\"elapsed_ms\":%u}",i==first?"":",",e>>30u,e&0x3fffffffu);
    }
    fputs("]}}",stdout);
}
#endif

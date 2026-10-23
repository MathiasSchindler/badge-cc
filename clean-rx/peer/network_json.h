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
    fputs("\"}",stdout);
}
#endif

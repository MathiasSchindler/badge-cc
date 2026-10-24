#ifndef CLEAN_RUNTIME_STATE_H
#define CLEAN_RUNTIME_STATE_H

#include "../cyw43_hw.h"
#include "../mac.h"
#include "../tx_diag.h"
#include "../ht_observer.h"

/* Flat, typed owner namespaces preserve the original static-storage layout. */
#define TRANSPORT(field) clean_transport_##field
#define STATION(field) clean_station_##field
#define PUBLICATION(field) clean_publication_##field
#define AGGREGATE(field) clean_aggregate_##field

/* LOOP fields have automatic lifetime in cyw43_main, never static storage. */
#define LOOP(field) clean_loop_##field

#endif

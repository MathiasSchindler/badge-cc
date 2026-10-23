#ifndef PICO2W_EXPERIMENT_H
#define PICO2W_EXPERIMENT_H

#include "health.h"

#define EXPERIMENT_RUNS 8u
#define EXPERIMENT_MAGIC 0x3142414cu

typedef struct {
    uint32_t sender[2], token, expected, mask, duplicates, last_ms;
    int32_t rssi_qdb, snr_qdb;
    link_profile_t profile;
} experiment_run_t;

typedef struct {
    link_profile_t profile;
    uint32_t revision, next_tx_ms, last_tx_ms, guard_ms, test_sequence;
    uint32_t queued, batch_count, ordinal, token, attempts, sent;
    uint32_t run_head, run_count, malformed, packet_cursor;
    int32_t config_error, tx_error, restore_error;
    experiment_run_t runs[EXPERIMENT_RUNS];
    uint8_t json[6144];
} experiment_t;

void experiment_init(experiment_t *lab);
int32_t experiment_configure(experiment_t *lab, sx1262_receiver_t *rx);
uint32_t experiment_wait(const experiment_t *lab, uint32_t now);
void experiment_tick(experiment_t *lab, health_t *health, sx1262_receiver_t *rx, uint32_t now);
void experiment_observe(experiment_t *lab, const sx1262_rx_packet_t *p, uint32_t now);
void experiment_charge(experiment_t *lab, uint32_t now);
uint32_t experiment_http(experiment_t *lab, health_t *health, sx1262_receiver_t *rx,
                         const http_request_t *request, uint8_t *out, uint32_t capacity);

#endif

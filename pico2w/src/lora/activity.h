#ifndef PICO2W_LORA_ACTIVITY_H
#define PICO2W_LORA_ACTIVITY_H

#include "receiver.h"

#define ACTIVITY_CHANNELS 65u
#define ACTIVITY_EVENTS 32u
#define ACTIVITY_DWELL_MS 5000u
#define ACTIVITY_SAMPLE_MS 10u
#define ACTIVITY_WARMUP_MS 500u
#define ACTIVITY_THRESHOLD_QDB 24
#define ACTIVITY_END_QDB 12

typedef struct {
    uint32_t frequency_hz, visits, observed_ms, samples, max_gap_ms;
    uint32_t bursts, packets, crc_errors, last_start_ms, last_start_observed_ms;
    uint8_t has_start, baseline_valid;
    int32_t baseline_qdb, peak_qdb;
} activity_channel_t;

typedef struct {
    uint32_t sequence, frequency_hz, sf, bandwidth_khz, profile;
    uint32_t start_ms, duration_ms, interval_ms, blind_ms;
    uint32_t samples, max_gap_ms, packets, crc_errors, first_packet, last_packet;
    uint8_t kind; /* 1 energy burst, 2 packet without an active energy burst */
    uint8_t truncated; /* dwell/stop/error ended the observation */
    uint8_t has_interval, baseline_valid;
    int32_t baseline_qdb, peak_qdb;
} activity_event_t;

typedef struct {
    uint8_t running, burst_active;
    int32_t error, restore_error;
    uint32_t session, sweep, index, dwell_at_ms, last_tick_ms, sample_at_ms;
    uint32_t sample_count, max_gap_ms, last_packet, next_sequence, head, count, overwritten;
    uint32_t below_at_ms;
    uint8_t below;
    uint32_t histogram[128];
    activity_event_t burst;
    sx1262_rx_config_t saved;
    activity_channel_t channels[ACTIVITY_CHANNELS];
    activity_event_t events[ACTIVITY_EVENTS];
} activity_t;

uint32_t activity_frequency(uint32_t index);
int32_t activity_start(activity_t *survey, sx1262_receiver_t *rx, uint32_t now_ms);
int32_t activity_stop(activity_t *survey, sx1262_receiver_t *rx, uint32_t now_ms);
void activity_tick(activity_t *survey, sx1262_receiver_t *rx, uint32_t now_ms);
const activity_event_t *activity_event(const activity_t *survey, uint32_t newest_index);

#endif

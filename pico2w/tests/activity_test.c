#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lora/activity.h"

static activity_t survey;
static sx1262_receiver_t radio;
static sx1262_rx_packet_t latest;
static uint32_t now, tunes, reads;
static int32_t rssi, tune_error, rssi_error;
static uint8_t have_packet;

int32_t sx1262_receiver_configure(sx1262_receiver_t *rx, const sx1262_rx_config_t *config) {
    ++tunes;
    if (tune_error) { int32_t e = tune_error; tune_error = 0; return e; }
    rx->active = *config;
    rx->state = SX1262_RX_RUNNING;
    return 0;
}

int32_t sx1262_receiver_rssi(sx1262_receiver_t *rx, int32_t *qdb) {
    (void)rx;
    ++reads;
    if (rssi_error) return rssi_error;
    *qdb = rssi;
    return 0;
}

const sx1262_rx_packet_t *sx1262_receiver_packet(const sx1262_receiver_t *rx, uint32_t index) {
    (void)rx;
    return have_packet && index == 0 ? &latest : NULL;
}

static void tick(uint32_t elapsed, int32_t level) {
    now += elapsed; rssi = level;
    activity_tick(&survey, &radio, now);
}

static void fresh(uint32_t start) {
    memset(&survey, 0, sizeof(survey));
    memset(&radio, 0, sizeof(radio));
    memset(&latest, 0, sizeof(latest));
    tunes = reads = have_packet = 0;
    tune_error = rssi_error = 0;
    now = start; rssi = -448;
    radio.state = SX1262_RX_RUNNING;
    radio.active = (sx1262_rx_config_t){869525000u, 11u, 250u, 1u};
    assert(activity_start(&survey, &radio, now) == 0);
    assert(survey.running && radio.active.frequency_hz == 863000000u);
    assert(radio.active.spreading_factor == 7 && radio.active.bandwidth_khz == 125);
    assert(activity_start(&survey, &radio, now) == SX1262_RX_BUSY_ERROR);
}

static void warm(void) {
    for (uint32_t i = 0; i < 50; ++i) tick(10, -448);
    assert(survey.channels[survey.index].baseline_valid);
    assert(survey.channels[survey.index].baseline_qdb == -448);
}

static void quiet(void) {
    for (uint32_t i = 0; i < 4; ++i) tick(10, -448);
}

static void packets(void) {
    fresh(0); warm();
    tick(10, -424); /* exactly 6 dB starts the burst */
    assert(survey.burst_active);
    latest.sequence = 1;
    latest.frequency_hz = radio.active.frequency_hz;
    latest.timestamp_ms = now + 10;
    latest.rssi_qdb = -420;
    have_packet = 1;
    tick(10, -408);
    assert(survey.burst.packets == 1 && survey.burst.first_packet == 1);
    tick(10, -440); /* below 3 dB */
    tick(10, -432); /* >=3 dB cancels termination */
    assert(survey.burst_active);
    quiet();
    const activity_event_t *e = activity_event(&survey, 0);
    assert(e && e->kind == 1 && e->start_ms == 510 && e->duration_ms == 40);
    assert(e->peak_qdb == -408 && e->baseline_qdb == -448 && !e->truncated);
    assert(e->packets == 1 && e->samples == 8 && !e->has_interval);
    tick(10, -420); quiet();
    e = activity_event(&survey, 0);
    assert(e->has_interval && e->interval_ms == 80 && e->blind_ms == 0);
    assert(survey.channels[0].bursts == 2);
    latest.sequence = 2; latest.flags = SX1262_RX_CRC_ERROR; latest.timestamp_ms = now + 10;
    tick(10, -448);
    e = activity_event(&survey, 0);
    assert(e->kind == 2 && e->crc_errors == 1 && e->duration_ms == 0 && e->last_packet == 2);
    assert(survey.channels[0].packets == 1 && survey.channels[0].crc_errors == 1);
    tick(10, -448);
    assert(survey.count == 3); /* no repeated packet */
    assert(activity_stop(&survey, &radio, now) == 0 && !survey.running);
    assert(radio.active.frequency_hz == 869525000u && radio.active.profile == 1);
}

static void coverage(void) {
    fresh(0);
    for (uint32_t i = 0; i < ACTIVITY_CHANNELS; ++i) {
        uint32_t f = activity_frequency(i);
        assert(f >= 863000000 && f <= 870000000);
        for (uint32_t j = 0; j < i; ++j) assert(f != activity_frequency(j));
        assert(radio.active.frequency_hz == f);
        if (f == 869525000) assert(radio.active.profile == 1 && radio.active.spreading_factor == 11 &&
                                  radio.active.bandwidth_khz == 250);
        tick(ACTIVITY_DWELL_MS, -448);
    }
    assert(activity_frequency(ACTIVITY_CHANNELS) == 0);
    assert(survey.sweep == 1 && survey.index == 0 && radio.active.spreading_factor == 8);
    for (uint32_t sf = 8; sf <= 12; ++sf) {
        assert(radio.active.spreading_factor == sf);
        for (uint32_t i = 0; i < ACTIVITY_CHANNELS; ++i) tick(ACTIVITY_DWELL_MS, -448);
    }
    assert(survey.sweep == 6 && radio.active.spreading_factor == 7);
    assert(survey.channels[0].observed_ms == 30000);
}

static void limits_and_faults(void) {
    fresh(0xfffffe00u); warm();
    tick(1, -400); assert(!survey.burst_active);
    tick(9, -424); assert(survey.burst_active);
    tick(100, -400); assert(survey.burst.max_gap_ms == 100);
    assert(activity_stop(&survey, &radio, now) == 0);
    assert(activity_event(&survey, 0)->truncated && activity_event(&survey, 0)->duration_ms == 100);
    assert(activity_start(&survey, &radio, now) == 0 && survey.session == 2 && survey.count == 0);
    warm();
    for (uint32_t i = 0; i < 40; ++i) { tick(10, -420); quiet(); }
    assert(survey.count == ACTIVITY_EVENTS && survey.overwritten == 8);
    assert(activity_event(&survey, 0)->sequence == 40 && activity_event(&survey, 31)->sequence == 9);
    assert(!activity_event(&survey, ACTIVITY_EVENTS));
    tune_error = SX1262_SPI_ERROR;
    tick(ACTIVITY_DWELL_MS, -448);
    assert(!survey.running && survey.error == SX1262_SPI_ERROR && !survey.restore_error);
    assert(radio.active.frequency_hz == 869525000);
    fresh(0); warm(); tick(10, -420);
    rssi_error = SX1262_SPI_ERROR; tick(10, -420);
    assert(!survey.running && survey.error == SX1262_SPI_ERROR && activity_event(&survey, 0)->truncated);
    fresh(0);
    tune_error = SX1262_SPI_ERROR;
    assert(activity_stop(&survey, &radio, now) == SX1262_SPI_ERROR && survey.restore_error == SX1262_SPI_ERROR);
    memset(&survey, 0, sizeof(survey)); radio.state = SX1262_RX_IDLE;
    assert(activity_start(&survey, &radio, now) == SX1262_RX_STATE_ERROR);
    fresh(0); activity_stop(&survey, &radio, now);
    tune_error = SX1262_SPI_ERROR;
    assert(activity_start(&survey, &radio, now) == SX1262_SPI_ERROR && !survey.running);
    assert(!survey.restore_error && radio.active.frequency_hz == 869525000);
}

static void off_frequency_intervals(void) {
    fresh(0); warm(); tick(10, -420); quiet();
    uint32_t first = activity_event(&survey, 0)->start_ms;
    tick(ACTIVITY_DWELL_MS - now, -448);
    for (uint32_t i = 1; i < ACTIVITY_CHANNELS; ++i) tick(ACTIVITY_DWELL_MS, -448);
    warm(); tick(10, -420); quiet();
    const activity_event_t *e = activity_event(&survey, 0);
    assert(e->frequency_hz == 863000000 && e->has_interval);
    assert(e->interval_ms == e->start_ms - first);
    assert(e->blind_ms == (ACTIVITY_CHANNELS - 1u) * ACTIVITY_DWELL_MS);
}

static void training_and_boundaries(void) {
    fresh(100);
    latest.sequence = 1; latest.frequency_hz = radio.active.frequency_hz;
    latest.timestamp_ms = now + 10; latest.rssi_qdb = -470; have_packet = 1;
    tick(10, -440);
    assert(activity_event(&survey, 0)->kind == 2 && !activity_event(&survey, 0)->baseline_valid);
    /* Exactly the weakest 20% determine the training floor despite busy samples. */
    for (uint32_t i = 1; i < 50; ++i) tick(10, i < 11 ? -480 : -440);
    assert(survey.channels[0].baseline_qdb == -480);
    tick(10, -456);
    assert(survey.burst_active);
    latest.sequence = 2; latest.timestamp_ms = now + 5000;
    tick(5000, -456);
    assert(activity_event(&survey, 0)->truncated && activity_event(&survey, 0)->packets == 1);
    assert(survey.channels[0].observed_ms == 5510 && survey.channels[0].max_gap_ms == 5000);
    uint32_t before = survey.channels[1].observed_ms;
    now += 17;
    assert(activity_stop(&survey, &radio, now) == 0 && survey.channels[1].observed_ms == before + 17);
}

int main(void) {
    packets(); coverage(); limits_and_faults(); off_frequency_intervals(); training_and_boundaries();
    puts("PASS: activity thresholds, hysteresis, sampled timing, packet correlation, coverage/SF, recurrence, bounds and restore");
    return 0;
}

#include "activity.h"

uint32_t activity_frequency(uint32_t index) {
    static const uint32_t extra[] = {
        867100000u, 867300000u, 867700000u, 867900000u,
        868100000u, 868300000u, 868950000u, 869525000u
    };
    if (index < 57u) return 863000000u + index * 125000u;
    return index < ACTIVITY_CHANNELS ? extra[index - 57u] : 0u;
}

static void store(activity_t *s, activity_event_t *event) {
    event->sequence = s->next_sequence++;
    if (!s->next_sequence) s->next_sequence = 1u;
    uint8_t *to = (uint8_t *)&s->events[s->head];
    const uint8_t *from = (const uint8_t *)event;
    for (uint32_t i = 0u; i < sizeof(*event); ++i) to[i] = from[i];
    s->head = (s->head + 1u) % ACTIVITY_EVENTS;
    if (s->count < ACTIVITY_EVENTS) ++s->count;
    else ++s->overwritten;
}

const activity_event_t *activity_event(const activity_t *s, uint32_t index) {
    if (index >= s->count) return 0;
    return &s->events[(s->head + ACTIVITY_EVENTS - 1u - index) % ACTIVITY_EVENTS];
}

static void new_event(activity_event_t *e, const sx1262_receiver_t *rx, uint32_t now) {
    uint8_t *bytes = (uint8_t *)e;
    for (uint32_t i = 0u; i < sizeof(*e); ++i) bytes[i] = 0u;
    e->frequency_hz = rx->active.frequency_hz;
    e->sf = rx->active.spreading_factor;
    e->bandwidth_khz = rx->active.bandwidth_khz;
    e->profile = rx->active.profile;
    e->start_ms = now;
}

static void end_burst(activity_t *s, uint32_t now, uint8_t truncated) {
    if (!s->burst_active) return;
    s->burst.duration_ms = now - s->burst.start_ms;
    s->burst.truncated = truncated;
    store(s, &s->burst);
    s->burst_active = s->below = 0u;
}

static int32_t tune(activity_t *s, sx1262_receiver_t *rx, uint32_t now) {
    sx1262_rx_config_t config;
    config.frequency_hz = activity_frequency(s->index);
    uint8_t mesh = config.frequency_hz == SX1262_RX_MESHTASTIC_FREQUENCY_HZ;
    config.profile = mesh ? SX1262_RX_PROFILE_MESHTASTIC : SX1262_RX_PROFILE_LORAWAN;
    config.spreading_factor = mesh ? 11u : 7u + s->sweep % 6u;
    config.bandwidth_khz = mesh ? 250u : 125u;
    int32_t error = sx1262_receiver_configure(rx, &config);
    if (error != 0) return error;
    s->dwell_at_ms = s->last_tick_ms = s->sample_at_ms = now;
    s->sample_count = s->max_gap_ms = 0u;
    s->below = 0u;
    for (uint32_t i = 0u; i < 128u; ++i) s->histogram[i] = 0u;
    activity_channel_t *c = &s->channels[s->index];
    c->baseline_valid = 0u;
    ++c->visits;
    return 0;
}

int32_t activity_stop(activity_t *s, sx1262_receiver_t *rx, uint32_t now) {
    if (!s->running) return s->restore_error;
    s->channels[s->index].observed_ms += now - s->last_tick_ms;
    s->last_tick_ms = now;
    end_burst(s, now, 1u);
    s->running = 0u;
    s->restore_error = sx1262_receiver_configure(rx, &s->saved);
    return s->restore_error;
}

int32_t activity_start(activity_t *s, sx1262_receiver_t *rx, uint32_t now) {
    if (s->running) return SX1262_RX_BUSY_ERROR;
    if (rx->state != SX1262_RX_RUNNING) return SX1262_RX_STATE_ERROR;
    uint32_t session = s->session + 1u;
    /* No libc; clear the owned state, including the previous run's history. */
    uint8_t *bytes = (uint8_t *)s;
    for (uint32_t i = 0u; i < sizeof(*s); ++i) bytes[i] = 0u;
    s->session = session;
    s->saved = rx->active;
    s->next_sequence = 1u;
    const sx1262_rx_packet_t *p = sx1262_receiver_packet(rx, 0u);
    if (p) s->last_packet = p->sequence;
    for (uint32_t i = 0u; i < ACTIVITY_CHANNELS; ++i) {
        s->channels[i].frequency_hz = activity_frequency(i);
        s->channels[i].peak_qdb = -512;
    }
    s->running = 1u;
    s->last_tick_ms = now;
    s->error = tune(s, rx, now);
    if (s->error != 0) (void)activity_stop(s, rx, now);
    return s->error;
}

static void packet(activity_t *s, const sx1262_receiver_t *rx, uint32_t now) {
    const sx1262_rx_packet_t *p = sx1262_receiver_packet(rx, 0u);
    if (!p || p->sequence == s->last_packet) return;
    s->last_packet = p->sequence;
    if (p->frequency_hz != rx->active.frequency_hz ||
        p->timestamp_ms - s->dwell_at_ms > now - s->dwell_at_ms) return;
    activity_channel_t *c = &s->channels[s->index];
    uint8_t crc = (p->flags & SX1262_RX_CRC_ERROR) != 0u;
    if (crc) ++c->crc_errors;
    else ++c->packets;
    activity_event_t e;
    new_event(&e, rx, p->timestamp_ms);
    activity_event_t *target = s->burst_active ? &s->burst : &e;
    if (!s->burst_active) {
        e.kind = 2u;
        e.peak_qdb = p->rssi_qdb;
        e.baseline_qdb = c->baseline_valid ? c->baseline_qdb : 0;
        e.baseline_valid = c->baseline_valid;
    }
    if (crc) ++target->crc_errors;
    else ++target->packets;
    if (!target->first_packet) target->first_packet = p->sequence;
    target->last_packet = p->sequence;
    if (!s->burst_active) store(s, &e);
}

void activity_tick(activity_t *s, sx1262_receiver_t *rx, uint32_t now) {
    if (!s->running) return;
    activity_channel_t *c = &s->channels[s->index];
    c->observed_ms += now - s->last_tick_ms;
    s->last_tick_ms = now;
    packet(s, rx, now);
    uint32_t gap = now - s->sample_at_ms;
    if (gap > s->max_gap_ms) s->max_gap_ms = gap;
    if (gap > c->max_gap_ms) c->max_gap_ms = gap;
    if (s->burst_active && gap > s->burst.max_gap_ms) s->burst.max_gap_ms = gap;
    if (rx->state != SX1262_RX_RUNNING) s->error = rx->last_error ? rx->last_error : SX1262_RX_STATE_ERROR;
    if (s->error != 0) {
        (void)activity_stop(s, rx, now);
        return;
    }
    if (now - s->dwell_at_ms >= ACTIVITY_DWELL_MS) {
        end_burst(s, now, 1u);
        if (++s->index == ACTIVITY_CHANNELS) { s->index = 0u; ++s->sweep; }
        s->error = tune(s, rx, now);
        if (s->error != 0) (void)activity_stop(s, rx, now);
        return;
    }
    if (gap < ACTIVITY_SAMPLE_MS) return;
    int32_t qdb = 0;
    s->error = sx1262_receiver_rssi(rx, &qdb);
    if (s->error != 0) { (void)activity_stop(s, rx, now); return; }
    s->sample_at_ms = now;
    ++c->samples;
    ++s->sample_count;
    if (gap > s->max_gap_ms) s->max_gap_ms = gap;
    if (gap > c->max_gap_ms) c->max_gap_ms = gap;
    if (qdb > c->peak_qdb) c->peak_qdb = qdb;
    if (!c->baseline_valid) {
        uint32_t bin = (uint32_t)(-qdb) / 4u;
        if (bin > 127u) bin = 127u;
        ++s->histogram[bin];
        if (now - s->dwell_at_ms < ACTIVITY_WARMUP_MS || s->sample_count < 8u) return;
        /* The lower RSSI quintile: bins run from strong to weak. */
        uint32_t total = 0u, wanted = (s->sample_count + 4u) / 5u;
        for (uint32_t i = 128u; i > 0u; --i) {
            total += s->histogram[i - 1u];
            if (total >= wanted) { c->baseline_qdb = -(int32_t)(i - 1u) * 4; break; }
        }
        c->baseline_valid = 1u;
        return;
    }
    if (!s->burst_active && qdb - c->baseline_qdb >= ACTIVITY_THRESHOLD_QDB) {
        new_event(&s->burst, rx, now);
        s->burst.kind = 1u;
        s->burst.baseline_qdb = c->baseline_qdb;
        s->burst.baseline_valid = 1u;
        s->burst.peak_qdb = qdb;
        s->burst.max_gap_ms = gap;
        if (c->has_start) {
            s->burst.has_interval = 1u;
            s->burst.interval_ms = now - c->last_start_ms;
            uint32_t observed = c->observed_ms - c->last_start_observed_ms;
            s->burst.blind_ms = s->burst.interval_ms > observed ? s->burst.interval_ms - observed : 0u;
        }
        c->last_start_ms = now;
        c->last_start_observed_ms = c->observed_ms;
        c->has_start = 1u;
        ++c->bursts;
        s->burst_active = 1u;
    }
    if (!s->burst_active) return;
    ++s->burst.samples;
    if (gap > s->burst.max_gap_ms) s->burst.max_gap_ms = gap;
    if (qdb > s->burst.peak_qdb) s->burst.peak_qdb = qdb;
    if (qdb - c->baseline_qdb >= ACTIVITY_END_QDB) s->below = 0u;
    else {
        if (!s->below) { s->below_at_ms = now; s->below = 1u; }
        if (now - s->below_at_ms >= 30u) end_burst(s, s->below_at_ms, 0u);
    }
}

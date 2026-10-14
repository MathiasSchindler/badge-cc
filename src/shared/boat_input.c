#include "boat_input.h"
#include "../tools/diagnostics_logic.h"
#include "bytes.h"

static int32_t absolute(int32_t value) { return value < 0 ? -value : value; }
static int32_t clamp(int32_t value, int32_t low, int32_t high) {
    return value < low ? low : value > high ? high : value;
}

static uint32_t magnitude(const int32_t vector[3]) {
    uint32_t square = 0u;
    for (uint32_t i = 0u; i < 3u; ++i) square += (uint32_t)(vector[i] * vector[i]);
    return diag_sqrt(square);
}

static void normalize(int32_t vector[3]) {
    uint32_t length = magnitude(vector);
    if (length == 0u) return;
    for (uint32_t i = 0u; i < 3u; ++i) vector[i] = vector[i] * 16384 / (int32_t)length;
}

void boat_input_init(boat_input_t *state) {
    badger_bytes_zero((uint8_t *)state, (uint32_t)sizeof(*state));
}

static void calibration_begin(boat_input_t *state, const int32_t accel[3],
    const int32_t gyro[3], uint32_t now) {
    state->samples = 1u;
    state->calibration_at = now;
    state->calibration_reason = BOAT_CAL_COLLECTING;
    for (uint32_t i = 0u; i < 3u; ++i) {
        state->sums[i] = gyro[i];
        state->accel_low[i] = state->accel_high[i] = accel[i];
        state->gyro_low[i] = state->gyro_high[i] = gyro[i];
    }
}

static int calibrate(boat_input_t *state, const int32_t accel[3],
    const int32_t gyro[3], uint32_t now, int credible, uint32_t delta) {
    state->calibration_rate = 0;
    for (uint32_t i = 0u; i < 3u; ++i)
        if (absolute(gyro[i]) > state->calibration_rate) state->calibration_rate = absolute(gyro[i]);
    /* The sensor's zero-rate offset can exceed 4.375 dps (500 counts).
       Stationarity is determined by the window spans, not near-zero rate. */
    if (!credible || state->calibration_rate > 2000) {
        state->samples = 0u;
        state->calibration_reason = credible ? BOAT_CAL_RATE : BOAT_CAL_ACCEL;
        return 0;
    }
    if (state->samples == 0u || delta > 50000u) {
        calibration_begin(state, accel, gyro, now);
        if (delta > 50000u) state->calibration_reason = BOAT_CAL_GAP;
        return 0;
    }
    for (uint32_t i = 0u; i < 3u; ++i) {
        if (accel[i] < state->accel_low[i]) state->accel_low[i] = accel[i];
        if (accel[i] > state->accel_high[i]) state->accel_high[i] = accel[i];
        if (gyro[i] < state->gyro_low[i]) state->gyro_low[i] = gyro[i];
        if (gyro[i] > state->gyro_high[i]) state->gyro_high[i] = gyro[i];
        if (state->accel_high[i] - state->accel_low[i] > 600 ||
            state->gyro_high[i] - state->gyro_low[i] > 200) {
            calibration_begin(state, accel, gyro, now);
            state->calibration_reason = BOAT_CAL_MOVING;
            return 0;
        }
    }
    ++state->samples;
    state->calibration_reason = BOAT_CAL_COLLECTING;
    for (uint32_t i = 0u; i < 3u; ++i) state->sums[i] += gyro[i];
    if (now - state->calibration_at < 500000u || state->samples < 32u) return 0;
    for (uint32_t i = 0u; i < 3u; ++i) state->bias[i] = state->sums[i] / (int32_t)state->samples;
    state->calibrated = 1u;
    return 1;
}

static int pose_supported(const boat_input_t *state) {
    int32_t x = state->gravity[0], y = state->gravity[1];
    uint32_t planar = diag_sqrt((uint32_t)(x * x) + (uint32_t)(y * y));
    uint32_t minimum = state->supported ? 5500u : 6500u;
    int32_t limit = state->supported ? 170 : 150;
    return planar >= minimum && y > 0 && absolute(x) * 256 <= (int32_t)planar * limit;
}

boat_input_result_t boat_input_update(boat_input_t *state, const lsm6ds3_sample_t *sample,
    uint32_t now, int32_t *gx, int32_t *gy) {
    if (state == 0 || sample == 0 || gx == 0 || gy == 0) return BOAT_INPUT_ARGUMENT;
    *gx = 0; *gy = 256;
    int32_t accel[3] = {sample->accel_x, sample->accel_y, sample->accel_z};
    int32_t gyro[3] = {sample->gyro_x, sample->gyro_y, sample->gyro_z};
    uint32_t length = magnitude(accel);
    int credible = length >= 12288u && length <= 20480u;
    uint32_t delta = state->started ? now - state->last_tick : 0u;
    if (state->started && delta < 5000u)
        return state->calibrated ? BOAT_INPUT_UNRELIABLE : BOAT_INPUT_CALIBRATING;
    state->started = 1u;
    state->last_tick = now;
    state->sample_gap = delta;
    if (!state->calibrated && !calibrate(state, accel, gyro, now, credible, delta))
        return BOAT_INPUT_CALIBRATING;
    if (delta > 50000u) {
        state->aligned = state->supported = state->pose_pending = state->reanchor_pending = 0u;
        return BOAT_INPUT_UNRELIABLE;
    }
    if (!state->aligned) {
        if (!credible) return BOAT_INPUT_UNRELIABLE;
        for (uint32_t i = 0u; i < 3u; ++i) {
            state->gravity[i] = accel[i];
            state->previous_gyro[i] = gyro[i] - state->bias[i];
        }
        normalize(state->gravity);
        state->trusted_at = now;
        state->aligned = 1u;
        state->reanchor_pending = 0u;
    } else {
        int32_t angle[3];
        for (uint32_t i = 0u; i < 3u; ++i) {
            if (absolute(gyro[i]) >= 32000) {
                state->aligned = state->supported = state->pose_pending = state->reanchor_pending = 0u;
                return BOAT_INPUT_UNRELIABLE;
            }
            /* Trapezoidal rate integration. 8.75 mdps/LSB gives Q16
               radians ~= raw * dt_us / 100000. */
            int32_t rate = gyro[i] - state->bias[i];
            angle[i] = (rate + state->previous_gyro[i]) * (int32_t)(delta / 100u) / 2000;
            state->previous_gyro[i] = rate;
        }
        int32_t x = state->gravity[0], y = state->gravity[1], z = state->gravity[2];
        state->gravity[0] += (y * angle[2] - z * angle[1]) / 65536;
        state->gravity[1] += (z * angle[0] - x * angle[2]) / 65536;
        state->gravity[2] += (x * angle[1] - y * angle[0]) / 65536;
        normalize(state->gravity);
        if (credible) {
            normalize(accel);
            int32_t dot = 0;
            int quiet = 1;
            for (uint32_t i = 0u; i < 3u; ++i) {
                dot += accel[i] * state->gravity[i];
                if (absolute(gyro[i] - state->bias[i]) > 500) quiet = 0;
            }
            /* Normalized vector blending cannot escape exact antipodes.
               Re-anchor only after a steady, credible conflicting hold. */
            if (dot < 0 && quiet) {
                int restart = !state->reanchor_pending;
                if (!restart) {
                    for (uint32_t i = 0u; i < 3u; ++i) {
                        if (accel[i] < state->accel_low[i]) state->accel_low[i] = accel[i];
                        if (accel[i] > state->accel_high[i]) state->accel_high[i] = accel[i];
                        if (state->accel_high[i] - state->accel_low[i] > 600) restart = 1;
                    }
                }
                if (restart) {
                    state->reanchor_pending = 1u;
                    state->reanchor_at = now;
                    for (uint32_t i = 0u; i < 3u; ++i)
                        state->accel_low[i] = state->accel_high[i] = accel[i];
                }
                state->supported = state->pose_pending = 0u;
                if (now - state->reanchor_at < 250000u) return BOAT_INPUT_UNRELIABLE;
                for (uint32_t i = 0u; i < 3u; ++i) state->gravity[i] = accel[i];
            }
            state->reanchor_pending = 0u;
            uint32_t dt = delta / 100u;
            for (uint32_t i = 0u; i < 3u; ++i)
                state->gravity[i] += (accel[i] - state->gravity[i]) * (int32_t)dt / (int32_t)(5000u + dt);
            normalize(state->gravity);
            state->trusted_at = now;
        } else state->reanchor_pending = 0u;
    }
    if (now - state->trusted_at > 250000u) {
        state->supported = state->pose_pending = 0u;
        return BOAT_INPUT_UNRELIABLE;
    }
    int supported = pose_supported(state);
    if (supported != state->supported) {
        if (!state->pose_pending) { state->pose_pending = 1u; state->pose_at = now; }
        if (now - state->pose_at >= 100000u) {
            state->supported = (uint8_t)supported;
            state->pose_pending = 0u;
        }
    } else state->pose_pending = 0u;
    if (!state->supported) return BOAT_INPUT_POSE;
    uint32_t planar = diag_sqrt((uint32_t)(state->gravity[0] * state->gravity[0]) +
        (uint32_t)(state->gravity[1] * state->gravity[1]));
    if (planar < 5500u || state->gravity[1] <= 0) return BOAT_INPUT_POSE;
    *gx = clamp(state->gravity[0] * 256 / (int32_t)planar, -96, 96);
    *gy = clamp(state->gravity[1] * 256 / (int32_t)planar, 192, 256);
    return BOAT_INPUT_READY;
}

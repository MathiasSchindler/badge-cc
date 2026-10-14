#ifndef UNIVERSE26_BOAT_INPUT_H
#define UNIVERSE26_BOAT_INPUT_H

#include "../platform/badger/lsm6ds3.h"

typedef enum {
    BOAT_INPUT_READY,
    BOAT_INPUT_CALIBRATING,
    BOAT_INPUT_POSE,
    BOAT_INPUT_UNRELIABLE,
    BOAT_INPUT_ARGUMENT
} boat_input_result_t;

typedef enum {
    BOAT_CAL_COLLECTING,
    BOAT_CAL_ACCEL,
    BOAT_CAL_RATE,
    BOAT_CAL_MOVING,
    BOAT_CAL_GAP
} boat_calibration_reason_t;

typedef struct {
    int32_t gravity[3], bias[3], sums[3], previous_gyro[3];
    int32_t accel_low[3], accel_high[3], gyro_low[3], gyro_high[3];
    uint32_t last_tick, calibration_at, trusted_at, pose_at, reanchor_at, samples;
    uint8_t started, calibrated, aligned, supported, pose_pending, reanchor_pending;
    boat_calibration_reason_t calibration_reason;
    int32_t calibration_rate;
    uint32_t sample_gap;
} boat_input_t;

void boat_input_init(boat_input_t *state);
/* Raw +/-2 g, 245 dps samples and actual microsecond timestamp, not physics
   catch-up time. Only READY outputs may advance physics. */
boat_input_result_t boat_input_update(boat_input_t *state, const lsm6ds3_sample_t *sample,
    uint32_t now_us, int32_t *gravity_x, int32_t *gravity_y);

#endif

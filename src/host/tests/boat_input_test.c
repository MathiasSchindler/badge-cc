#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../shared/boat_input.h"

static uint32_t now;
static int32_t gx, gy;
static boat_input_result_t feed(boat_input_t *state, lsm6ds3_sample_t sample, uint32_t delta) {
    now += delta;
    boat_input_result_t result = boat_input_update(state, &sample, now, &gx, &gy);
    if (result == BOAT_INPUT_READY) {
        assert(gx >= -96 && gx <= 96 && gy >= 192 && gy <= 256);
    } else assert(gx == 0 && gy == 256);
    return result;
}

static lsm6ds3_sample_t pose(double roll, double lean) {
    double r = roll * 3.141592653589793 / 180.0;
    double p = lean * 3.141592653589793 / 180.0;
    lsm6ds3_sample_t sample = {0};
    sample.accel_x = (int16_t)lround(16384.0 * sin(r) * cos(p));
    sample.accel_y = (int16_t)lround(16384.0 * cos(r) * cos(p));
    sample.accel_z = (int16_t)lround(-16384.0 * sin(p));
    return sample;
}

static void hold(boat_input_t *state, lsm6ds3_sample_t sample,
    uint32_t steps, boat_input_result_t expected) {
    boat_input_result_t result = BOAT_INPUT_ARGUMENT;
    for (uint32_t i = 0u; i < steps; ++i) result = feed(state, sample, 10000u);
    assert(result == expected);
}

static void ready(boat_input_t *state, lsm6ds3_sample_t sample) {
    boat_input_init(state);
    hold(state, sample, 100u, BOAT_INPUT_READY);
}

static void calibration(void) {
    boat_input_t state;
    lsm6ds3_sample_t sample = pose(0, 0);
    sample.gyro_x = 43; sample.gyro_y = -61; sample.gyro_z = 97;
    boat_input_init(&state);
    for (uint32_t i = 0u; i < 50u; ++i)
        assert(feed(&state, sample, 10000u) == BOAT_INPUT_CALIBRATING);
    assert(!state.calibrated);
    hold(&state, sample, 50u, BOAT_INPUT_READY);
    assert(state.bias[0] == 43 && state.bias[1] == -61 && state.bias[2] == 97);
    hold(&state, sample, 1000u, BOAT_INPUT_READY);
    assert(gx == 0 && gy == 256);

    boat_input_init(&state);
    sample.gyro_z = 2500;
    hold(&state, sample, 150u, BOAT_INPUT_CALIBRATING);
    assert(state.samples == 0u);
    assert(state.calibration_reason == BOAT_CAL_RATE);
    sample.gyro_z = 0;
    for (uint32_t i = 0u; i < 150u; ++i) {
        sample.accel_x = i % 2u ? 1200 : -1200;
        assert(feed(&state, sample, 10000u) == BOAT_INPUT_CALIBRATING);
    }
    sample = pose(0, 0);
    hold(&state, sample, 100u, BOAT_INPUT_READY);

    boat_input_init(&state);
    for (uint32_t i = 0u; i < 150u; ++i) {
        sample.gyro_z = i % 2u ? 150 : -150;
        assert(feed(&state, sample, 10000u) == BOAT_INPUT_CALIBRATING);
    }
    boat_input_init(&state);
    sample = pose(0, 0);
    assert(feed(&state, sample, 10000u) == BOAT_INPUT_CALIBRATING);
    for (uint32_t i = 0u; i < 1000u; ++i)
        assert(feed(&state, sample, 1u) == BOAT_INPUT_CALIBRATING);
    assert(state.samples == 1u && !state.calibrated);
    sample.gyro_z = 1200;
    ready(&state, sample);
    assert(state.bias[2] == 1200);
    hold(&state, sample, 1000u, BOAT_INPUT_READY);
    assert(gx == 0 && gy == 256);
}

static void pose_ranges(void) {
    boat_input_t state;
    ready(&state, pose(0, 60));
    assert(gx == 0 && gy == 256);
    hold(&state, pose(15, 60), 400u, BOAT_INPUT_READY);
    assert(gx >= 62 && gx <= 69 && gy >= 244);
    hold(&state, pose(-15, 60), 400u, BOAT_INPUT_READY);
    assert(gx <= -62 && gx >= -69);
    hold(&state, pose(0, -60), 400u, BOAT_INPUT_READY);
    hold(&state, pose(0, 90), 400u, BOAT_INPUT_POSE);
    hold(&state, pose(0, 180), 400u, BOAT_INPUT_POSE);
    hold(&state, pose(50, 0), 400u, BOAT_INPUT_POSE);
    hold(&state, pose(0, 0), 400u, BOAT_INPUT_READY);
    lsm6ds3_sample_t inverted = { .accel_y = -16384 };
    boat_input_init(&state);
    hold(&state, inverted, 100u, BOAT_INPUT_POSE);
    hold(&state, pose(0, 0), 100u, BOAT_INPUT_READY);
    assert(gx == 0 && gy == 256);

    /* Direct aligned-vector fixtures test gate thresholds independently of
       complementary-filter settling; these are not physical sign evidence. */
    state.gravity[0] = 0; state.gravity[1] = 5700; state.gravity[2] = -15358;
    lsm6ds3_sample_t edge = { .accel_y = 5700, .accel_z = -15358 };
    hold(&state, edge, 50u, BOAT_INPUT_READY);
    edge.accel_y = 5300; edge.accel_z = -15503;
    hold(&state, edge, 300u, BOAT_INPUT_POSE);
    state.gravity[0] = 0; state.gravity[1] = 6200; state.gravity[2] = -15165;
    edge.accel_y = 6200; edge.accel_z = -15165;
    hold(&state, edge, 50u, BOAT_INPUT_POSE);
    edge.accel_y = 7000; edge.accel_z = -14814;
    hold(&state, edge, 300u, BOAT_INPUT_READY);

    state.gravity[0] = 10232; state.gravity[1] = 12793; state.gravity[2] = 0;
    edge = pose(38.66, 0);
    hold(&state, edge, 100u, BOAT_INPUT_READY);
    edge = pose(43, 0);
    hold(&state, edge, 400u, BOAT_INPUT_POSE);
    edge = pose(38.66, 0);
    hold(&state, edge, 400u, BOAT_INPUT_POSE);
    edge = pose(34, 0);
    hold(&state, edge, 400u, BOAT_INPUT_READY);

    /* A brief excursion doesn't change the latched pose. */
    state.gravity[0] = 11585; state.gravity[1] = 11585; state.gravity[2] = 0;
    edge = pose(45, 0);
    hold(&state, edge, 9u, BOAT_INPUT_READY);
    state.gravity[0] = 0; state.gravity[1] = 16384; state.gravity[2] = 0;
    hold(&state, pose(0, 0), 10u, BOAT_INPUT_READY);
}

static void rotation_and_timing(void) {
    boat_input_t state;
    ready(&state, pose(0, 0));
    lsm6ds3_sample_t sample = { .gyro_z = 10000 };
    hold(&state, sample, 10u, BOAT_INPUT_READY);
    assert(gx >= 35 && gx <= 39); /* 8.3125 degrees including the initial rate ramp. */
    sample.gyro_z = -10000;
    hold(&state, sample, 10u, BOAT_INPUT_READY);
    assert(gx >= -2 && gx <= 2);
    hold(&state, pose(0, 0), 100u, BOAT_INPUT_READY);

    state.previous_gyro[2] = 20000;
    boat_input_t irregular = state;
    uint32_t irregular_at = now;
    sample.gyro_z = 20000;
    sample.accel_y = 0;
    hold(&state, sample, 10u, BOAT_INPUT_READY);
    int32_t regular_x = state.gravity[0], regular_y = state.gravity[1];
    now = irregular_at;
    assert(feed(&irregular, sample, 25000u) == BOAT_INPUT_READY);
    assert(feed(&irregular, sample, 35000u) == BOAT_INPUT_READY);
    assert(feed(&irregular, sample, 40000u) == BOAT_INPUT_READY);
    assert(abs(regular_x - irregular.gravity[0]) < 30);
    assert(abs(regular_y - irregular.gravity[1]) < 30);
    assert(gx >= 74 && gx <= 80); /* 17.5 degrees, not extra catch-up rotation. */
    assert(feed(&irregular, sample, 100000u) == BOAT_INPUT_UNRELIABLE);
    assert(!irregular.aligned);
    assert(feed(&irregular, sample, 10000u) == BOAT_INPUT_UNRELIABLE);
    hold(&irregular, pose(0, 0), 30u, BOAT_INPUT_READY);
    assert(gx == 0);

    ready(&state, pose(0, 0));
    sample = (lsm6ds3_sample_t){ .gyro_x = 10000 };
    state.previous_gyro[0] = 10000;
    hold(&state, sample, 10u, BOAT_INPUT_READY);
    assert(state.gravity[2] < -2400 && state.gravity[2] > -2600);
    ready(&state, pose(0, 60));
    sample = (lsm6ds3_sample_t){ .gyro_y = 10000 };
    state.previous_gyro[1] = 10000;
    hold(&state, sample, 10u, BOAT_INPUT_READY);
    assert(state.gravity[0] > 2000 && state.gravity[0] < 2250);

    now = UINT32_MAX - 500000u;
    ready(&state, pose(0, 0));
    assert(now < 1000000u && state.calibrated);
    hold(&state, pose(0, 0), 10u, BOAT_INPUT_READY);
}

static void unreliable(void) {
    boat_input_t state;
    ready(&state, pose(0, 0));
    lsm6ds3_sample_t sample = { .accel_x = 32767, .accel_y = 32767, .accel_z = -32768 };
    hold(&state, sample, 20u, BOAT_INPUT_READY);
    assert(gx == 0 && gy == 256);
    hold(&state, sample, 10u, BOAT_INPUT_UNRELIABLE);
    hold(&state, pose(0, 0), 30u, BOAT_INPUT_READY);
    sample = pose(0, 0); sample.gyro_z = 32767;
    assert(feed(&state, sample, 10000u) == BOAT_INPUT_UNRELIABLE);
    hold(&state, pose(0, 0), 30u, BOAT_INPUT_READY);
    sample.gyro_z = -32768;
    assert(feed(&state, sample, 10000u) == BOAT_INPUT_UNRELIABLE);
    assert(boat_input_update(0, &sample, now, &gx, &gy) == BOAT_INPUT_ARGUMENT);
    assert(boat_input_update(&state, 0, now, &gx, &gy) == BOAT_INPUT_ARGUMENT);
    assert(boat_input_update(&state, &sample, now, 0, &gy) == BOAT_INPUT_ARGUMENT);
    assert(boat_input_update(&state, &sample, now, &gx, 0) == BOAT_INPUT_ARGUMENT);

    ready(&state, pose(0, 0));
    sample = (lsm6ds3_sample_t){ .accel_x = 8192, .accel_y = 16384 };
    hold(&state, sample, 10u, BOAT_INPUT_READY);
    printf("boat input: 0.5g/100ms translation response %d/256\n", gx);
    assert(gx > 0 && gx <= 24);
    hold(&state, pose(0, 0), 300u, BOAT_INPUT_READY);
    assert(gx <= 1);
}

static void trajectories(void) {
    for (uint32_t cadence = 0u; cadence < 2u; ++cadence) {
        for (uint32_t speed = 0u; speed < 2u; ++speed) {
            boat_input_t state;
            ready(&state, pose(0, 60));
            uint32_t elapsed = 0u, index = 0u;
            int32_t max_error = 0;
            double period = speed ? 0.8 : 8.0;
            while (elapsed < 8000000u) {
                const uint32_t intervals[] = {10000u, 20000u, 40000u};
                uint32_t dt = cadence ? intervals[index++ % 3u] : 10000u;
                elapsed += dt;
                double phase = (double)elapsed / 1000000.0 * 2.0 * 3.141592653589793 / period;
                double roll = 30.0 * sin(phase);
                double rate = 30.0 * cos(phase) * 2.0 * 3.141592653589793 / period;
                lsm6ds3_sample_t sample = pose(roll, 60);
                sample.gyro_z = (int16_t)lround(rate / 0.00875);
                assert(feed(&state, sample, dt) == BOAT_INPUT_READY);
                int32_t expected = (int32_t)lround(256.0 * sin(roll * 3.141592653589793 / 180.0));
                if (expected > 96) expected = 96;
                if (expected < -96) expected = -96;
                int32_t error = abs(gx - expected);
                if (error > max_error) max_error = error;
            }
            printf("boat input: %s %s-turn replay maximum error %d/256\n",
                cadence ? "irregular" : "10ms", speed ? "fast" : "slow", max_error);
            assert(max_error <= 6);
            hold(&state, pose(0, 60), 400u, BOAT_INPUT_READY);
            assert(abs(gx) <= 1);
        }
    }
}

int main(void) {
    calibration();
    pose_ranges();
    rotation_and_timing();
    unreliable();
    trajectories();
    puts("boat input: calibration/motion rejection, partial-upright range, hysteresis, timed 3-axis rotation/reversal, shake, stalls and timer wrap PASS");
}

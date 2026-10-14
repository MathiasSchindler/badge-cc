#ifndef UNIVERSE26_BOAT_MEASURE_H
#define UNIVERSE26_BOAT_MEASURE_H

#include "nametag_measure.h"

/* Reuse the tested M2 paged envelope, not the nametag payload schema. */
#define BOAT_MEASURE_MAGIC 0x34544142u
#define BOAT_MEASURE_STEPS 60000u
#define BOAT_MEASURE_BINS 64u
enum {
    BM_MAGIC, BM_VERSION, BM_ERROR, BM_STAGE, BM_DURATION, BM_STEPS,
    BM_FRAMES, BM_PHYSICS_SKIPS, BM_RENDER_SKIPS, BM_PIXELS, BM_REGIONS,
    BM_FULL_FRAMES, BM_SAMPLES, BM_SOF, BM_SOF_MAX_GAP, BM_VOLUME,
    BM_VOLUME_ERRORS, BM_MIN_DEPTH, BM_MAX_DEPTH, BM_MIN_X, BM_MAX_X,
    BM_MIN_Y, BM_MAX_Y, BM_MAX_SLOPE, BM_CALIBRATED, BM_BIAS_X, BM_BIAS_Y,
    BM_BIAS_Z, BM_SAMPLE_GAP_MAX, BM_FRAME_GAPS, BM_FIRST_FRAME_US,
    BM_INITIAL_VOLUME, BM_STATUS_BASE = 32, BM_FRAME_OVER_25MS = 37, BM_CLOCK = 40,
    BM_METRIC_BASE = 64, BM_HISTOGRAM_BASE = 112
};
enum {
    BM_SENSOR, BM_INPUT, BM_PHYSICS, BM_RENDER, BM_PRESENT,
    BM_WRITER, BM_FRAME_WORK, BM_FRAME_INTERVAL, BM_METRICS
};
/* Four words per metric: count, sum, minimum, maximum; no truncated sample
   reservoir. Frame-work histogram: 1 ms bins; last is >=63 ms. */
void boat_measure_init(uint32_t words[NAMETAG_M2_WORDS]);
void boat_measure_record(uint32_t words[NAMETAG_M2_WORDS], uint32_t metric, uint32_t us);

#ifdef BOAT_PROFILE
int boat_profile_begin(uint32_t now);
int boat_profile_poll(uint32_t now);
void boat_profile_sample(uint32_t status, uint32_t gap, int calibrated, const int32_t bias[3]);
void boat_profile_gravity(uint32_t step, int32_t *gx, int32_t *gy);
int boat_profile_check(void);
void boat_profile_record(uint32_t metric, uint32_t us);
int boat_profile_service(void);
void boat_profile_abort(int32_t error);
void boat_profile_halt(void);
void boat_profile_frame(uint32_t started, uint32_t rendered, uint32_t finished,
    uint32_t pixels, uint32_t regions, uint32_t full, uint32_t skipped);
#endif
#endif

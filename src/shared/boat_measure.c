#include "boat_measure.h"
#include "bytes.h"

void boat_measure_init(uint32_t words[NAMETAG_M2_WORDS]) {
    badger_bytes_zero((uint8_t *)words, NAMETAG_M2_WORDS * 4u);
    words[BM_MAGIC] = BOAT_MEASURE_MAGIC;
    words[BM_VERSION] = 1u;
    for (uint32_t i = 0u; i < BM_METRICS; ++i)
        words[BM_METRIC_BASE + i * 4u + 2u] = 0xffffffffu;
    words[BM_MIN_DEPTH] = words[BM_MIN_X] = words[BM_MIN_Y] = 0xffffffffu;
}

void boat_measure_record(uint32_t words[NAMETAG_M2_WORDS], uint32_t metric, uint32_t us) {
    uint32_t *stat = words + BM_METRIC_BASE + metric * 4u;
    ++stat[0];
    stat[1] += us;
    if (us < stat[2]) stat[2] = us;
    if (us > stat[3]) stat[3] = us;
    if (metric == BM_FRAME_WORK) {
        uint32_t bin = us / 1000u;
        if (bin >= BOAT_MEASURE_BINS) bin = BOAT_MEASURE_BINS - 1u;
        ++words[BM_HISTOGRAM_BASE + bin];
    }
}

#include <stdint.h>

uint32_t micro_kernel(uint32_t seed) {
    static const int16_t samples[] = {31, -73, 200, 601, -101, 17, -280, 101};
    int32_t state = (int32_t)(seed & 65535u) - 32768;
    uint32_t gain = (seed >> 16u) & 4095u;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        int32_t target = (int32_t)samples[i] * 32;
        int64_t delta = (int64_t)(target - state) * gain;
        state += (int32_t)((delta + (delta >= 0 ? 2048 : -2048)) / 4096);
        sum = (sum * 33u) ^ (uint32_t)state;
    }
    return sum;
}

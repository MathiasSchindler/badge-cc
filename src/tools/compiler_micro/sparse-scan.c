#include <stdint.h>

typedef struct {
    uint16_t key, weight;
} record_t;

static uint32_t score(uint32_t value, uint32_t weight) {
    uint32_t distance = value > weight ? value - weight : weight - value;
    return ((weight * 257u) / (distance + 3u)) ^ (weight * distance);
}

uint32_t micro_kernel(uint32_t seed) {
    static const record_t records[] = {
        {2, 105}, {9, 51}, {15, 250}, {21, 180}, {27, 59}, {33, 239},
        {38, 17}, {41, 209}, {49, 82}, {52, 13}, {59, 83}, {63, 145}
    };
    uint32_t center = seed & 63u, value = (seed >> 8u) & 255u, sum = 0;
    for (uint32_t i = 0; i < sizeof(records) / sizeof(records[0]); ++i) {
        uint32_t key = records[i].key;
        if (key + 3u < center || key > center + 3u)
            continue;
        sum += score(value, records[i].weight);
    }
    return sum;
}

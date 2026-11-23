#include <stdint.h>

uint32_t micro_kernel(uint32_t seed) {
    uint8_t tile[64];
    uint32_t noise = seed;
    for (uint32_t i = 0; i < 64u; ++i) {
        noise = noise * 1664525u + 1013904223u;
        tile[i] = (uint8_t)(noise >> 24u);
    }
    uint32_t threshold = ((seed >> 12u) & 63u) + 8u;
    uint32_t energy = 0, edges = 0, weighted = 0;
    for (uint32_t y = 1; y < 7u; ++y) {
        for (uint32_t x = 1; x < 7u; ++x) {
            uint32_t p = y * 8u + x;
            uint32_t left = tile[p - 1u], right = tile[p + 1u];
            uint32_t top = tile[p - 8u], bottom = tile[p + 8u];
            uint32_t dx = right >= left ? right - left : left - right;
            uint32_t dy = bottom >= top ? bottom - top : top - bottom;
            uint32_t magnitude = dx + dy;
            energy += magnitude;
            if (magnitude > threshold) {
                edges++;
                weighted += (tile[p] + magnitude) * (x + y);
            } else {
                weighted ^= tile[p] << (x & 3u);
            }
        }
    }
    return (energy * 131u + weighted) ^ (edges << 24u);
}

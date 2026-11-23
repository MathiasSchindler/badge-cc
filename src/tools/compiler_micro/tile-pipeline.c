#include <stdint.h>

static uint16_t shade(uint32_t color, uint32_t level, uint32_t phase) {
    uint32_t light = ((color >> 8u) & 255u) + level;
    if (light > 255u)
        light = 255u;
    uint32_t accent = (color ^ phase) & 31u;
    uint32_t red = (light + accent) >> 3u;
    if (red > 31u)
        red = 31u;
    uint32_t green = ((light * 3u + accent * 2u) >> 4u) & 63u;
    uint32_t blue = ((color >> 3u) ^ (light >> 2u) ^ phase) & 31u;
    return (uint16_t)((red << 11u) | (green << 5u) | blue);
}

uint32_t micro_kernel(uint32_t seed) {
    static const uint16_t palette[16] = {
        0x0080, 0x811f, 0x4280, 0xfd03, 0x52e0, 0x607a, 0xae17, 0x00ff,
        0x6bb2, 0x3664, 0xe7bd, 0x0295, 0x1084, 0x9d53, 0xbaf1, 0x7e00
    };
    uint16_t pixels[24];
    uint32_t count = (seed & 15u) + 8u;
    uint32_t level = (seed >> 8u) & 63u;
    uint32_t phase = seed >> 16u;
    for (uint32_t i = 0; i < count; ++i) {
        phase = phase * 101u + 17u;
        uint32_t color = palette[(phase ^ i) & 15u];
        pixels[i] = shade(color, level, phase);
    }
    uint32_t sum = 5381u;
    for (uint32_t i = 0; i < count; ++i)
        sum = (sum * 33u) ^ pixels[i];
    return sum;
}

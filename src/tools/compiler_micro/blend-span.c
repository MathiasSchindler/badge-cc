#include <stdint.h>

static uint16_t blend(uint32_t pair, uint32_t alpha) {
    uint32_t fg = pair >> 16u, bg = pair & 65535u;
    uint32_t inverse = 32u - alpha;
    uint32_t rb = ((fg & 0xf81fu) * alpha + (bg & 0xf81fu) * inverse) >> 5u;
    uint32_t g = ((fg & 0x07e0u) * alpha + (bg & 0x07e0u) * inverse) >> 5u;
    return (uint16_t)((rb & 0xf81fu) | (g & 0x07e0u));
}

uint32_t micro_kernel(uint32_t seed) {
    static const uint32_t pixels[9] = {
        0xf800001fu, 0x07e0ffffu, 0x0000f81fu, 0xffff4208u, 0x03ef07e0u,
        0x8410001fu, 0xf81fef7du, 0x4208ffffu, 0x001f0000u
    };
    uint16_t span[9];
    uint32_t count = (seed & 7u) + 2u;
    uint32_t alpha = (seed >> 8u) % 33u;
    uint32_t i = 0;
    for (; i + 1u < count; i += 2u) {
        span[i] = blend(pixels[i], alpha);
        span[i + 1u] = blend(pixels[i + 1u], alpha);
    }
    if (i < count)
        span[i] = blend(pixels[i], alpha);
    uint32_t result = 0;
    for (i = 0; i < count; ++i)
        result = (result * 33u) ^ span[i];
    return result;
}

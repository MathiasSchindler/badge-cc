#include "../../../src/shared/bytes.h"

static uint8_t source[2058], destination[2058];
static uint16_t pixels[258];

int main(void) {
    const uint32_t lengths[] = {0u, 1u, 3u, 4u, 63u, 64u, 65u, 512u, 2040u, 2056u};
    for (uint32_t i = 0u; i < 2058u; ++i) source[i] = (uint8_t)(i * 37u + 11u);
    for (uint32_t k = 0u; k < 10u; ++k) {
        uint32_t length = lengths[k];
        for (uint32_t i = 0u; i < 2058u; ++i) destination[i] = 0xa5u;
        badger_bytes_copy(destination + 1u, source + 1u, length);
        if (destination[0] != 0xa5u || destination[length + 1u] != 0xa5u) return 1;
        if (!badger_bytes_equal(destination + 1u, source + 1u, length)) return 2;
        for (uint32_t i = 0u; i < length; ++i) if (destination[i + 1u] != source[i + 1u]) return 3;
        if (length != 0u) {
            destination[length] ^= 1u;
            if (badger_bytes_equal(destination + 1u, source + 1u, length)) return 4;
        }
        badger_bytes_zero(destination + 1u, length);
        for (uint32_t i = 0u; i < length; ++i) if (destination[i + 1u] != 0u) return 5;
        if (destination[0] != 0xa5u || destination[length + 1u] != 0xa5u) return 6;
    }
    for (uint32_t i = 0u; i < 258u; ++i) pixels[i] = 0xa55au;
    badger_pixels_fill(pixels + 1u, 0x0863u, 256u);
    if (pixels[0] != 0xa55au || pixels[257] != 0xa55au) return 7;
    for (uint32_t i = 1u; i < 257u; ++i) if (pixels[i] != 0x0863u) return 8;
    badger_bytes_copy(0, 0, 0u);
    badger_bytes_zero(0, 0u);
    if (!badger_bytes_equal(0, 0, 0u)) return 9;
    badger_pixels_fill(0, 0u, 0u);
    const uint32_t values[] = {0u, 1u, 0x80000000u, 0xffffffffu, 0x89abcdefu};
    for (uint32_t k = 0u; k < 5u; ++k) {
        destination[0] = destination[5] = 0xa5u;
        badger_bytes_store_u32(destination + 1u, values[k]);
        if (badger_bytes_load_u32(destination + 1u) != values[k] ||
            destination[0] != 0xa5u || destination[5] != 0xa5u) return 10;
        for (uint32_t i = 0u; i < 4u; ++i)
            if (destination[i + 1u] != (uint8_t)(values[k] >> (8u * i))) return 11;
    }
    return 0;
}

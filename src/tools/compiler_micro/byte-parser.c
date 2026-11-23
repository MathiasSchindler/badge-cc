#include <stdint.h>

uint32_t micro_kernel(uint32_t seed) {
    static const uint8_t bytes[] = {0x21, 5, 7, 0x40, 0x02, 0x76, 1, 9, 0x82, 0x10, 0x41, 3};
    uint32_t pos = seed & 7u, end = 7u + ((seed >> 8u) % 6u);
    uint32_t result = 0;
    while (pos < end) {
        uint32_t head = bytes[pos++];
        uint32_t len = head & 3u;
        if ((head & 128u) || len > end - pos)
            return result ^ 0x80000000u;
        result = (result * 33u) ^ (head >> 2u);
        while (len != 0u) {
            result = (result * 33u) ^ bytes[pos++];
            --len;
        }
    }
    return result;
}

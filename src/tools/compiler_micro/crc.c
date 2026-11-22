#include <stdint.h>

uint32_t micro_kernel(uint32_t word) {
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0; i < 32; ++i) {
        uint32_t bit = (crc ^ word) & 1u;
        crc = (crc >> 1) ^ ((0u - bit) & 0xedb88320u);
        word >>= 1;
    }
    return ~crc;
}

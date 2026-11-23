#include <stdint.h>

uint32_t micro_kernel(uint32_t word) {
    uint32_t other = word ^ 0x73962dd1u;
    int32_t low = (int32_t)(int16_t)word * (int32_t)(int16_t)other;
    int32_t high = (int32_t)(int16_t)(word >> 16u) * (int32_t)(int16_t)(other >> 16u);
    return (uint32_t)low + (uint32_t)high;
}

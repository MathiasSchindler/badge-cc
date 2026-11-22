#include <stdint.h>

uint32_t micro_kernel(uint32_t seed) {
    int32_t x = (int32_t)(seed & 65535u) - 32768;
    int32_t y = (int32_t)(seed >> 16) - 32768;
    return (uint32_t)((x * 37 + y * 11) / 256);
}

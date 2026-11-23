#include <stdint.h>

uint32_t micro_kernel(uint32_t word) {
    int32_t input = (int16_t)(word >> 8u);
    if (input > 127) return 127u;
    if (input < -128) return (uint32_t)-128;
    return (uint32_t)input;
}

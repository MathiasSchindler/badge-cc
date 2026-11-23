#include <stdint.h>

uint32_t micro_kernel(uint32_t word) {
    uint32_t input = word & 65535u;
    return (input / 37u) ^ ((input % 37u) << 16u);
}

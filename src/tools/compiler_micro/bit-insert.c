#include <stdint.h>

uint32_t micro_kernel(uint32_t word) {
    uint32_t field = word >> 16u;
    return (word & ~0x1fe0u) | ((field << 5u) & 0x1fe0u);
}

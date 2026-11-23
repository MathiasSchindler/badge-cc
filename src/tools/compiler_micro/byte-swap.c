#include <stdint.h>

uint32_t micro_kernel(uint32_t word) {
    uint32_t pairs = ((word & 0x00ff00ffu) << 8u) | ((word >> 8u) & 0x00ff00ffu);
    return (pairs << 16u) | (pairs >> 16u);
}

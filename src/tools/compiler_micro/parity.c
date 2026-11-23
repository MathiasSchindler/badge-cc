#include <stdint.h>

uint32_t micro_kernel(uint32_t word) {
    word ^= word >> 16u;
    word ^= word >> 8u;
    word ^= word >> 4u;
    return (0x6996u >> (word & 15u)) & 1u;
}

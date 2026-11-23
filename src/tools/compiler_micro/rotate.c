#include <stdint.h>

uint32_t micro_kernel(uint32_t word) {
    return ((word << 7u) | (word >> 25u)) ^ 0x10203040u;
}

#include <stdint.h>

uint32_t micro_kernel(uint32_t word) {
    uint64_t sum = (uint64_t)word + (uint64_t)(word ^ 0x5713fc82u) + 33u;
    return (uint32_t)sum ^ (uint32_t)(sum >> 32u);
}

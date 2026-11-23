#include <stdint.h>

uint32_t micro_kernel(uint32_t word) {
    return word + ((word >> 8u) & 65535u);
}

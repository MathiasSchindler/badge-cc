#include <stdint.h>

uint32_t micro_kernel(uint32_t word) {
    switch (word & 7u) {
    case 0: return 3u;
    case 1: return 11u;
    case 2: return 6u;
    case 3: return 13u;
    case 4: return 8u;
    case 5: return 0u;
    case 6: return 12u;
    default: return 5u;
    }
}

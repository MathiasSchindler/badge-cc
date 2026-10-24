#ifndef CLEAN_RX_TEST_CRC32_H
#define CLEAN_RX_TEST_CRC32_H

#include <stdint.h>

/* Independent bitwise reference, not the controller's nibble-table FCS. */
static inline uint32_t test_crc32(const uint8_t *data, uint32_t length) {
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0u; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
}

#endif

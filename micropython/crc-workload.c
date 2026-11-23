#include <stddef.h>
#include <stdint.h>

uint32_t lfs2_crc(uint32_t, const void *, size_t);

int main(void) {
    uint8_t bytes[1024];
    uint32_t expected = 0xffffffffu;
    for (unsigned i = 0; i < sizeof(bytes); i++) {
        bytes[i] = (uint8_t)(i ^ (i >> 3));
        expected ^= bytes[i];
        for (unsigned bit = 0; bit < 8; bit++)
            expected = (expected >> 1) ^ ((expected & 1u) ? 0xedb88320u : 0u);
    }
    return lfs2_crc(0xffffffffu, bytes, sizeof(bytes)) != expected;
}

#include <stddef.h>
#include <stdint.h>

uint32_t lfs2_crc(uint32_t, const void *, size_t);
uint32_t gcc_lfs2_crc(uint32_t, const void *, size_t);

static uint32_t reference(uint32_t crc, const uint8_t *input, size_t count) {
    while (count--) {
        crc ^= *input++;
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return crc;
}

int main(void) {
    uint8_t input[260];
    uint32_t state = 0x12345678u;
    for (size_t i = 0; i < sizeof(input); i++) {
        state = state * 1664525u + 1013904223u;
        input[i] = (uint8_t)(state >> 24);
    }
    for (unsigned alignment = 0; alignment < 4; alignment++) {
        const uint8_t *bytes = input + alignment;
        for (size_t size = 0; size <= 256; size++) {
            uint32_t seed = size * 0x9e3779b9u + alignment;
            uint32_t expected = reference(seed, bytes, size);
            if (lfs2_crc(seed, bytes, size) != expected ||
                gcc_lfs2_crc(seed, bytes, size) != expected)
                return 1;
            size_t split = size / 3;
            if (lfs2_crc(lfs2_crc(seed, bytes, split), bytes + split, size - split) != expected)
                return 2;
        }
    }
    return 0;
}

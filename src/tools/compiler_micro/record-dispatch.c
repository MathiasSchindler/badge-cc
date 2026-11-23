#include <stdint.h>

static uint32_t fold_word(uint32_t accumulator, uint32_t value, uint32_t salt) {
    uint32_t mixed = (accumulator ^ value) * 33u + salt;
    return mixed ^ (mixed >> 11u);
}

static uint32_t weight(uint32_t value, uint32_t scale) {
    uint32_t upper = value >> 16u;
    uint32_t lower = value & 65535u;
    return (lower * scale + upper * (17u - scale)) >> 4u;
}

uint32_t micro_kernel(uint32_t seed) {
    static const uint8_t rows[32] = {
        0x29, 0xb3, 0x74, 0x4f, 0x88, 0xd1, 0x5e, 0x02,
        0x9d, 0x35, 0x3f, 0x67, 0xac, 0x51, 0xeb, 0x16,
        0x7a, 0xc3, 0x48, 0x82, 0x64, 0xb7, 0x0d, 0xdd,
        0x97, 0x26, 0xf0, 0x17, 0x58, 0xe4, 0x6e, 0x31
    };
    uint32_t total = seed, history = 0, active = 1;
    uint32_t start = (seed >> 7u) & 31u;
    uint32_t count = (seed & 15u) + 12u;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t row = rows[(start + i) & 31u];
        uint32_t data = (row & 31u) + ((total >> 8u) & 255u);
        switch (row >> 5u) {
        case 0: total += data * active; break;
        case 1: history = fold_word(history, total, row); break;
        case 2: total ^= weight(history ^ seed, (row & 15u) + 1u); break;
        case 3: active = total & 7u; break;
        case 4: total = fold_word(total, history, data); break;
        case 5: history += weight(total, (data & 15u) + 1u); break;
        case 6: if (active) total -= data; break;
        default: history ^= (total >> 3u) + i; break;
        }
        if ((row & 7u) == 1u)
            history = fold_word(history, data, i);
    }
    return total ^ history ^ (active << 16u);
}

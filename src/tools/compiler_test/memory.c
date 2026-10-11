#include "suite.h"
#include "../../shared/bytes.h"
#include "../diagnostics_logic.h"

void compiler_memory(uint32_t seed, compiler_result_t *out) {
    uint8_t source[67], dest[67];
    out->cases = 64u;
    for (uint32_t i = 0u; i < 67u; ++i) {
        source[i] = (uint8_t)(seed + 29u * i);
        dest[i] = 0xa5u;
    }
    for (uint32_t length = 0u; length < 64u; ++length) {
        badger_bytes_copy(dest + 1u, source + 1u, length);
        out->values[0] += (uint32_t)badger_bytes_equal(dest + 1u, source + 1u, length);
        out->values[1] = compiler_mix(out->values[1], diag_crc32(dest + 1u, length));
    }
    out->values[2] = (uint32_t)dest[0] | ((uint32_t)dest[64] << 8u) |
                     ((uint32_t)dest[66] << 16u);
    badger_bytes_store_u32(dest + 3u, seed ^ 0x12345678u);
    out->values[3] = badger_bytes_load_u32(dest + 3u);
    badger_bytes_zero(dest + 1u, 63u);
    out->values[4] = diag_crc32(dest, 67u);
    out->values[5] = (uint32_t)badger_bytes_equal(dest + 1u, dest + 2u, 62u);
}

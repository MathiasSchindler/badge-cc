#include "suite.h"
#include "../diagnostics_logic.h"

void compiler_strings(uint32_t seed, compiler_result_t *out) {
    const int32_t numbers[8] = {0, 1, -1, 2147483647, (-2147483647 - 1),
                               1000000000, -123456789, (int32_t)(seed & 65535u)};
    char text[12], hex[9], reversed[12];
    out->cases = 8u;
    for (uint32_t i = 0u; i < out->cases; ++i) {
        diag_decimal(text, numbers[i]);
        uint32_t length = 0u;
        while (text[length] != '\0') ++length;
        for (uint32_t j = 0u; j < length; ++j) reversed[j] = text[length - j - 1u];
        reversed[length] = '\0';
        diag_hex(hex, (uint32_t)numbers[i] ^ seed, 8u);
        out->values[0] += length;
        out->values[1] = compiler_mix(out->values[1], diag_crc32((const uint8_t *)text, length));
        out->values[2] = compiler_mix(out->values[2], diag_crc32((const uint8_t *)reversed, length));
        out->values[3] ^= diag_crc32((const uint8_t *)hex, 8u);
        out->values[4] += (uint8_t)text[0];
        out->values[5] += (uint8_t)text[length - 1u];
    }
}

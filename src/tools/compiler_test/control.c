#include "suite.h"
#include "../diagnostics_logic.h"

static uint32_t gcd(uint32_t a, uint32_t b) {
    while (b != 0u) {
        uint32_t next = a % b;
        a = b;
        b = next;
    }
    return a;
}

void compiler_control(uint32_t seed, compiler_result_t *out) {
    out->cases = 128u;
    for (uint32_t i = 1u; i <= out->cases; ++i) {
        uint32_t prime = i >= 2u;
        for (uint32_t divisor = 2u; divisor * divisor <= i; ++divisor) {
            if (i % divisor == 0u) { prime = 0u; break; }
        }
        if (prime) { ++out->values[0]; out->values[1] += i; }
        out->values[2] += gcd(seed % 10000u + i, i);
        switch (i % 5u) {
            case 0u: out->values[3] += i * i; break;
            case 1u: out->values[3] ^= i << 3u; break;
            case 2u: out->values[3] += i + 7u; break;
            default: out->values[3] = compiler_mix(out->values[3], i); break;
        }
        if ((i & 3u) == 0u) continue;
        out->values[4] += diag_sqrt(i * i + i);
        out->values[5] ^= compiler_mix(i, seed);
    }
}

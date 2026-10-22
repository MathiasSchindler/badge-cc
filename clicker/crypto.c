#include "crypto.h"

static uint8_t multiply(uint8_t a, uint8_t b) {
    uint8_t result = 0u;
    for (uint32_t i = 0u; i < 8u; ++i) {
        result ^= a & (uint8_t)(0u - (b & 1u));
        a = (uint8_t)((a << 1u) ^ (0x1bu & (0u - (a >> 7u))));
        b >>= 1u;
    }
    return result;
}

static uint8_t rotate(uint8_t x, uint32_t n) {
    return (uint8_t)((x << n) | (x >> (8u - n)));
}

static uint8_t substitute(uint8_t x) {
    uint8_t power = x, inverse = 1u;
    for (uint32_t bit = 0u; bit < 8u; ++bit) {
        if (bit != 0u) inverse = multiply(inverse, power);
        power = multiply(power, power);
    }
    return inverse ^ rotate(inverse, 1u) ^ rotate(inverse, 2u) ^
           rotate(inverse, 3u) ^ rotate(inverse, 4u) ^ 0x63u;
}

void clicker_aes128(const uint8_t key[16], const uint8_t input[16], uint8_t output[16]) {
    uint8_t keys[176], state[16], temporary[16], rcon = 1u;
    for (uint32_t i = 0u; i < 16u; ++i) keys[i] = key[i];
    for (uint32_t offset = 16u; offset < sizeof(keys); offset += 4u) {
        uint8_t word[4];
        for (uint32_t i = 0u; i < 4u; ++i) word[i] = keys[offset - 4u + i];
        if ((offset & 15u) == 0u) {
            uint8_t first = word[0];
            for (uint32_t i = 0u; i < 3u; ++i) word[i] = substitute(word[i + 1u]);
            word[3] = substitute(first);
            word[0] ^= rcon;
            rcon = multiply(rcon, 2u);
        }
        for (uint32_t i = 0u; i < 4u; ++i) keys[offset + i] = keys[offset - 16u + i] ^ word[i];
    }
    for (uint32_t i = 0u; i < 16u; ++i) state[i] = input[i] ^ keys[i];
    for (uint32_t round = 1u; round <= 10u; ++round) {
        for (uint32_t column = 0u; column < 4u; ++column) {
            for (uint32_t row = 0u; row < 4u; ++row) {
                temporary[column * 4u + row] =
                    substitute(state[((column + row) & 3u) * 4u + row]);
            }
        }
        for (uint32_t column = 0u; column < 4u; ++column) {
            uint32_t p = column * 4u;
            uint8_t a = temporary[p], b = temporary[p + 1u];
            uint8_t c = temporary[p + 2u], d = temporary[p + 3u];
            if (round != 10u) {
                state[p] = multiply(a, 2u) ^ multiply(b, 3u) ^ c ^ d;
                state[p + 1u] = a ^ multiply(b, 2u) ^ multiply(c, 3u) ^ d;
                state[p + 2u] = a ^ b ^ multiply(c, 2u) ^ multiply(d, 3u);
                state[p + 3u] = multiply(a, 3u) ^ b ^ c ^ multiply(d, 2u);
            } else {
                for (uint32_t i = 0u; i < 4u; ++i) state[p + i] = temporary[p + i];
            }
        }
        for (uint32_t i = 0u; i < 16u; ++i) state[i] ^= keys[round * 16u + i];
    }
    for (uint32_t i = 0u; i < 16u; ++i) output[i] = state[i];
    clicker_secret_clear(keys, sizeof(keys));
    clicker_secret_clear(state, sizeof(state));
    clicker_secret_clear(temporary, sizeof(temporary));
}

static void encrypt_le(const uint8_t key[16], const uint8_t input[16], uint8_t output[16]) {
    uint8_t k[16], value[16], result[16];
    for (uint32_t i = 0u; i < 16u; ++i) { k[i] = key[15u - i]; value[i] = input[15u - i]; }
    clicker_aes128(k, value, result);
    for (uint32_t i = 0u; i < 16u; ++i) output[i] = result[15u - i];
    clicker_secret_clear(k, sizeof(k));
    clicker_secret_clear(value, sizeof(value));
    clicker_secret_clear(result, sizeof(result));
}

void clicker_c1(const uint8_t key[16], const uint8_t random[16],
                const uint8_t request[7], const uint8_t response[7],
                uint8_t initiator_type, const uint8_t initiator[6],
                uint8_t responder_type, const uint8_t responder[6], uint8_t output[16]) {
    uint8_t p[16], intermediate[16];
    p[0] = initiator_type; p[1] = responder_type;
    for (uint32_t i = 0u; i < 7u; ++i) { p[2u + i] = request[i]; p[9u + i] = response[i]; }
    for (uint32_t i = 0u; i < 16u; ++i) p[i] ^= random[i];
    encrypt_le(key, p, intermediate);
    for (uint32_t i = 0u; i < 16u; ++i) {
        uint8_t byte = i < 6u ? responder[i] : i < 12u ? initiator[i - 6u] : 0u;
        p[i] = intermediate[i] ^ byte;
    }
    encrypt_le(key, p, output);
    clicker_secret_clear(p, sizeof(p));
    clicker_secret_clear(intermediate, sizeof(intermediate));
}

void clicker_s1(const uint8_t key[16], const uint8_t responder[16],
                const uint8_t initiator[16], uint8_t output[16]) {
    uint8_t input[16];
    for (uint32_t i = 0u; i < 8u; ++i) { input[i] = initiator[i]; input[i + 8u] = responder[i]; }
    encrypt_le(key, input, output);
    clicker_secret_clear(input, sizeof(input));
}

int clicker_resolves(const uint8_t irk[16], const uint8_t address[6]) {
    if ((address[5] & 0xc0u) != 0x40u) return 0;
    uint8_t input[16] = {0}, result[16];
    for (uint32_t i = 0u; i < 3u; ++i) input[i] = address[i + 3u];
    encrypt_le(irk, input, result);
    int matches = clicker_secret_equal(result, address, 3u);
    clicker_secret_clear(result, sizeof(result));
    return matches;
}

int clicker_secret_equal(const uint8_t *a, const uint8_t *b, uint32_t length) {
    uint32_t difference = 0u;
    for (uint32_t i = 0u; i < length; ++i) difference |= a[i] ^ b[i];
    return difference == 0u;
}

void clicker_secret_clear(void *data, uint32_t length) {
    volatile uint8_t *bytes = data;
    while (length-- != 0u) *bytes++ = 0u;
}

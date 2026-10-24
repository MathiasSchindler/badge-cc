#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "firmware_command.h"
#if CLEAN_RX_DIAGNOSTIC
#include "diagnostic/ht_command.h"
#else
#include "ht_payload.h"
#endif

int main(void) {
    for (uint32_t version = 0u; version < 256u; ++version) {
        for (uint32_t upper = 0u; upper < 32u; ++upper) {
            uint32_t bits = upper << 8u;
            assert(!clean_runtime_command_allowed(bits | 9u, (uint8_t)version));
            assert(!clean_runtime_command_allowed(bits | CLEAN_HT_PROBE_CMD, (uint8_t)version));
            assert(!clean_runtime_command_allowed(bits | CLEAN_AMPDU_BA_CMD, (uint8_t)version));
            assert(clean_runtime_command_allowed(bits | CLEAN_HT_DATA_CMD, (uint8_t)version) ==
                   (version == CLEAN_HT_DATA_VERSION));
            assert(clean_runtime_command_allowed(bits | 5u, (uint8_t)version));
            assert(clean_runtime_command_allowed(bits | 7u, (uint8_t)version));
        }
    }
    uint8_t packet[CLEAN_HT_DATA_HEADER + 6u + 64u] = {0};
    uint8_t *m = packet + CLEAN_HT_DATA_HEADER + 6u;
    uint32_t command = CLEAN_HT_DATA_CMD | clean_tx_length_bits(70u) | (1u << 19u);
    packet[0] = CLEAN_HT_DATA_VERSION;
    packet[4] = 64u;
    m[0] = 8u;
    m[10] = 2u;
    memset(m + 4u, 255, 6u);
    memset(m + 16u, 255, 6u);
    for (uint32_t rate = 0u; rate < 8u; ++rate) {
        packet[2] = (uint8_t)rate;
        assert(clean_ht20_sig(packet + CLEAN_HT_DATA_HEADER, 64u, rate));
        for (uint32_t seed = 0u; seed < 32u; ++seed) {
            for (uint32_t i = 24u; i < 60u; ++i) m[i] = (uint8_t)(seed + 3u * i);
            uint32_t crc = clean_mac_fcs(m, 60u);
            for (uint32_t i = 0u; i < 4u; ++i) m[60u+i] = (uint8_t)(crc >> (8u*i));
            assert(clean_ht_payload_command_valid(command, 1u, 0u, packet));
            assert(!clean_ht_payload_command_valid(command, 0u, 0u, packet));
            assert(!clean_ht_payload_command_valid(command, 1u, 1u, packet));
            m[60] ^= 1u;
            assert(!clean_ht_payload_command_valid(command, 1u, 0u, packet));
            m[60] ^= 1u;
        }
    }
#if !CLEAN_RX_DIAGNOSTIC
    for (uint32_t version = 0u; version < 256u; ++version) {
        packet[0] = (uint8_t)version;
        assert(clean_ht_payload_command_valid(command, 1u, 0u, packet) ==
               (version == CLEAN_HT_DATA_VERSION));
    }
#endif
    puts("Firmware commands: diagnostic rejection and arbitrary normal HT v1 payload admission PASS");
    return 0;
}

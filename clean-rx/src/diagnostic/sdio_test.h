#ifndef CLEAN_RX_SDIO_TEST_H
#define CLEAN_RX_SDIO_TEST_H

#include "../types.h"
#include "../sdio_ring.h"
#include "../rx_contract.h"

#define CLEAN_SDIO_TEST_PACING_TICKS 500000u
#define CLEAN_SDIO_TEST_ACK_OFFSET CLEAN_SDIO_ACK_OFFSET
#define CLEAN_SDIO_TEST_WINDOW_OFFSET 40u
#define CLEAN_SDIO_TEST_PATTERN_OFFSET 44u

#ifndef CLEAN_SDIO_SELF_TEST_WINDOW
#define CLEAN_SDIO_SELF_TEST_WINDOW 0u
#endif
#if CLEAN_SDIO_SELF_TEST_WINDOW > 16u
#error "SDIO test window must be 0..16"
#endif

static inline int clean_sdio_test_window_open(uint32_t posted, uint32_t consumed,
                                              uint32_t window) {
    return clean_sdio_credit_available(posted, consumed, window) > 0;
}

static inline uint32_t clean_sdio_test_length(uint32_t sequence) {
    return 8u + (sequence * 37u) % 497u;
}

static inline uint32_t clean_sdio_test_pattern_length(uint32_t sequence, uint32_t pattern) {
    if (pattern == 2u) {
        static const uint16_t boundaries[] = {
            8u,14u,15u,16u,31u,32u,33u,465u,502u,504u,505u,511u,512u,513u,
            1005u,1006u,1007u,1008u,1023u,1024u,1025u,1499u,1500u,1501u,
            1535u,1536u,1537u,1597u,1598u,1599u,1600u,
            2025u,2026u,2027u,2028u,2047u,2048u,2049u,
            2303u,2304u,2305u,2331u,2332u,2333u,2397u,2398u,2399u,2400u
        };
        return (sequence & 1u) ? boundaries[(sequence >> 1u) %
                (sizeof(boundaries) / sizeof(boundaries[0]))] :
                8u + ((sequence >> 1u) * 37u) % (CLEAN_MPDU_BYTES - 7u);
    }
    return pattern == 1u ? ((sequence & 1u) ? 465u : 502u) : clean_sdio_test_length(sequence);
}

static inline uint8_t clean_sdio_test_byte(uint32_t sequence, uint32_t offset) {
    uint32_t value = sequence * 0x9e3779b9u + offset * 0x01010101u;
    value ^= value >> 16u;
    value *= 0x85ebca6bu;
    return (uint8_t)(value ^ (value >> 13u));
}

#endif

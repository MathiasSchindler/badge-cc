#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "protocol.h"
#include "frame.h"
#include "../../src/shared/bytes.h"

static void frame_tests(void) {
    for (uint32_t mcs = 0u; mcs < 8u; ++mcs) {
        uint32_t rate = clean_peer_ht_rate(mcs);
        assert(clean_peer_rate_matches(rate, mcs));
        assert(clean_peer_rate_matches(rate | 0x40000000u, mcs));
        assert(clean_peer_rate_matches(rate | 0x80000000u, mcs));
        assert(clean_peer_rate_matches(rate | 0xc0000000u, mcs));
        assert(!clean_peer_rate_matches(rate, mcs ^ 1u));
        assert(!clean_peer_rate_matches(rate ^ 0x03000000u, mcs));
        for (uint32_t bit = 8u; bit < 30u; ++bit)
            assert(!clean_peer_rate_matches(rate ^ (1u << bit), mcs));
    }
    assert(!clean_peer_rate_matches(clean_peer_ht_rate(8u), 8u));
    uint8_t nonce[14] = {1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,7u};
    for (uint32_t qos = 0u; qos < 2u; ++qos)
        for (uint32_t sequence = 0u; sequence < CLEAN_PEER_FRAME_COUNT; ++sequence) {
            uint8_t ethernet[CLEAN_PEER_ETHERNET_BYTES], mpdu[88] = {0};
            clean_peer_ethernet(ethernet, nonce, (uint8_t)sequence);
            uint32_t header = qos ? 26u : 24u, length = header + CLEAN_PEER_ETHERNET_BYTES - 2u;
            mpdu[0] = qos ? 0x88u : 0x08u;
            mpdu[1] = 2u;
            memcpy(mpdu + 4u, ethernet, 6u);
            memcpy(mpdu + 10u, ethernet + 6u, 6u);
            memcpy(mpdu + 16u, ethernet + 6u, 6u);
            static const uint8_t snap[6] = {0xaau,0xaau,3u,0u,0u,0u};
            memcpy(mpdu + header, snap, sizeof(snap));
            memcpy(mpdu + header + 6u, ethernet + 12u, sizeof(ethernet) - 12u);
            badger_bytes_store_u32(mpdu + length - 4u, diag_crc32(mpdu, length - 4u));
            assert(clean_peer_mpdu_sequence(mpdu, length, nonce) == (int)sequence);
            assert(clean_peer_mpdu_sequence(mpdu, length - 1u, nonce) == -1);
            assert(clean_peer_mpdu_sequence(NULL, length, nonce) == -1);
            nonce[12] ^= 1u;
            assert(clean_peer_mpdu_sequence(mpdu, length, nonce) == -1);
            nonce[12] ^= 1u;
            for (uint32_t i = 4u; i < length - 4u; ++i) {
                if (i >= 22u && i < header) continue;
                mpdu[i] ^= 1u;
                badger_bytes_store_u32(mpdu + length - 4u, diag_crc32(mpdu, length - 4u));
                assert(clean_peer_mpdu_sequence(mpdu, length, nonce) == -1);
                mpdu[i] ^= 1u;
            }
            badger_bytes_store_u32(mpdu + length - 4u, diag_crc32(mpdu, length - 4u));
            mpdu[length - 1u] ^= 1u;
            assert(clean_peer_mpdu_sequence(mpdu, length, nonce) == -1);
        }
}

int main(void) {
    frame_tests();
    uint8_t control[CLEAN_PEER_CONTROL_BYTES], set;
    uint32_t command;
    for (uint32_t i = 0u; i < CLEAN_PEER_RECORDS; ++i) {
        uint32_t size = clean_peer_prepare_query(i, control, &command, &set);
        if (i < 5u) {
            assert(size == sizeof(control) && command == 262u && !set);
            assert(!strcmp((const char *)control, clean_peer_variables[i]));
            for (uint32_t j = (uint32_t)strlen(clean_peer_variables[i]) + 1u;
                 j < size; ++j) assert(!control[j]);
        } else if (i == 5u) {
            assert(size == 39u && command == 263u && set == 1u);
            assert(!strcmp((const char *)control, "pkteng") && control[7] == 2u);
            for (uint32_t j = 8u; j < sizeof(control); ++j) assert(!control[j]);
        } else {
            assert(size == 4u && command == 107u && !set);
            for (uint32_t j = 0u; j < sizeof(control); ++j) assert(!control[j]);
        }
    }
    assert(!clean_peer_prepare_query(CLEAN_PEER_RECORDS, control, &command, &set));
    uint8_t nonce[16] = {1u}, report[64], trace[CLEAN_PEER_TRACE_BYTES] = {0};
    usb_report_header(report, 0x46363255u, 123u, nonce, 3u);
    badger_bytes_store_u32(report + 60u, diag_crc32(report, 60u));
    assert(clean_peer_report_valid(report, nonce, 123u));
    for (uint32_t i = 0u; i < sizeof(report); ++i) {
        report[i] ^= 1u;
        assert(!clean_peer_report_valid(report, nonce, 123u));
        report[i] ^= 1u;
    }
    assert(!clean_peer_report_valid(report, nonce, 124u));
    nonce[1] = 1u;
    assert(!clean_peer_report_valid(report, nonce, 123u));
    assert(!clean_peer_trace_valid(trace));
    badger_bytes_store_u32(trace, CLEAN_PEER_MAGIC);
    badger_bytes_store_u32(trace + 16u, CLEAN_PEER_RECORDS);
    badger_bytes_store_u32(trace + 20u, 1u);
    badger_bytes_store_u32(trace + 24u, CLEAN_PEER_RECORDS);
    for (uint32_t i = 0u; i < CLEAN_PEER_RECORDS; ++i) {
        uint8_t *r = trace + CLEAN_PEER_RECORD_BASE + i * CLEAN_PEER_RECORD_BYTES;
        badger_bytes_store_u32(r, i);
        badger_bytes_store_u32(r + 8u, 8u);
        badger_bytes_store_u32(r + 12u, (uint32_t)-23);
    }
    assert(clean_peer_trace_valid(trace));
    uint8_t *r = trace + CLEAN_PEER_RECORD_BASE;
    badger_bytes_store_u32(r + 4u, 1u);
    assert(!clean_peer_trace_valid(trace));
    badger_bytes_store_u32(r + 8u, 0u);
    badger_bytes_store_u32(r + 12u, 0u);
    assert(clean_peer_trace_valid(trace));
    badger_bytes_store_u32(r + 16u, 1025u);
    assert(!clean_peer_trace_valid(trace));
    puts("Vendor peer report binding and explicit failure tests passed");
    return 0;
}

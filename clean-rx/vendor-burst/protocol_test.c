#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "protocol.h"
#include "../src/ht20.h"

int main(void) {
    uint8_t frame[REF_FRAME_MAX+8u], nonce[16], peer[6] = {2u,1u,2u,3u,4u,5u};
    for (uint32_t i = 0u; i < 16u; ++i) nonce[i] = (uint8_t)i;
    uint32_t counts[4] = {0}, first_counts[4] = {0}, digest = REF_DIGEST_INITIAL;
    for (uint32_t i = 0u; i < REF_FRAME_COUNT; ++i) {
        memset(frame,0xa5,sizeof(frame));
        uint32_t size = ref_frame(frame,nonce,peer,i);
        uint32_t index = (i+i/4u)&3u;
        assert(size == (128u << index));
        assert(!memcmp(frame,peer,6u) && frame[6] == 2u);
        assert(!memcmp(frame+7u,nonce,5u));
        assert(badger_bytes_load_u32(frame+22u) == i);
        assert(badger_bytes_load_u32(frame+size-4u) == diag_crc32(frame,size-4u));
        uint32_t next_digest = ref_digest(digest,frame,size);
        frame[38] ^= 1u;
        assert(next_digest != ref_digest(digest,frame,size));
        frame[38] ^= 1u;
        digest = next_digest;
        for (uint32_t j = size; j < sizeof(frame); ++j) assert(frame[j] == 0xa5u);
        ++counts[index];
        if (!(i&3u)) ++first_counts[index];
    }
    for (uint32_t i = 0u; i < 4u; ++i)
        assert(counts[i] == 32u && first_counts[i] == 8u);
    assert(digest != REF_DIGEST_INITIAL);
    assert(!ref_frame(frame,nonce,peer,REF_FRAME_COUNT));
    for (uint32_t mcs = 0u; mcs < 8u; ++mcs) {
        uint8_t sig[6];
        assert(clean_ht_ampdu_probe_sig(sig,mcs) && ref_sig_valid(sig));
        for (uint32_t bit = 0u; bit < 48u; ++bit) {
            sig[bit/8u] ^= (uint8_t)(1u<<(bit&7u));
            assert(!ref_sig_valid(sig));
            sig[bit/8u] ^= (uint8_t)(1u<<(bit&7u));
        }
    }
    uint8_t before[REF_COUNTER_BYTES] = {10u,0u,0x50u,3u}, after[REF_COUNTER_BYTES];
    memcpy(after,before,sizeof(after));
    badger_bytes_store_u32(before+416u,0xfffffffdu);
    badger_bytes_store_u32(after+416u,3u);
    uint32_t delta;
    assert(ref_counter_delta(before,after,416u,&delta) && delta == 6u);
    assert(!ref_counter_delta(before,after,3u,&delta));
    assert(!ref_counter_delta(before,after,848u,&delta));
    after[0] = 11u;
    assert(!ref_counter_delta(before,after,416u,&delta));
    after[0] = 10u; after[2] = 0x4cu;
    assert(!ref_counter_delta(before,after,416u,&delta));
    const uint8_t captured[][6] = {
        {0u,0xaau,0u,7u,0xe4u,2u},
        {0u,0x2eu,2u,15u,0x24u,2u},
        {0u,0xaeu,0u,15u,0x28u,2u},
        {0u,0x3eu,8u,15u,0x1cu,1u}
    };
    for (uint32_t i = 0u; i < sizeof(captured)/sizeof(captured[0]); ++i)
        assert(ref_sig_valid(captured[i]));
    uint8_t log[CLEAN_OBSERVER_WORDS*2u] = {1u,0u,1u};
    uint8_t sample[REF_SAMPLE_BYTES] = {0}, mac[6];
    ref_mac(mac,nonce);
    uint8_t *record = log+2u*CLEAN_OBSERVER_HEADER_WORDS, *mpdu = sample+16u;
    badger_bytes_store_u32(sample,170u);
    badger_bytes_store_u32(sample+4u,1u);
    memcpy(record+12u,captured[2],6u);
    record[20] = record[76] = 4u;
    record[22] = record[78] = 1u;
    record[28] = 176u;
    mpdu[0] = 0x88u; mpdu[1] = 0x41u;
    memcpy(mpdu+10u,mac,6u);
    badger_bytes_store_u32(mpdu+166u,diag_crc32(mpdu,166u));
    badger_bytes_store_u32(sample+12u,diag_crc32(mpdu,166u));
    memcpy(record+36u,mpdu,40u);
    assert(ref_sample_correlated(log,sample,mac));
    for (uint32_t outcome = 0u; outcome <= 8u; ++outcome) {
        assert(ref_observer_outcome_name(outcome));
        record[22] = (uint8_t)outcome;
        assert(ref_sample_correlated(log,sample,mac) == (outcome == 1u));
    }
    assert(!ref_observer_outcome_name(9u) && !ref_observer_outcome_name(65535u));
    record[22] = 1u;
    for (uint32_t fault = 0u; fault < 12u; ++fault) {
        uint8_t saved_log[sizeof(log)], saved_sample[sizeof(sample)];
        memcpy(saved_log,log,sizeof(log)); memcpy(saved_sample,sample,sizeof(sample));
        if (fault == 0u) sample[4] = 0u;
        if (fault == 1u) sample[4] = 17u;
        if (fault == 2u) sample[1] = 255u;
        if (fault == 3u) record[12] ^= 1u;
        if (fault == 4u) record[22] = 2u;
        if (fault == 5u) record[28] ^= 1u;
        if (fault == 6u) record[76] ^= 1u;
        if (fault == 7u) record[78] ^= 1u;
        if (fault == 8u) record[36] ^= 1u;
        if (fault == 9u) mpdu[10] ^= 1u;
        if (fault == 10u) mpdu[60] ^= 1u;
        if (fault == 11u) sample[12] ^= 1u;
        assert(!ref_sample_correlated(log,sample,mac));
        memcpy(log,saved_log,sizeof(log)); memcpy(sample,saved_sample,sizeof(sample));
    }
    uint8_t early[CLEAN_OBSERVER_EARLY_WORDS*2u] = {0};
    early[0] = 1u; early[2] = 4u; early[4] = 176u; early[7] = 1u;
    early[20] = 1u;
    memcpy(early+14u,captured[2],6u);
    memcpy(early+CLEAN_OBSERVER_EARLY_HEADER*2u,mpdu,170u);
    assert(ref_early_correlated(log,early,mac));
    record[22] = 5u;
    memcpy(early+14u,captured[3],6u); memcpy(record+12u,captured[3],6u);
    early[20] = 2u;
    assert(ref_early_correlated(log,early,mac));
    for (uint32_t fault = 0u; fault < 15u; ++fault) {
        uint8_t saved[sizeof(early)];
        memcpy(saved,early,sizeof(saved));
        if (fault == 0u) early[0] = 0u;
        if (fault == 1u) early[0] = 17u;
        if (fault == 2u) early[4] = 49u;
        if (fault == 3u) { early[4] = 255u; early[5] = 255u; }
        if (fault == 4u) early[2] ^= 1u;
        if (fault == 5u) early[14] ^= 1u;
        if (fault == 6u) early[7] = 0u;
        if (fault == 7u) early[7] |= 0x40u;
        if (fault == 8u) early[7] |= 0x80u;
        if (fault == 9u) early[20] = 0u;
        if (fault == 10u) early[20] = 3u;
        if (fault == 11u) early[CLEAN_OBSERVER_EARLY_HEADER*2u+10u] ^= 1u;
        if (fault == 12u) early[CLEAN_OBSERVER_EARLY_HEADER*2u+60u] ^= 1u;
        if (fault == 13u) early[CLEAN_OBSERVER_EARLY_HEADER*2u+169u] ^= 1u;
        if (fault == 14u) early[20] = 1u;
        assert(!ref_early_correlated(log,early,mac));
        memcpy(early,saved,sizeof(saved));
    }
    puts("Reference: fixtures/digest, HT-SIG CRCs, counter bounds, fresh F2 correlation and independent early snapshot/FCS/epoch/profile guards pass");
    return 0;
}

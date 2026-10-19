#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "protocol.h"
#include "../src/ht20.h"

static void delivery_accounting(void) {
    uint8_t trace[REF_TRACE_BYTES];
    const uint32_t values[] = {0u,1u,2u,65534u,65535u,65536u,0xfffffffeu,0xffffffffu};
    for (uint32_t p = 0u; p < sizeof(values)/sizeof(values[0]); ++p)
    for (uint32_t d = 0u; d < sizeof(values)/sizeof(values[0]); ++d)
    for (uint32_t pending = 0u; pending <= 1u; ++pending) {
        uint32_t posted = values[p], dropped = values[d];
        if (dropped > 0xffffffffu-posted || pending > posted) continue;
        memset(trace,0,sizeof(trace));
        uint8_t *f2 = trace+REF_MBOX_TAIL+444u-REF_MBOX_HEAD_BYTES;
        uint32_t consumed = posted-pending;
        badger_bytes_store_u32(trace+128u,0x4f505258u);
        badger_bytes_store_u32(trace+REF_MULTI_DETAIL,0x4d554c54u);
        badger_bytes_store_u32(trace+REF_MULTI_DETAIL+8u,posted+dropped);
        badger_bytes_store_u32(f2,(posted < 65535u ? posted : 65535u) |
            ((dropped < 65535u ? dropped : 65535u)<<16u));
        badger_bytes_store_u32(f2+4u,consumed);
        badger_bytes_store_u32(f2+8u,posted);
        badger_bytes_store_u32(trace+REF_LOADER_DETAIL,consumed);
        badger_bytes_store_u32(trace+60u,consumed);
        badger_bytes_store_u32(trace+REF_LOADER_DETAIL+28u,REF_RX_SUMMARY_COUNT);
        assert(ref_delivery_accounted(trace));
        const uint32_t corrupt[] = {128u,REF_MULTI_DETAIL,
            REF_MBOX_TAIL+444u-REF_MBOX_HEAD_BYTES,
            REF_MBOX_TAIL+446u-REF_MBOX_HEAD_BYTES,
            REF_LOADER_DETAIL,60u};
        for (uint32_t i = 0u; i < sizeof(corrupt)/sizeof(corrupt[0]); ++i) {
            trace[corrupt[i]] ^= 1u;
            assert(!ref_delivery_accounted(trace));
            trace[corrupt[i]] ^= 1u;
        }
        badger_bytes_store_u32(trace+REF_LOADER_DETAIL+28u,REF_RX_SUMMARY_COUNT+1u);
        assert(!ref_delivery_accounted(trace));
        badger_bytes_store_u32(trace+REF_LOADER_DETAIL+28u,REF_RX_SUMMARY_COUNT);
        if (posted) {
            badger_bytes_store_u32(trace+REF_MULTI_DETAIL+8u,posted-1u);
            assert(!ref_delivery_accounted(trace));
        }
    }
    memset(trace,0,sizeof(trace));
    badger_bytes_store_u32(trace+128u,0x4f505258u);
    badger_bytes_store_u32(trace+REF_MULTI_DETAIL,0x4d554c54u);
    uint8_t *f2 = trace+REF_MBOX_TAIL+444u-REF_MBOX_HEAD_BYTES;
    badger_bytes_store_u32(trace+REF_MULTI_DETAIL+8u,2u);
    badger_bytes_store_u32(f2,2u);
    badger_bytes_store_u32(f2+8u,2u);
    assert(!ref_delivery_accounted(trace));
    badger_bytes_store_u32(f2+8u,0u);
    badger_bytes_store_u32(f2+4u,0xffffffffu);
    badger_bytes_store_u32(trace+REF_LOADER_DETAIL,0xffffffffu);
    badger_bytes_store_u32(trace+60u,0xffffffffu);
    assert(!ref_delivery_accounted(trace));
    puts("Reference frozen delivery: full capture/post/drop/consume accounting, one-frame pending window, saturation and corrupt reports pass");
}

int main(void) {
    delivery_accounting();
    uint8_t frame[REF_FRAME_MAX+8u], nonce[16], peer[6] = {2u,1u,2u,3u,4u,5u};
    for (uint32_t i = 0u; i < 16u; ++i) nonce[i] = (uint8_t)i;
    uint32_t counts[4] = {0}, first_counts[4] = {0}, digest = REF_DIGEST_INITIAL;
    const uint32_t expected[16] = {1024u,1024u,1024u,1024u,
        128u,256u,128u,256u,128u,256u,128u,256u,512u,512u,512u,512u};
    for (uint32_t i = 0u; i < REF_FRAME_COUNT; ++i) {
        memset(frame,0xa5,sizeof(frame));
        uint32_t size = ref_frame(frame,nonce,peer,i);
        uint32_t index = size == 128u ? 0u : size == 256u ? 1u : size == 512u ? 2u : 3u;
        assert(size == expected[i&15u]);
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
        assert(counts[i] == 32u && first_counts[i] == (i == 0u ? 16u : i == 1u ? 0u : 8u));
    assert(digest != REF_DIGEST_INITIAL);
    assert(!ref_frame(frame,nonce,peer,REF_FRAME_COUNT));
    uint8_t packet[18u+CLEAN_MPDU_BYTES], summary[REF_RX_SUMMARY_BYTES+8u];
    for (uint32_t i = 0u; i < sizeof(packet); ++i) packet[i] = (uint8_t)(i*13u);
    for (uint32_t length = 4u; length <= CLEAN_MPDU_BYTES; ++length) {
        badger_bytes_store_u32(packet+18u+length-4u,diag_crc32(packet+18u,length-4u));
        memset(summary,0xa5,sizeof(summary));
        assert(ref_rx_summary(summary,packet,18u+length,17u));
        assert(badger_bytes_load_u32(summary) == length);
        assert(!memcmp(summary+4u,packet+14u,4u));
        assert(badger_bytes_load_u32(summary+8u) == badger_bytes_load_u32(summary+12u));
        uint32_t copied = length < 40u ? length : 40u;
        assert(!memcmp(summary+16u,packet+18u,copied));
        for (uint32_t i = copied; i < 40u; ++i) assert(!summary[16u+i]);
        assert(badger_bytes_load_u32(summary+56u) == 17u);
        assert(!memcmp(summary+60u,packet+4u,4u));
        for (uint32_t i = REF_RX_SUMMARY_BYTES; i < sizeof(summary); ++i)
            assert(summary[i] == 0xa5u);
    }
    packet[18u] ^= 1u;
    assert(ref_rx_summary(summary,packet,sizeof(packet),18u));
    assert(badger_bytes_load_u32(summary+8u) != badger_bytes_load_u32(summary+12u));
    uint8_t saved_summary[sizeof(summary)];
    memcpy(saved_summary,summary,sizeof(summary));
    assert(!ref_rx_summary(summary,packet,21u,0u));
    assert(!ref_rx_summary(summary,packet,sizeof(packet)+1u,0u));
    assert(!memcmp(summary,saved_summary,sizeof(summary)));
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
#if CLEAN_RX_HT_ADVANCE
    const uint8_t pair_sig[6] = {0u,0xdeu,1u,15u,0xb0u,3u};
    assert(ref_sig_valid(pair_sig));
    memcpy(early+14u,pair_sig,6u); memcpy(record+12u,pair_sig,6u);
    early[24] = 1u; early[26] = 0x2au; early[27] = 1u;
    record[22] = 1u; record[28] = 0x30u; record[29] = 1u; record[33] = 0x40u;
    badger_bytes_store_u32(sample,298u);
    badger_bytes_store_u32(sample+8u,REF_ADVANCED_SAMPLE);
    memset(mpdu,0,298u);
    memcpy(mpdu,early+CLEAN_OBSERVER_EARLY_HEADER*2u,170u);
    mpdu[22] = 16u;
    badger_bytes_store_u32(mpdu+294u,diag_crc32(mpdu,294u));
    badger_bytes_store_u32(sample+12u,diag_crc32(mpdu,294u));
    memcpy(record+36u,mpdu,40u);
    assert(ref_pair_correlated(log,early,sample,mac));
    for (uint32_t fault = 0u; fault < 13u; ++fault) {
        uint8_t saved_early[sizeof(early)], saved_sample[sizeof(sample)], saved_log[sizeof(log)];
        memcpy(saved_early,early,sizeof(early));
        memcpy(saved_sample,sample,sizeof(sample)); memcpy(saved_log,log,sizeof(log));
        if (fault == 0u) early[24] = 0u;
        if (fault == 1u) early[26] ^= 1u;
        if (fault == 2u) sample[9] = 0u;
        if (fault == 3u) sample[4] = 2u;
        if (fault == 4u) mpdu[22] = early[CLEAN_OBSERVER_EARLY_HEADER*2u+22u];
        if (fault == 5u) mpdu[24] ^= 1u;
        if (fault == 6u) record[33] = 0u;
        if (fault == 7u) early[0] = 0u;
        if (fault == 8u) mpdu[22] |= 1u;
        if (fault == 9u) mpdu[4] ^= 2u;
        if (fault == 10u) mpdu[1] ^= 2u;
        if (fault == 11u) {
            memcpy(early+14u,captured[3],6u); memcpy(record+12u,captured[3],6u);
            assert(ref_early_correlated(log,early,mac) && ref_sample_correlated(log,sample,mac));
        }
        if (fault == 12u) record[33] |= 0x80u;
        if (fault == 4u || fault == 5u || (fault >= 8u && fault <= 10u)) {
            badger_bytes_store_u32(mpdu+294u,diag_crc32(mpdu,294u));
            badger_bytes_store_u32(sample+12u,diag_crc32(mpdu,294u));
            memcpy(record+36u,mpdu,40u);
            assert(ref_sample_correlated(log,sample,mac));
        }
        assert(!ref_pair_correlated(log,early,sample,mac));
        memcpy(early,saved_early,sizeof(early));
        memcpy(sample,saved_sample,sizeof(sample)); memcpy(log,saved_log,sizeof(log));
    }
    uint8_t first_body[170u];
    uint8_t *early_body = early+CLEAN_OBSERVER_EARLY_HEADER*2u;
    memcpy(first_body,early_body,sizeof(first_body));
    memcpy(early_body,mpdu,298u); memcpy(mpdu,first_body,sizeof(first_body));
    early[4] = 0x30u; early[5] = 1u; early[26] = 170u; early[27] = 0u;
    badger_bytes_store_u32(sample,170u);
    badger_bytes_store_u32(sample+12u,diag_crc32(mpdu,166u));
    record[28] = 176u; record[29] = 0u;
    memcpy(record+36u,mpdu,40u);
    assert(ref_pair_correlated(log,early,sample,mac));
#endif
    uint8_t pool[REF_SAMPLES*REF_SAMPLE_BYTES] = {0};
    assert(ref_sample_slot(pool,0u,170u,1u,0u) == 0u);
    badger_bytes_store_u32(pool,170u); badger_bytes_store_u32(pool+4u,1u);
    assert(ref_sample_slot(pool,1u,298u,2u,0u) == 1u);
    assert(ref_sample_slot(pool,1u,170u,1u,0u) == 0u);
    assert(ref_sample_slot(pool,1u,170u,2u,0u) == 0u);
    assert(ref_sample_slot(pool,1u,170u,0u,0u) == REF_SAMPLES);
    badger_bytes_store_u32(pool+REF_SAMPLE_BYTES,554u);
    assert(ref_sample_slot(pool,2u,298u,3u,0u) == REF_SAMPLES);
    assert(ref_sample_slot(pool,2u,298u,3u,1u) == 0u);
    badger_bytes_store_u32(pool+8u,REF_ADVANCED_SAMPLE);
    assert(ref_sample_slot(pool,2u,170u,3u,0u) == REF_SAMPLES);
    assert(ref_sample_slot(pool,2u,170u,3u,1u) == REF_SAMPLES);
    assert(ref_sample_slot(pool,2u,298u,3u,1u) == 1u);
    badger_bytes_store_u32(pool+REF_SAMPLE_BYTES+8u,REF_ADVANCED_SAMPLE);
    assert(ref_sample_slot(pool,2u,298u,3u,1u) == REF_SAMPLES);
    puts("Reference: fixture/profile/FCS correlation and immutable proof sample retention pass");
#if CLEAN_RX_HT_ADVANCE
    puts("Reference bounded pair: distinct-body/length/TID/address/fragment/PSDU/EOF/advance guards pass");
#endif
    return 0;
}

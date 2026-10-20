#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "early_rx.h"
#include "../vendor-burst/protocol.h"

static void admit(uint32_t length) {
    uint16_t h[CLEAN_OBSERVER_EARLY_HEADER] = {0}, prefix[CLEAN_OBSERVER_RECORD_WORDS] = {0};
    uint8_t raw[8u+CLEAN_OBSERVER_EARLY_MAX+2u], sig[6];
    memset(raw,0x71,sizeof(raw));
    assert(clean_ht20_ampdu_sig(sig,8u+((length+3u)&~3u),0u));
    h[0] = 2u; h[1] = prefix[10] = 7u; h[2] = (uint16_t)(length+6u);
    h[3] = 0x1922u; h[10] = 2u; h[12] = 2u;
    for (uint32_t i = 0u; i < 3u; ++i)
        h[7u+i] = prefix[6u+i] = (uint16_t)(sig[2u*i] | ((uint16_t)sig[2u*i+1u]<<8u));
    raw[8u] = 8u;
    for (uint32_t i = 1u; i < length-4u; ++i) raw[8u+i] = (uint8_t)(i^length);
    badger_bytes_store_u32(raw+8u+length-4u,clean_mac_fcs(raw+8u,length-4u));
    assert(clean_observer_early_length(h) == (int)length);
    assert(clean_observer_early_decode(h,prefix,raw) == (int)length);
    assert(!raw[0] && !raw[1] && !memcmp(raw+2u,sig,6u));
    assert(raw[8u+length] == 0x71u && raw[9u+length] == 0x71u);
    for (uint32_t fault = 0u; fault < 11u; ++fault) {
        uint16_t bad[CLEAN_OBSERVER_EARLY_HEADER];
        memcpy(bad,h,sizeof(bad));
        if (fault == 0u) bad[0] = 17u;
        if (fault == 1u) bad[1] ^= 1u;
        if (fault == 2u) bad[2] = 49u;
        if (fault == 3u) bad[2] = CLEAN_OBSERVER_EARLY_MAX+7u;
        if (fault == 4u) bad[3] &= ~0x100u;
        if (fault == 5u) bad[3] |= 0x4000u;
        if (fault == 6u) bad[3] |= 0x8000u;
        if (fault == 7u) bad[7] ^= 1u;
        if (fault == 8u) bad[8] ^= 1u;
        if (fault == 9u) bad[9] ^= 1u;
        if (fault == 10u) bad[10] = 1u;
        assert(clean_observer_early_decode(bad,prefix,raw) <= 0);
    }
    raw[8u+length-1u] ^= 1u;
    assert(clean_observer_early_decode(h,prefix,raw) == -2);
    h[0] = 0u;
    assert(!clean_observer_early_decode(h,prefix,raw));
}

static void correlation(void) {
    uint8_t nonce[14] = {1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,3u,0u,6u};
    uint8_t fixture[CLEAN_HT_STIMULUS_STORAGE], mac[6];
    uint8_t log[CLEAN_OBSERVER_WORDS*2u] = {0}, early[CLEAN_OBSERVER_EARLY_WORDS*2u] = {0};
    uint8_t sample[REF_SAMPLE_BYTES] = {0}, packet[18u+CLEAN_HT_STIMULUS_MPDU] = {0};
    assert(clean_ht_stimulus_frame(fixture,nonce,2u));
    ref_mac(mac,nonce);
    log[0] = log[2] = early[0] = early[2] = 1u;
    uint8_t *record = log+2u*CLEAN_OBSERVER_HEADER_WORDS;
    record[20] = 1u;
    memcpy(record+12u,fixture+CLEAN_HT_DATA_HEADER,6u);
    memcpy(early+14u,record+12u,6u);
    early[4u] = (uint8_t)(CLEAN_HT_STIMULUS_MPDU+6u);
    early[5u] = (uint8_t)((CLEAN_HT_STIMULUS_MPDU+6u)>>8u);
    early[7u] = 1u; early[20u] = early[24u] = 2u;
    early[26u] = 1u;
    const uint8_t *body = fixture+CLEAN_HT_DATA_HEADER+10u;
    memcpy(early+2u*CLEAN_OBSERVER_EARLY_HEADER,body,CLEAN_HT_STIMULUS_MPDU);
    badger_bytes_store_u32(sample,CLEAN_HT_STIMULUS_MPDU);
    sample[4] = 1u;
    badger_bytes_store_u32(sample+8u,REF_EARLY_SAMPLE|CLEAN_RX_UNSUPPORTED_PHY|6u);
    badger_bytes_store_u32(sample+12u,diag_crc32(body,CLEAN_HT_STIMULUS_MPDU-4u));
    memcpy(sample+16u,body,CLEAN_HT_STIMULUS_MPDU);
    assert(ref_early_correlated(log,early,mac));
    assert(ref_early_f2_correlated(log,early,sample,mac,6u));
    early[26u] = 0u;
    assert(!ref_early_f2_correlated(log,early,sample,mac,6u));
    early[26u] = 1u;
    assert(!ref_early_f2_correlated(log,early,sample,mac,7u));
    assert(!ref_sample_correlated(log,sample,mac));
    for (uint32_t i = 0u; i < 16u+CLEAN_HT_STIMULUS_MPDU; ++i) {
        sample[i] ^= 1u;
        assert(!ref_early_f2_correlated(log,early,sample,mac,6u));
        sample[i] ^= 1u;
    }
    packet[10] = 1u; packet[11] = (uint8_t)(CLEAN_OBSERVER_EARLY_TAG>>8u);
    packet[16] = CLEAN_RX_UNSUPPORTED_PHY|6u;
    memcpy(packet+18u,body,CLEAN_HT_STIMULUS_MPDU);
    assert(ref_early_stimulus_rx_index(packet,sizeof(packet),nonce) == 2u);
    packet[16] = CLEAN_RX_OFDM|6u;
    assert(ref_early_stimulus_rx_index(packet,sizeof(packet),nonce) == CLEAN_HT_STIMULUS_COUNT);
    packet[16] = 6u;
    assert(ref_early_stimulus_rx_index(packet,sizeof(packet),nonce) == CLEAN_HT_STIMULUS_COUNT);
    packet[16] = CLEAN_RX_UNSUPPORTED_PHY|6u;
    for (uint32_t i = 10u; i < sizeof(packet); ++i) {
        if (i >= 12u && i <= 14u) continue;
        packet[i] ^= 0x80u;
        assert(ref_early_stimulus_rx_index(packet,sizeof(packet),nonce) == CLEAN_HT_STIMULUS_COUNT);
        packet[i] ^= 0x80u;
    }
    assert(ref_sample_slot(sample,1u,CLEAN_HT_STIMULUS_MPDU,1u,0u) == REF_SAMPLES);
    assert(ref_sample_slot(sample,1u,CLEAN_HT_STIMULUS_MPDU,2u,CLEAN_OBSERVER_EARLY_TAG) == 0u);
    assert(ref_sample_slot(sample,1u,CLEAN_HT_STIMULUS_MPDU,1u,CLEAN_OBSERVER_EARLY_TAG) == REF_SAMPLES);
    for (uint32_t phy = 0u; phy < 4u; ++phy) {
        early[24u] = (uint8_t)phy;
        assert(ref_early_f2_correlated(log,early,sample,mac,6u));
    }
}

static void archive(void) {
    uint8_t nonce[14] = {1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,3u,0u,6u};
    uint8_t exported[REF_EARLY_EXPORT_BYTES] = {0}, fixture[CLEAN_HT_STIMULUS_STORAGE];
    uint32_t mask = 0u;
    badger_bytes_store_u32(exported,REF_EARLY_EXPORT_MAGIC);
    assert(ref_early_export_valid(exported,nonce,0u,&mask) && !mask);
    for (uint32_t i = 0u; i < REF_EARLY_EXPORT_SLOTS; ++i) {
        uint32_t index = 3u*i+2u, size = 18u+CLEAN_HT_STIMULUS_MPDU;
        uint8_t *r = exported+REF_EARLY_EXPORT_HEADER+i*REF_EARLY_EXPORT_RECORD;
        uint8_t *p = r+12u;
        badger_bytes_store_u32(r,size);
        badger_bytes_store_u32(r+4u,i+1u);
        badger_bytes_store_u32(r+8u,index);
        p[0] = (uint8_t)size; p[1] = (uint8_t)(size>>8u);
        p[2] = (uint8_t)~size; p[3] = (uint8_t)(~size>>8u);
        p[4] = (uint8_t)i; p[5] = 2u; p[7] = 14u;
        p[10] = (uint8_t)(i+1u); p[11] = (uint8_t)(CLEAN_OBSERVER_EARLY_TAG>>8u);
        p[12] = (uint8_t)CLEAN_RX_PHASE_MAGIC; p[13] = (uint8_t)(CLEAN_RX_PHASE_MAGIC>>8u);
        p[14] = 0x20u; p[16] = CLEAN_RX_UNSUPPORTED_PHY|6u;
        assert(clean_ht_stimulus_frame(fixture,nonce,index));
        memcpy(p+18u,fixture+CLEAN_HT_DATA_HEADER+10u,CLEAN_HT_STIMULUS_MPDU);
        badger_bytes_store_u32(exported+4u,i+1u);
        badger_bytes_store_u32(exported+8u,i+1u);
        assert(ref_early_export_valid(exported,nonce,8u,&mask));
    }
    assert(mask == 9586980u);
    for (uint32_t i = 0u; i < sizeof(exported); ++i) {
        if (i >= 16u) {
            uint32_t field = (i-16u)%REF_EARLY_EXPORT_RECORD;
            if (field == 18u || field == 20u || field == 21u) continue;
        }
        exported[i] ^= 0x80u;
        assert(!ref_early_export_valid(exported,nonce,8u,&mask));
        exported[i] ^= 0x80u;
    }
}

int main(void) {
    _Static_assert(CLEAN_RX_HT_EARLY_FIFO, "Select the early FIFO profile");
    for (uint32_t length = 44u; length <= CLEAN_OBSERVER_EARLY_MAX; ++length) admit(length);
    correlation();
    archive();
    puts("{\"test\":\"early-fifo-admission\",\"passed\":true,\"rf_emulation\":false}");
    return 0;
}

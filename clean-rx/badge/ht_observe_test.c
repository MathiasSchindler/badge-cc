#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ht_observe.h"

int main(void) {
    for (uint32_t mode = 0u; mode <= 31u; ++mode) {
        uint8_t report[2092], before[2092], request[16] = {1u};
        request[11] = 60u; request[14] = (uint8_t)mode;
        memset(report, 0xa5, sizeof(report)); report[2000u] = (uint8_t)mode;
        badger_bytes_store_u32(report + 1088u, 78u);
        badger_bytes_store_u32(report + CLEAN_HT_TIMING_DETAIL + 8u, 1000u);
        clean_ht_reject_baseline_t baseline = {
            .magic = CLEAN_HT_REJECT_BASELINE_MAGIC, .generation = 1u,
            .maccontrol_before = 0x84020400u, .maccontrol_after = 0x84020400u,
            .begin_us = 100u, .end_us = 200u
        };
        memcpy(before, report, sizeof(report));
        int allowed = mode == 25u || mode == 27u;
        assert(clean_ht_reject_baseline_store(report, request, &baseline) == allowed);
        if (allowed) {
            assert(!memcmp(report + CLEAN_HT_REJECT_BASELINE_DETAIL, &baseline, sizeof(baseline)));
            memcpy(before + CLEAN_HT_REJECT_BASELINE_DETAIL, &baseline, sizeof(baseline));
        }
        assert(!memcmp(report, before, sizeof(report)));
        clean_ht_candidate_trace_t candidate = {.magic=CLEAN_HT_CANDIDATE_MAGIC};
        memcpy(before,report,sizeof(report));
        assert(clean_ht_candidate_store(report,request,&candidate)==allowed);
        if (allowed) {
            assert(!memcmp(report+CLEAN_HT_CANDIDATE_DETAIL,&candidate,sizeof(candidate)));
            memcpy(before+CLEAN_HT_CANDIDATE_DETAIL,&candidate,sizeof(candidate));
        }
        assert(!memcmp(report,before,sizeof(report)));
        if (!allowed) continue;
        for (uint32_t fault=0u;fault<5u;++fault) {
            clean_ht_candidate_trace_t bad=candidate;
            uint8_t nonce[16];memcpy(nonce,request,sizeof(nonce));
            if (fault==0u) bad.magic^=1u;
            if (fault==1u) bad.committed=2u;
            if (fault==2u) nonce[11]=28u;
            if (fault==3u) nonce[14]=24u;
            if (fault==4u) badger_bytes_store_u32(report+1088u,113u);
            memcpy(before,report,sizeof(report));
            assert(!clean_ht_candidate_store(report,nonce,&bad));
            assert(!memcmp(report,before,sizeof(report)));
        }
        badger_bytes_store_u32(report+1088u,78u);
        for (uint32_t fault = 0u; fault < 8u; ++fault) {
            clean_ht_reject_baseline_t bad = baseline;
            uint8_t nonce[16]; memcpy(nonce, request, sizeof(nonce));
            if (fault == 0u) bad.magic ^= 1u;
            if (fault == 1u) bad.generation ^= 1u;
            if (fault == 2u) bad.maccontrol_before |= 1u;
            if (fault == 3u) bad.maccontrol_after |= 2u;
            if (fault == 4u) bad.end_us = 1001u;
            if (fault == 5u) bad.reserved = 1u;
            if (fault == 6u) nonce[11] = 28u;
            if (fault == 7u) badger_bytes_store_u32(report + 1088u, 417u);
            memcpy(before, report, sizeof(report));
            assert(!clean_ht_reject_baseline_store(report, nonce, &bad));
            assert(!memcmp(report, before, sizeof(report)));
        }
    }
    for (uint32_t mode=0u;mode<=31u;++mode) {
        uint8_t detail[2092],before[2092],entry[40];
        memset(detail,0xa5,sizeof(detail)); memset(entry,0x5a,sizeof(entry));
        detail[2000u]=(uint8_t)mode; badger_bytes_store_u32(detail+1632u,0u);
        memcpy(before,detail,sizeof(detail));
        for (uint32_t at=0u;at<9u;++at) {
            entry[0]=(uint8_t)at;
            clean_record_beacon_entry(detail,entry);
        }
        if ((mode>=24u && mode<=28u) || mode==30u || mode==31u)
            assert(!memcmp(detail,before,sizeof(detail)));
        else {
            assert(badger_bytes_load_u32(detail+1632u)==8u);
            assert(detail[1640u]==0u && detail[1920u]==7u);
        }
    }
    uint8_t detail[2092]={0},nonce[14]={1u,2u,3u,4u,5u};
    uint8_t packet[46]={0};
    detail[2000u]=25u;
    badger_bytes_store_u32(detail+CLEAN_HT_F2_DETAIL,CLEAN_HT_F2_MAGIC);
    packet[14]=0x20u; packet[15]=7u; packet[16]=0xc6u; packet[17]=1u;
    packet[18]=8u; packet[28]=2u; memcpy(packet+29u,nonce,5u);
    packet[40]=0x10u; packet[41]=0x20u;
    badger_bytes_store_u32(packet+42u,diag_crc32(packet+18u,24u));
    for (uint32_t size=0u;size<42u;++size)
        clean_ht_observe_f2(detail,packet,size,nonce,123u);
    assert(!badger_bytes_load_u32(detail+CLEAN_HT_F2_DETAIL+4u));
    nonce[0]^=1u; clean_ht_observe_f2(detail,packet,sizeof(packet),nonce,123u); nonce[0]^=1u;
    assert(!badger_bytes_load_u32(detail+CLEAN_HT_F2_DETAIL+4u));
    clean_ht_observe_f2(detail,packet,sizeof(packet),nonce,123u);
    uint8_t *f2=detail+CLEAN_HT_F2_DETAIL;
    assert(badger_bytes_load_u32(f2+4u)==1u && badger_bytes_load_u32(f2+8u)==46u);
    assert(badger_bytes_load_u32(f2+12u)==123u && !memcmp(f2+24u,packet+14u,32u));
    assert(badger_bytes_load_u32(f2+16u)==badger_bytes_load_u32(f2+20u));
    packet[18]^=1u;
    clean_ht_observe_f2(detail,packet,sizeof(packet),nonce,456u);
    assert(badger_bytes_load_u32(f2+4u)==2u && badger_bytes_load_u32(f2+12u)==123u);
    badger_bytes_store_u32(f2+4u,0u);
    clean_ht_observe_f2(detail,packet,sizeof(packet),nonce,789u);
    assert(badger_bytes_load_u32(f2+16u)!=badger_bytes_load_u32(f2+20u));
    assert(f2[28u]==packet[18u]);
    uint8_t ack[14]={0xd4u,0u,0u,0u,2u,1u,2u,3u,4u,5u};
    badger_bytes_store_u32(ack+10u,diag_crc32(ack,10u));
    for (uint32_t size=0u;size<14u;++size) {
        clean_ht_observe_ack(detail,ack,size,nonce);
        assert(!badger_bytes_load_u32(detail+CLEAN_HT_WITNESS_ACK_DETAIL));
    }
    for (uint32_t at=0u;at<14u;++at) {
        ack[at]^=1u; clean_ht_observe_ack(detail,ack,14u,nonce); ack[at]^=1u;
        assert(!badger_bytes_load_u32(detail+CLEAN_HT_WITNESS_ACK_DETAIL));
    }
    nonce[0]^=1u; clean_ht_observe_ack(detail,ack,14u,nonce); nonce[0]^=1u;
    assert(!badger_bytes_load_u32(detail+CLEAN_HT_WITNESS_ACK_DETAIL));
    clean_ht_observe_ack(detail,ack,14u,nonce);
    assert(badger_bytes_load_u32(detail+CLEAN_HT_WITNESS_ACK_DETAIL)==CLEAN_HT_WITNESS_ACK_MAGIC);
    assert(!memcmp(detail+CLEAN_HT_WITNESS_ACK_DETAIL+8u,ack,14u));
    clean_ht_observe_ack(detail,ack,14u,nonce);
    assert(badger_bytes_load_u32(detail+CLEAN_HT_WITNESS_ACK_DETAIL+4u)==2u);
    puts("HT report ownership and passive ACK witness: all beacon slots/modes, FCS/address, truncation and stale identity pass");
    return 0;
}

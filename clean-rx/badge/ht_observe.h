#ifndef CLEAN_BADGE_HT_OBSERVE_H
#define CLEAN_BADGE_HT_OBSERVE_H

#include "loader.h"
#include "frames.h"
#include "../tests/fixtures/ht20.h"

typedef struct {
    uint32_t magic, count, packet_bytes, elapsed_us, calculated_fcs, received_fcs;
    uint8_t metadata[4], mpdu[64];
} clean_ht_f2_observation_t;

_Static_assert(CLEAN_HT_OBSERVER_DETAIL + 4u <= CLEAN_HT_F2_DETAIL &&
               CLEAN_HT_F2_DETAIL + sizeof(clean_ht_f2_observation_t) <= 1960u,
               "Correlated F2 evidence must fit the HT-owned report gap");

static inline int clean_ht_reject_baseline_store(uint8_t detail[2092],
        const uint8_t nonce[16], const clean_ht_reject_baseline_t *record) {
    if ((nonce[14] != 25u && nonce[14] != 27u) || detail[2000u] != nonce[14] ||
        clean_ht_data_length(nonce) != CLEAN_HT_ACK_MPDU ||
        record->magic != CLEAN_HT_REJECT_BASELINE_MAGIC ||
        record->generation != clean_request_generation(nonce) ||
        (record->maccontrol_before & 3u) || (record->maccontrol_after & 3u) ||
        record->reserved || record->begin_us > record->end_us ||
        record->end_us > badger_bytes_load_u32(detail + CLEAN_HT_TIMING_DETAIL + 8u) ||
        badger_bytes_load_u32(detail + 1088u) > CLEAN_HT_REJECT_BASELINE_DETAIL - 1104u)
        return 0;
    badger_bytes_copy(detail + CLEAN_HT_REJECT_BASELINE_DETAIL,
                     (const uint8_t *)record, sizeof(*record));
    return 1;
}

static inline int clean_ht_candidate_store(uint8_t detail[2092],
        const uint8_t nonce[16], const clean_ht_candidate_trace_t *record) {
    if ((nonce[14] != 25u && nonce[14] != 27u) || detail[2000u] != nonce[14] ||
        clean_ht_data_length(nonce) != CLEAN_HT_ACK_MPDU ||
        record->magic != CLEAN_HT_CANDIDATE_MAGIC || record->committed > 1u ||
        badger_bytes_load_u32(detail + 1088u) > CLEAN_HT_CANDIDATE_DETAIL - 1104u)
        return 0;
    badger_bytes_copy(detail + CLEAN_HT_CANDIDATE_DETAIL, (const uint8_t *)record, sizeof(*record));
    return 1;
}

static inline void clean_ht_observe_f2(uint8_t detail[2092], const uint8_t *packet,
        uint32_t size, const uint8_t nonce[14], uint32_t elapsed_us) {
    if ((detail[2000u]!=25u && detail[2000u]!=27u) || size<42u ||
        size>CLEAN_RX_PACKET_BYTES) return;
    const uint8_t *mpdu=packet+18u;
    if (mpdu[10]!=2u || !badger_bytes_equal(mpdu+11u,nonce,5u) ||
        mpdu[22]!=(uint8_t)(nonce[0]<<4u) ||
        mpdu[23]!=(uint8_t)((nonce[0]>>4u)|(nonce[1]<<4u))) return;
    uint8_t *record=detail+CLEAN_HT_F2_DETAIL;
    uint32_t count=badger_bytes_load_u32(record+4u);
    if (!count) {
        uint32_t length=size-18u, prefix=length<64u?length:64u;
        badger_bytes_store_u32(record+8u,size);
        badger_bytes_store_u32(record+12u,elapsed_us);
        badger_bytes_store_u32(record+16u,diag_crc32(mpdu,length-4u));
        badger_bytes_store_u32(record+20u,badger_bytes_load_u32(mpdu+length-4u));
        badger_bytes_copy(record+24u,packet+14u,4u);
        badger_bytes_copy(record+28u,mpdu,prefix);
    }
    badger_bytes_store_u32(record+4u,count+1u);
}

static inline void clean_ht_observe_ack(uint8_t detail[2092], const uint8_t *mpdu,
        uint32_t length, const uint8_t nonce[14]) {
    uint8_t own[6]={2u,nonce[0],nonce[1],nonce[2],nonce[3],nonce[4]};
    if (!clean_ack_reply(mpdu,length,own)) return;
    uint8_t *record=detail+CLEAN_HT_WITNESS_ACK_DETAIL;
    uint32_t count=badger_bytes_load_u32(record+4u);
    if (!count) badger_bytes_copy(record+8u,mpdu,14u);
    badger_bytes_store_u32(record+4u,count+1u);
    badger_bytes_store_u32(record,CLEAN_HT_WITNESS_ACK_MAGIC);
}

#endif

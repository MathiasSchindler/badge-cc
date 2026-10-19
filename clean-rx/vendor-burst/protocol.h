#ifndef CLEAN_VENDOR_BURST_PROTOCOL_H
#define CLEAN_VENDOR_BURST_PROTOCOL_H
#include "../../src/shared/bytes.h"
#include "../../src/tools/diagnostics_logic.h"
#include "../src/frame_limits.h"
#include "../src/ht_observer.h"

#define REF_MAGIC 0x31524648u
#define REF_TRACE_BYTES 5888u
#define REF_FRAME_COUNT 128u
#define REF_FRAME_MAX 1024u
#define REF_QUERY_BASE 128u
#define REF_QUERY_BYTES 32u
#define REF_QUERY_COUNT 19u
#define REF_COUNTER_BYTES 1536u
#define REF_COUNTER_BEFORE 1024u
#define REF_COUNTER_AFTER 2560u
#define REF_AMPDU_BEFORE 4096u
#define REF_AMPDU_AFTER 4608u
#define REF_DUMP_BEFORE 5120u
#define REF_DUMP_AFTER 5504u
#define REF_DUMP_BYTES 384u
#define REF_OBSERVER_DETAIL 1024u
#define REF_DIGEST_INITIAL 0x811c9dc5u
#define REF_OBSERVER_ABI 5u
#define REF_SAMPLE_BASE 2560u
#define REF_SAMPLE_BYTES 1104u
#define REF_SAMPLES 2u
#define REF_SAMPLE_MPDU_BYTES (REF_SAMPLE_BYTES - 16u)
#define REF_EARLY_BASE (REF_SAMPLE_BASE + REF_SAMPLES*REF_SAMPLE_BYTES)

_Static_assert(REF_OBSERVER_DETAIL+CLEAN_OBSERVER_WORDS*2u <= REF_SAMPLE_BASE,
               "Observer records must not overlap complete MPDU samples");
_Static_assert(REF_EARLY_BASE+CLEAN_OBSERVER_EARLY_WORDS*2u <= REF_TRACE_BYTES,
               "Complete MPDU samples and early snapshot must fit the trace transport");

static inline const char *ref_observer_outcome_name(uint32_t outcome) {
    static const char *const names[] = {
        "open","good","bad_fcs","mismatch_or_short","length_reject",
        "abort_or_fault","watchdog","new_rx","new_prefix"
    };
    return outcome < sizeof(names)/sizeof(names[0]) ? names[outcome] : 0;
}

static inline uint16_t ref_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static const char *const ref_queries[REF_QUERY_COUNT] = {
    "nmode", "nrate", "2g_rate", "mimo_bw_cap", "ampdu", "ampdu_tx",
    "ampdu_rx", "ampdu_mpdu", "ampdu_ba_wsize", "amsdu", "wme",
    "chanspec", "sta_info", "counters", "counters",
    "ampdu_counters", "ampdu_counters", "ampdu_dump", "ampdu_dump"
};

static inline void ref_mac(uint8_t mac[6], const uint8_t nonce[16]) {
    mac[0] = 2u;
    badger_bytes_copy(mac + 1u, nonce, 5u);
}

static inline uint32_t ref_frame(uint8_t frame[REF_FRAME_MAX], const uint8_t nonce[16],
                                 const uint8_t peer[6], uint32_t sequence) {
    static const uint16_t lengths[4] = {128u,256u,512u,1024u};
    if (sequence >= REF_FRAME_COUNT) return 0u;
    uint32_t size = lengths[(sequence + sequence / 4u) & 3u];
    badger_bytes_zero(frame, size);
    badger_bytes_copy(frame, peer, 6u);
    ref_mac(frame + 6u, nonce);
    frame[12] = 0x88u; frame[13] = 0xb5u;
    static const uint8_t tag[8] = {'H','T','R','E','F','0','0','1'};
    badger_bytes_copy(frame + 14u, tag, sizeof(tag));
    badger_bytes_store_u32(frame + 22u, sequence);
    badger_bytes_copy(frame + 26u, nonce, 12u);
    for (uint32_t i = 38u; i < size - 4u; ++i)
        frame[i] = (uint8_t)(nonce[i % 12u] ^ i ^ sequence);
    badger_bytes_store_u32(frame + size - 4u, diag_crc32(frame, size - 4u));
    return size;
}

static inline int ref_sig_valid(const uint8_t sig[6]) {
    uint32_t length = (uint32_t)sig[1] | ((uint32_t)sig[2] << 8u);
    if (!length || (sig[0] & 0x7fu) > 7u || (sig[0] & 0x80u) ||
        (sig[5] & 0xfcu) || !(sig[3] & 4u)) return 0;
    uint8_t crc = 255u;
    for (uint32_t i = 0u; i < 34u; ++i) {
        uint32_t feedback = (crc >> 7u) ^ ((sig[i / 8u] >> (i & 7u)) & 1u);
        crc = (uint8_t)((crc << 1u) ^ (feedback ? 7u : 0u));
    }
    crc = (uint8_t)~crc;
    for (uint32_t i = 0u; i < 8u; ++i)
        if (((sig[(34u+i)/8u] >> ((34u+i)&7u)) & 1u) != ((crc >> (7u-i)) & 1u))
            return 0;
    return 1;
}

static inline uint32_t ref_digest(uint32_t previous, const uint8_t *frame, uint32_t size) {
    return (previous * 16777619u) ^ diag_crc32(frame, size - 4u);
}

static inline int ref_sample_correlated(const uint8_t log[CLEAN_OBSERVER_WORDS*2u],
                                        const uint8_t sample[REF_SAMPLE_BYTES],
                                        const uint8_t mac[6]) {
    uint32_t length = badger_bytes_load_u32(sample), id = badger_bytes_load_u32(sample+4u);
    uint32_t count = ref_u16(log+2u);
    if (count > CLEAN_OBSERVER_RECORDS || !id || id > count ||
        length < 44u || length > REF_SAMPLE_MPDU_BYTES) return 0;
    const uint8_t *record = log+2u*CLEAN_OBSERVER_HEADER_WORDS+
        (id-1u)*2u*CLEAN_OBSERVER_RECORD_WORDS;
    const uint8_t *mpdu = sample+16u;
    uint32_t psdu = ref_u16(record+13u);
    return ref_u16(log) == 1u && ref_sig_valid(record+12u) &&
        ((record[15]&8u) ? psdu >= length+4u : psdu == length) &&
        ref_u16(record+22u) == 1u && ref_u16(record+28u) == length+6u &&
        ref_u16(record+20u) == ref_u16(record+76u) && ref_u16(record+78u) == id &&
        badger_bytes_equal(record+36u,mpdu,40u) &&
        badger_bytes_equal(mpdu+10u,mac,6u) &&
        (mpdu[0]&12u) == 8u &&
        diag_crc32(mpdu,length-4u) == badger_bytes_load_u32(mpdu+length-4u) &&
        diag_crc32(mpdu,length-4u) == badger_bytes_load_u32(sample+12u);
}

static inline int ref_counter_delta(const uint8_t before[REF_COUNTER_BYTES],
                                    const uint8_t after[REF_COUNTER_BYTES],
                                    uint32_t offset, uint32_t *delta) {
    /* Offsets are specific to the public 848-byte version-10 counter ABI. */
    if (before[0] != 10u || before[1] || after[0] != 10u || after[1] ||
        before[2] != 0x50u || before[3] != 3u || after[2] != 0x50u || after[3] != 3u ||
        offset < 4u || offset > 844u || (offset & 3u)) return 0;
    *delta = badger_bytes_load_u32(after + offset) - badger_bytes_load_u32(before + offset);
    return 1;
}

static inline int ref_early_correlated(const uint8_t log[CLEAN_OBSERVER_WORDS*2u],
                                       const uint8_t early[CLEAN_OBSERVER_EARLY_WORDS*2u],
                                       const uint8_t mac[6]) {
    uint32_t id = ref_u16(early), rxe = ref_u16(early+4u), count = ref_u16(log+2u);
    if (ref_u16(log) != 1u || count > CLEAN_OBSERVER_RECORDS || !id || id > count ||
        rxe < 50u || rxe > CLEAN_OBSERVER_EARLY_MAX+6u ||
        (ref_u16(early+6u)&0xc100u) != 0x100u) return 0;
    const uint8_t *record = log+2u*CLEAN_OBSERVER_HEADER_WORDS+
        (id-1u)*2u*CLEAN_OBSERVER_RECORD_WORDS;
    const uint8_t *mpdu = early+CLEAN_OBSERVER_EARLY_HEADER*2u;
    uint32_t length = rxe-6u, psdu = ref_u16(early+15u);
    uint32_t first_padded = 4u+((length+3u)&~3u), kind = ref_u16(early+20u);
    return early[14] == 0u && ref_sig_valid(early+14u) && (early[17]&8u) &&
        psdu >= length+4u && (kind == 1u ? psdu <= first_padded :
                              kind == 2u && psdu > first_padded) &&
        ref_u16(early+2u) == ref_u16(record+20u) &&
        badger_bytes_equal(early+14u,record+12u,6u) &&
        badger_bytes_equal(mpdu+10u,mac,6u) && (mpdu[0]&12u) == 8u &&
        diag_crc32(mpdu,length-4u) == badger_bytes_load_u32(mpdu+length-4u);
}

_Static_assert(REF_QUERY_BASE + REF_QUERY_COUNT * REF_QUERY_BYTES <= REF_COUNTER_BEFORE,
               "Query metadata must not overlap raw counter replies");
_Static_assert(REF_DUMP_AFTER + REF_DUMP_BYTES <= REF_TRACE_BYTES &&
    REF_OBSERVER_DETAIL + CLEAN_OBSERVER_WORDS * 2u <= REF_SAMPLE_BASE &&
    REF_SAMPLE_BASE + REF_SAMPLE_BYTES * REF_SAMPLES <= REF_TRACE_BYTES,
    "Reference evidence must fit the USB trace");
#endif

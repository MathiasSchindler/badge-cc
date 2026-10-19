#ifndef CLEAN_VENDOR_BURST_PROTOCOL_H
#define CLEAN_VENDOR_BURST_PROTOCOL_H
#include "../../src/shared/bytes.h"
#include "../../src/tools/diagnostics_logic.h"
#include "../src/frame_limits.h"
#include "../src/ht_observer.h"
#include "../src/sdio_ring.h"

#define REF_MAGIC 0x31524648u
#define REF_TRACE_BYTES 5888u
#define REF_FRAME_COUNT 128u
#define REF_FRAME_MAX 1024u
#define REF_EARLY_PREPARED 8u
#define REF_QUERY_BASE 128u
#define REF_QUERY_BYTES 32u
#define REF_QUERY_COUNT 19u
#define REF_SUBMIT_STATS (REF_QUERY_BASE + REF_QUERY_COUNT*REF_QUERY_BYTES)
#define REF_AMPDU_LIMIT 2u
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
#define REF_OBSERVER_ABI 18u
static inline int ref_timer_calibration_valid(uint32_t ticks, uint32_t frames) {
    return ticks && ticks <= 100000u && frames >= 32u && frames <= 1024u &&
        ticks*100u >= frames;
}
static inline uint64_t ref_timer_us(uint32_t value, uint32_t ticks, uint32_t frames) {
    return (uint64_t)value*frames*1000u/ticks;
}
#define REF_WINDOW_BASE 128u
#define REF_WINDOW_MAGIC 0x37444e57u
#define REF_WINDOW_INTERVAL 100000u
#define REF_WINDOW_RECORDS 35u
#define REF_WINDOW_RECORD_BYTES 160u
#define REF_WINDOW_HEADER_BYTES 96u
#define REF_WINDOW_BYTES (REF_WINDOW_HEADER_BYTES+REF_WINDOW_RECORDS*REF_WINDOW_RECORD_BYTES)
_Static_assert(REF_WINDOW_BASE+REF_WINDOW_BYTES <= REF_TRACE_BYTES,
    "Separate receive-window export must fit the existing trace transport");
static inline int ref_window_coherent(const uint8_t *record) {
    uint32_t sequence = badger_bytes_load_u32(record+140u);
    return sequence && !(sequence&1u) &&
        sequence == badger_bytes_load_u32(record+108u) &&
        sequence == badger_bytes_load_u32(record+144u);
}
static inline int ref_window_valid(const uint8_t *window) {
    uint32_t count = badger_bytes_load_u32(window+4u), end = 0u;
    uint32_t ticks = badger_bytes_load_u32(window+16u), frames = badger_bytes_load_u32(window+20u);
    if (badger_bytes_load_u32(window) != REF_WINDOW_MAGIC ||
        !count || count > REF_WINDOW_RECORDS ||
        badger_bytes_load_u32(window+12u) != REF_WINDOW_INTERVAL ||
        badger_bytes_load_u32(window+24u) > 1u ||
        badger_bytes_load_u32(window+28u) > 3u ||
        (badger_bytes_load_u32(window+28u) &&
         (!badger_bytes_load_u32(window+24u) ||
          !(badger_bytes_load_u32(window+28u)&1u))) ||
        !ref_timer_calibration_valid(ticks,frames)) return 0;
    if (!badger_bytes_load_u32(window+24u))
        for (uint32_t i = 32u; i < REF_WINDOW_HEADER_BYTES; ++i)
            if (window[i]) return 0;
    if (!badger_bytes_load_u32(window+28u))
        for (uint32_t i = 64u; i < REF_WINDOW_HEADER_BYTES; ++i)
            if (window[i]) return 0;
    for (uint32_t i = 0u; i < count; ++i) {
        const uint8_t *r = window+REF_WINDOW_HEADER_BYTES+i*REF_WINDOW_RECORD_BYTES;
        uint32_t time = badger_bytes_load_u32(r), duration = badger_bytes_load_u32(r+4u);
        if (time < end || time >= 45000000u || !duration ||
            duration >= 45000000u-time) return 0;
        end = time+duration;
    }
    for (uint32_t i = REF_WINDOW_HEADER_BYTES+count*REF_WINDOW_RECORD_BYTES; i < REF_WINDOW_BYTES; ++i)
        if (window[i]) return 0;
    return 1;
}
#define REF_PHY_SNAPSHOT 872u
_Static_assert(REF_PHY_SNAPSHOT+CLEAN_OBSERVER_PHY_BYTES <= REF_OBSERVER_DETAIL,
    "Stopped PHY snapshots must precede the observer log");
static inline int ref_phy_snapshot_valid(const uint8_t *snapshot) {
#if CLEAN_RX_HT_PHY_SNAPSHOT
    return badger_bytes_load_u32(snapshot) == CLEAN_OBSERVER_PHY_MAGIC &&
        badger_bytes_load_u32(snapshot+4u) == 3u;
#else
    for (uint32_t i = 0u; i < CLEAN_OBSERVER_PHY_BYTES; ++i)
        if (snapshot[i]) return 0;
    return 1;
#endif
}
#define REF_STOPPED_PSM 292u
#define REF_STOPPED_PSM_WORDS 12u
_Static_assert(REF_STOPPED_PSM+4u+2u*REF_STOPPED_PSM_WORDS <= 320u,
    "Stopped PSM state must precede the mailbox tail");
#define REF_HARDWARE_BASE 80u
#define REF_HARDWARE_WORDS 6u
static const uint32_t ref_hardware_addresses[REF_HARDWARE_WORDS] = {
    0x18000000u,0x180001e0u,0x180011e0u,0x18000608u,0x1800060cu,0x18000610u
};
_Static_assert(REF_HARDWARE_BASE+2u*REF_HARDWARE_WORDS*4u <= 128u,
    "Stopped hardware snapshots must precede the mailbox");
#define REF_MBOX_TAIL 320u
#define REF_MBOX_HEAD_BYTES 128u
#define REF_MBOX_BYTES 480u
#define REF_MULTI_DETAIL 672u
#define REF_LOADER_DETAIL 712u
#define REF_RX_SUMMARIES 744u
#define REF_RX_SUMMARY_BYTES 64u
#define REF_RX_SUMMARY_COUNT 2u
_Static_assert(292u <= REF_MBOX_TAIL &&
    REF_MBOX_TAIL + REF_MBOX_BYTES - REF_MBOX_HEAD_BYTES <= REF_MULTI_DETAIL &&
    REF_MULTI_DETAIL + 40u <= REF_LOADER_DETAIL &&
    REF_LOADER_DETAIL + 32u <= REF_RX_SUMMARIES &&
    REF_RX_SUMMARIES + REF_RX_SUMMARY_COUNT*REF_RX_SUMMARY_BYTES <= REF_PHY_SNAPSHOT,
    "Frozen receive diagnostics must not overlap existing trace regions");
static inline int ref_delivery_accounted(const uint8_t trace[REF_TRACE_BYTES]) {
    const uint8_t *f2 = trace+REF_MBOX_TAIL+444u-REF_MBOX_HEAD_BYTES;
    const uint8_t *loader = trace+REF_LOADER_DETAIL;
    uint32_t captured = badger_bytes_load_u32(trace+REF_MULTI_DETAIL+8u);
    uint32_t posted = badger_bytes_load_u32(f2+8u), consumed = badger_bytes_load_u32(f2+4u);
    uint32_t counts = badger_bytes_load_u32(f2);
    /* Reference counters start at zero; wraparound is not a bounded report. */
    if (badger_bytes_load_u32(trace+128u) != 0x4f505258u ||
        badger_bytes_load_u32(trace+REF_MULTI_DETAIL) != 0x4d554c54u ||
        posted > captured || consumed > posted ||
        clean_sdio_credit_available(posted,consumed,CLEAN_SDIO_WINDOW) < 0 ||
        consumed != badger_bytes_load_u32(loader) ||
        consumed != badger_bytes_load_u32(trace+60u) ||
        badger_bytes_load_u32(loader+28u) > REF_RX_SUMMARY_COUNT) return 0;
    uint32_t dropped = captured-posted;
    return (counts&0xffffu) == (posted < 0xffffu ? posted : 0xffffu) &&
        (counts>>16u) == (dropped < 0xffffu ? dropped : 0xffffu);
}
static inline int ref_rx_summary(uint8_t summary[REF_RX_SUMMARY_BYTES],
                                  const uint8_t *packet, uint32_t size, uint32_t ordinal) {
    if (size < 22u || size > 18u + CLEAN_MPDU_BYTES) return 0;
    uint32_t length = size - 18u, copied = length < 40u ? length : 40u;
    badger_bytes_zero(summary,REF_RX_SUMMARY_BYTES);
    badger_bytes_store_u32(summary,length);
    badger_bytes_copy(summary+4u,packet+14u,4u);
    badger_bytes_store_u32(summary+8u,diag_crc32(packet+18u,length-4u));
    badger_bytes_copy(summary+12u,packet+size-4u,4u);
    badger_bytes_copy(summary+16u,packet+18u,copied);
    badger_bytes_store_u32(summary+56u,ordinal);
    badger_bytes_copy(summary+60u,packet+4u,4u);
    return 1;
}
#define REF_SAMPLE_BASE 2560u
#define REF_SAMPLE_BYTES 1104u
#define REF_SAMPLES 2u
#define REF_SAMPLE_MPDU_BYTES (REF_SAMPLE_BYTES - 16u)
#define REF_EARLY_BASE (REF_SAMPLE_BASE + REF_SAMPLES*REF_SAMPLE_BYTES)
#define REF_SECOND_BASE (REF_EARLY_BASE + 2u*(CLEAN_OBSERVER_SECOND_SHM-CLEAN_OBSERVER_EARLY_SHM))
#define REF_HANDOFF_BASE (REF_EARLY_BASE + 2u*(CLEAN_OBSERVER_HANDOFF_SHM-CLEAN_OBSERVER_EARLY_SHM))
#define REF_ADVANCED_SAMPLE 0x100u
static inline uint32_t ref_sample_slot(const uint8_t *samples, uint32_t count,
                                       uint32_t length, uint32_t identity, uint32_t advanced) {
    for (uint32_t i = 0u; i < count; ++i) {
        const uint8_t *saved = samples+i*REF_SAMPLE_BYTES;
        if (badger_bytes_load_u32(saved) != length) continue;
        if ((badger_bytes_load_u32(saved+8u)&REF_ADVANCED_SAMPLE) || !identity)
            return REF_SAMPLES;
        return i;
    }
    if (count < REF_SAMPLES) return count;
    if (advanced) {
        for (uint32_t i = 0u; i < count; ++i)
            if (!(badger_bytes_load_u32(samples+i*REF_SAMPLE_BYTES+8u)&REF_ADVANCED_SAMPLE))
                return i;
    }
    return REF_SAMPLES;
}

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

static inline uint32_t ref_fixture_size(uint8_t profile) {
    return profile >= 1u && profile <= 3u ? 64u<<profile : 0u;
}

static inline uint32_t ref_frame(uint8_t frame[REF_FRAME_MAX], const uint8_t nonce[16],
                                 const uint8_t peer[6], uint32_t sequence) {
    static const uint16_t lengths[4] = {128u,256u,512u,1024u};
    if (sequence >= REF_FRAME_COUNT || nonce[11] > 3u) return 0u;
    uint32_t phase = sequence & 15u;
    uint32_t size = lengths[phase < 4u ? 3u : phase < 12u ? phase&1u : 2u];
    if (nonce[11]) size = ref_fixture_size(nonce[11]);
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

static inline int ref_prepare_early(uint8_t frames[REF_EARLY_PREPARED][REF_FRAME_MAX],
                                    uint32_t digests[REF_EARLY_PREPARED],
                                    const uint8_t nonce[16], const uint8_t peer[6]) {
    if (!ref_fixture_size(nonce[11])) return 0;
    uint32_t digest = REF_DIGEST_INITIAL;
    for (uint32_t i = 0u; i < REF_EARLY_PREPARED; ++i) {
        uint32_t size = ref_frame(frames[i],nonce,peer,i);
        if (!size) return 0;
        digests[i] = digest = ref_digest(digest,frames[i],size);
    }
    return 1;
}

static inline const char *ref_early_reason(const uint8_t *record, const uint8_t *early,
                                           uint32_t id) {
    const uint8_t *sig = record+12u;
    uint32_t hint = ref_u16(record+92u), psdu = ref_u16(sig+1u);
    if (!ref_sig_valid(sig)) return "invalid_ht_sig";
    if (!(sig[3]&8u)) return "not_ampdu_format";
    if (sig[0]&127u) return "unsupported_early_mcs";
    if (!hint) {
        uint32_t terminal = ref_u16(record+28u);
        if (ref_u16(record+22u) == 1u && terminal >= 50u && psdu >= terminal-2u &&
            psdu <= ((terminal+1u)&~3u)) return "terminal_single_body";
        return "no_nonterminal_good_hint";
    }
    if (hint < 50u) return "short_good_hint";
    if (hint > CLEAN_OBSERVER_EARLY_MAX+6u) return "early_storage_limit";
    if (psdu < hint-2u) return "inconsistent_body_geometry";
    if (psdu <= ((hint+1u)&~3u)) return "single_body_good_hint";
    if (ref_u16(early) == id) return "retained_multi_candidate";
    if (ref_u16(early) && ref_u16(early) < id && ref_u16(early+20u) == 2u)
        return "prior_multi_already_retained";
    return "eligible_not_retained";
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

static inline int ref_pair_correlated(const uint8_t log[CLEAN_OBSERVER_WORDS*2u],
                                      const uint8_t early[CLEAN_OBSERVER_EARLY_WORDS*2u],
                                      const uint8_t sample[REF_SAMPLE_BYTES],
                                      const uint8_t mac[6]) {
#if CLEAN_RX_HT_ADVANCE
    if (!ref_early_correlated(log,early,mac) || !ref_sample_correlated(log,sample,mac) ||
        ref_u16(early+24u) != 1u ||
        !(badger_bytes_load_u32(sample+8u)&REF_ADVANCED_SAMPLE) ||
        badger_bytes_load_u32(sample+4u) != ref_u16(early)) return 0;
    const uint8_t *first = early+CLEAN_OBSERVER_EARLY_HEADER*2u, *second = sample+16u;
    uint32_t first_length = ref_u16(early+4u)-6u;
    uint32_t second_length = badger_bytes_load_u32(sample);
    const uint8_t *record = log+2u*CLEAN_OBSERVER_HEADER_WORDS+
        (ref_u16(early)-1u)*2u*CLEAN_OBSERVER_RECORD_WORDS;
    for (uint32_t i = 4u; i < 22u; ++i)
        if (first[i] != second[i]) return 0;
    uint16_t first_sequence = ref_u16(first+22u), second_sequence = ref_u16(second+22u);
    return ((first_length == 170u && second_length == 298u) ||
            (first_length == 298u && second_length == 170u)) &&
        ref_u16(early+26u) == second_length &&
        ref_u16(early+15u) == 4u+((first_length+3u)&~3u)+4u+second_length &&
        !(first_sequence&15u) && !(second_sequence&15u) &&
        (first_sequence>>4u) != (second_sequence>>4u) &&
        (first[0]&0xfcu) == 0x88u && (second[0]&0xfcu) == 0x88u &&
        (first[1]&7u) == (second[1]&7u) && (first[1]&3u) != 3u && !(first[1]&4u) &&
        (first[24]&15u) == (second[24]&15u) &&
        (ref_u16(record+32u)&0xc000u) == 0x4000u;
#else
    (void)log; (void)early; (void)sample; (void)mac;
    return 0;
#endif
}

_Static_assert(REF_SUBMIT_STATS + 48u <= REF_COUNTER_BEFORE,
               "Query metadata must not overlap raw counter replies");
_Static_assert(REF_DUMP_AFTER + REF_DUMP_BYTES <= REF_TRACE_BYTES &&
    REF_OBSERVER_DETAIL + CLEAN_OBSERVER_WORDS * 2u <= REF_SAMPLE_BASE &&
    REF_SAMPLE_BASE + REF_SAMPLE_BYTES * REF_SAMPLES <= REF_TRACE_BYTES,
    "Reference evidence must fit the USB trace");
#endif

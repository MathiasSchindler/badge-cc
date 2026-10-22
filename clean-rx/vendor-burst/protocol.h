#ifndef CLEAN_VENDOR_BURST_PROTOCOL_H
#define CLEAN_VENDOR_BURST_PROTOCOL_H
#include "../../src/shared/bytes.h"
#include "../../src/tools/diagnostics_logic.h"
#include "../src/frame_limits.h"
#include "../src/ht_observer.h"
#include "../src/observer_dma.h"
#include "../src/sdio_ring.h"
#include "../src/ht20.h"

#ifndef REF_CLEAN_STIMULUS
#define REF_CLEAN_STIMULUS 0
#endif
#ifndef REF_CLEAN_HANDOFF
#define REF_CLEAN_HANDOFF 0
#endif
#ifndef REF_VENDOR_MONITOR
#define REF_VENDOR_MONITOR 0
#endif
#ifndef REF_MONITOR_POLL
#define REF_MONITOR_POLL 0
#endif
_Static_assert(REF_MONITOR_POLL >= 0 && REF_MONITOR_POLL <= 2,
    "Vendor monitor control-poll gate must select off, 100-ms or 10-ms service");
#define REF_MONITOR_POLL_MS (REF_MONITOR_POLL == 2 ? 10u : 100u)
_Static_assert(!REF_MONITOR_POLL || (REF_VENDOR_MONITOR && !REF_CLEAN_HANDOFF),
    "Vendor control polling requires the fixed-source raw monitor");
_Static_assert(REF_VENDOR_MONITOR == 0 || REF_VENDOR_MONITOR == 1,
    "Vendor monitor gate must be zero or one");
_Static_assert(!REF_VENDOR_MONITOR || (REF_CLEAN_STIMULUS && !CLEAN_RX_HT_HANDOFF_GUARD &&
    !CLEAN_RX_HT_EARLY_FIFO),
    "Vendor monitor requires an unchanged clean source without early FIFO publication");
_Static_assert(REF_CLEAN_HANDOFF == 0 || REF_CLEAN_HANDOFF == 1,
    "Controlled handoff gate must be zero or one");
_Static_assert(!REF_CLEAN_HANDOFF ||
    (REF_CLEAN_STIMULUS && CLEAN_RX_HT_ADVANCE && CLEAN_RX_HT_EARLY_SAVE &&
     CLEAN_RX_HT_OBSERVER_FIFO && !CLEAN_RX_HT_WINDOW),
    "Controlled handoff requires the FIFO-backed passive advance diagnostic");
#ifndef REF_OBSERVER_PAUSE
#define REF_OBSERVER_PAUSE 0
#endif
#ifndef REF_OBSERVER_STOP_FULL
#define REF_OBSERVER_STOP_FULL 0
#endif
_Static_assert(REF_OBSERVER_STOP_FULL == 0 || REF_OBSERVER_STOP_FULL == 1,
    "Full-queue stop gate must be zero or one");
_Static_assert(!REF_OBSERVER_STOP_FULL || REF_OBSERVER_PAUSE,
    "Full-queue stop requires the host pause diagnostic");
_Static_assert(REF_OBSERVER_PAUSE == 0 || REF_OBSERVER_PAUSE == 1,
    "Observer pause gate must be zero or one");
_Static_assert(!REF_OBSERVER_PAUSE ||
    (REF_CLEAN_STIMULUS && CLEAN_RX_HT_OBSERVER_FIFO),
    "Observer pause requires the fixed-source FIFO diagnostic");
#define REF_FIFO_PEAK 16u
#define REF_FIFO_HELD_SAMPLES 20u
#define REF_FIFO_PAUSE_STATE 52u
_Static_assert(REF_CLEAN_STIMULUS == 0 || REF_CLEAN_STIMULUS == 1,
    "Fixed reference source gate must be zero or one");
_Static_assert(!REF_CLEAN_STIMULUS ||
    REF_CLEAN_HANDOFF ||
    (CLEAN_RX_HT_WINDOW && CLEAN_RX_HT_EARLY_SAVE && !CLEAN_RX_HT_ADVANCE),
    "Fixed source requires the early-only timeline or controlled handoff");
#define REF_STIMULUS_RECORD_BASE 128u
#define REF_STIMULUS_RECORD_BYTES 64u
#define REF_STIMULUS_PACKET_BASE 4096u
#define REF_STIMULUS_RX_MASK 40u
#define REF_STIMULUS_RX_DUPLICATES 44u
#define REF_STIMULUS_RX_INVALID 48u
#define REF_EARLY_RX_COUNT (REF_COPY_FAULT_BASE+32u)
#define REF_EARLY_RX_MASK (REF_EARLY_RX_COUNT+4u)
#define REF_EARLY_RX_DUPLICATES (REF_EARLY_RX_COUNT+8u)
#define REF_EARLY_EXPORT_BASE 128u
#define REF_EARLY_EXPORT_MAGIC 0x31465245u
#define REF_EARLY_EXPORT_HEADER 16u
#define REF_EARLY_EXPORT_RECORD (12u+18u+CLEAN_HT_STIMULUS_MPDU)
#define REF_EARLY_EXPORT_SLOTS (CLEAN_HT_STIMULUS_COUNT/3u)
#define REF_EARLY_EXPORT_BYTES (REF_EARLY_EXPORT_HEADER+REF_EARLY_EXPORT_RECORD*REF_EARLY_EXPORT_SLOTS)
_Static_assert(REF_STIMULUS_RECORD_BASE+CLEAN_HT_STIMULUS_COUNT*REF_STIMULUS_RECORD_BYTES <=
    REF_STIMULUS_PACKET_BASE &&
    REF_STIMULUS_PACKET_BASE+CLEAN_HT_STIMULUS_STORAGE <= 5888u,
    "Fixed stimulus evidence must fit the trace");

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
#define REF_OBSERVER_ABI (CLEAN_RX_HT_COPY_DMA_PROBE ? 22u : 21u)
static inline uint16_t ref_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static inline int ref_dma_probe_valid(const uint8_t *trace, const uint8_t *early) {
    uint32_t count = badger_bytes_load_u32(trace+8u);
    if (badger_bytes_load_u32(trace) != CLEAN_OBSERVER_DMA_MAGIC ||
        badger_bytes_load_u32(trace+4u) != 5u || count > CLEAN_OBSERVER_DMA_SLOTS ||
        badger_bytes_load_u32(trace+12u) < count ||
        badger_bytes_load_u32(trace+76u) > badger_bytes_load_u32(trace+12u) ||
        badger_bytes_load_u32(trace+76u) !=
            badger_bytes_load_u32(trace+104u)+badger_bytes_load_u32(trace+108u) ||
        badger_bytes_load_u32(trace+112u) > badger_bytes_load_u32(trace+104u) ||
        badger_bytes_load_u32(trace+116u) > badger_bytes_load_u32(trace+108u) ||
        badger_bytes_load_u32(trace+20u) > CLEAN_D11_BUFFER_BYTES ||
        (badger_bytes_load_u32(trace+120u)>>28u) || badger_bytes_load_u32(trace+124u) ||
        badger_bytes_load_u32(trace+16u) !=
            badger_bytes_load_u32(trace+12u)+badger_bytes_load_u32(trace+28u)) return 0;
    for (uint32_t i = 0u; i < count; ++i) {
        const uint8_t *r = trace+CLEAN_OBSERVER_DMA_HEADER+i*CLEAN_OBSERVER_DMA_RECORD;
        uint32_t consumer = badger_bytes_load_u32(r+4u);
        uint32_t pointer = (badger_bytes_load_u32(r+12u)&0x1fffu) -
            (CLEAN_D11_RING_ADDR&0x1fffu);
        uint32_t frozen = badger_bytes_load_u32(r+24u)&0x80000000u;
        if (consumer >= 32u || badger_bytes_load_u32(r+8u) >= 32u ||
            (!frozen && (pointer%16u || pointer/16u >= 32u || pointer/16u == consumer)) ||
            !ref_u16(r+20u) || ref_u16(r+20u) != ref_u16(early) ||
            ref_u16(r+22u) != ref_u16(early+2u) ||
            (badger_bytes_load_u32(r+24u)&0x1fff0000u) ||
            (!frozen && ref_u16(r+24u) != 14u) ||
            (badger_bytes_load_u32(r+28u)>>16u)+CLEAN_OBSERVER_DMA_PREFIX >
                CLEAN_D11_BUFFER_BYTES) return 0;
    }
    return 1;
}
static inline int ref_stream_defer_verified(const uint8_t *handoff, const uint8_t *early) {
    const uint8_t *before = handoff+2u*CLEAN_OBSERVER_HANDOFF_HEADER;
    uint32_t owner = ref_u16(handoff);
    return owner && owner <= CLEAN_OBSERVER_RECORDS && owner == ref_u16(early) &&
        ref_u16(handoff+2u) == ref_u16(early+2u) &&
        ref_u16(handoff+4u) == 7u && !ref_u16(handoff+6u) &&
        !(ref_u16(before+10u)&2u) &&
        !(ref_u16(before+2u*CLEAN_OBSERVER_HANDOFF_RECORD_WORDS+10u)&2u);
}
static inline int ref_stream_phase_verified(const uint8_t *handoff, const uint8_t *early,
                                            const uint8_t *copy, uint32_t phase) {
    if ((phase < 1u || phase > 3u) || !ref_stream_defer_verified(handoff,early) ||
        (ref_u16(handoff+2u*CLEAN_OBSERVER_HANDOFF_HEADER+
            4u*CLEAN_OBSERVER_HANDOFF_RECORD_WORDS+10u)&2u) ||
        ref_u16(copy) != ref_u16(early) || ref_u16(copy+2u) != ref_u16(early+2u) ||
        (ref_u16(copy+4u) != 1u && ref_u16(copy+4u) != 3u) ||
        (ref_u16(copy+6u) != 3u && ref_u16(copy+6u) != 4u &&
         ref_u16(copy+6u) != 5u && ref_u16(copy+6u) != 6u && ref_u16(copy+6u) != 7u)) return 0;
    if (phase == 1u) return ref_u16(copy+4u) == 3u && !(ref_u16(copy+26u)&2u);
    uint32_t threshold = phase == 3u ? 256u : 128u;
    int activated = ref_u16(copy+84u) == 1u && ref_u16(copy+86u) == ref_u16(early+2u) &&
        ref_u16(copy+72u) >= threshold && (ref_u16(copy+76u)&2u);
    if (ref_u16(copy+4u) == 3u && ref_u16(copy+20u) < threshold) {
        /* Post-stall replay can rewind the count after committed activation. */
        if (phase == 2u && ref_u16(copy+6u) >= 5u && activated)
            return ref_u16(copy+20u) >= 50u && (ref_u16(copy+26u)&2u);
        return ref_u16(copy+20u) >= 50u && !(ref_u16(copy+26u)&2u) &&
            !(ref_u16(copy+84u) == 1u && ref_u16(copy+86u) == ref_u16(early+2u));
    }
    return activated && (ref_u16(copy+4u) != 3u || (ref_u16(copy+26u)&2u));
}
static inline int ref_stream_rearm_verified(const uint8_t *copy, const uint8_t *early,
                                            uint32_t mode) {
    return (mode == 1u || mode == 2u) && ref_u16(copy+6u) == 4u &&
        ref_u16(copy) && ref_u16(copy) == ref_u16(early) &&
        ref_u16(copy+2u) == ref_u16(early+2u) &&
        (ref_u16(copy+4u) == 1u || ref_u16(copy+4u) == 3u) &&
        ref_u16(copy+84u) == 1u && ref_u16(copy+86u) == ref_u16(early+2u) &&
        !(ref_u16(copy+88u)&2u) && (ref_u16(copy+76u)&2u) &&
        (ref_u16(copy+100u)&2u) && ref_u16(copy+72u) >= 128u &&
        ref_u16(copy+98u) <= CLEAN_RXE_BYTES &&
        ref_u16(copy+90u) == CLEAN_SHM_FRAME && ref_u16(copy+102u) == CLEAN_SHM_FRAME &&
        ref_u16(copy+92u) == ref_u16(early+26u) &&
        ref_u16(copy+104u) == ref_u16(early+26u);
}
static inline int ref_stream_stall_valid(const uint8_t *copy, const uint8_t *early,
                                        uint32_t mode) {
    uint32_t reason = ref_u16(copy+96u), budget = ref_u16(copy+90u);
    uint32_t base = CLEAN_SHM_FRAME +
        (mode >= 6u && reason == 1u ? CLEAN_STREAM_DEST_WORDS : 0u);
    uint32_t end = ref_u16(early+26u) -
        (mode == 8u && reason == 1u ? CLEAN_STREAM_END_BYTES : 0u);
    if (mode < 3u || mode > 8u || ref_u16(copy+6u) != (mode >= 7u ? 7u : mode >= 5u ? 6u : 5u) ||
        !ref_u16(copy) || ref_u16(copy) != ref_u16(early) ||
        ref_u16(copy+2u) != ref_u16(early+2u) ||
        (ref_u16(copy+4u) != 1u && ref_u16(copy+4u) != 3u) ||
        ref_u16(copy+84u) != 1u || ref_u16(copy+86u) != ref_u16(early+2u) ||
        !(ref_u16(copy+76u)&2u) || !(ref_u16(copy+100u)&2u) ||
        ref_u16(copy+72u) < 128u || ref_u16(copy+88u) < 128u ||
        ref_u16(copy+88u) > CLEAN_RXE_BYTES || ref_u16(copy+98u) > CLEAN_RXE_BYTES ||
        ref_u16(copy+102u) != base ||
        ref_u16(copy+104u) != end ||
        budget > CLEAN_STREAM_STALL_BUDGET || reason < 1u || reason > (mode >= 7u ? 6u : 5u)) return 0;
    if (mode >= 7u && reason == 1u &&
        (ref_u16(early+26u) <= CLEAN_STREAM_END_BYTES ||
         ref_u16(early+26u) > CLEAN_OBSERVER_SECOND_MAX)) return 0;
    if (reason == 6u && ref_u16(early+26u) > CLEAN_STREAM_END_BYTES &&
        ref_u16(early+26u) <= CLEAN_OBSERVER_SECOND_MAX) return 0;
    int stable = ref_u16(copy+88u) == CLEAN_STREAM_STALL_COUNT && budget &&
        ref_u16(copy+94u) >= CLEAN_STREAM_STALL_US && ref_u16(copy+94u) <= 1000u;
    if (reason == 1u) return stable;
    if (reason == 6u && !stable) return 0;
    return !(ref_u16(copy+100u)&0xc0u) && (reason != 5u || !budget);
}
static inline int ref_copy_scope_valid(const uint8_t *scope, const uint8_t *copy,
                                       const uint8_t *early, uint32_t mode) {
    uint32_t commit = ref_u16(scope);
    if (mode < 1u || mode > 4u || ref_u16(copy+6u) != 6u ||
        !ref_u16(copy) || ref_u16(copy) != ref_u16(early) ||
        ref_u16(copy+2u) != ref_u16(early+2u) ||
        (ref_u16(copy+4u) != 1u && ref_u16(copy+4u) != 3u)) return 0;
    if (!commit) return !ref_u16(scope+2u) && ref_u16(copy+96u) == 2u &&
        ref_stream_stall_valid(copy,early,
            CLEAN_RX_HT_COPY_SCOPE ? CLEAN_RX_HT_COPY_STREAM_REARM : 6u);
    if (commit < 17u || commit > 20u || ref_u16(scope+2u) != ref_u16(early+2u) ||
        ref_u16(copy+84u) != 1u || ref_u16(copy+86u) != ref_u16(early+2u) ||
        ref_u16(copy+72u) < 128u || !(ref_u16(copy+76u)&2u) ||
        ref_u16(copy+88u) != 134u || !ref_u16(copy+90u) ||
        ref_u16(copy+90u) > CLEAN_STREAM_STALL_BUDGET ||
        ref_u16(copy+94u) < 32u || ref_u16(copy+94u) > 1000u ||
        ref_u16(copy+96u) != 1u) return 0;
    for (uint32_t n = 0u; n < 3u; ++n) {
        const uint8_t *s = scope+4u+52u*n;
        if (ref_u16(s+10u) != CLEAN_SHM_FRAME+
                (n && !(CLEAN_RX_HT_COPY_SCOPE && CLEAN_RX_HT_COPY_STREAM_REARM == 5) ? 64u : 0u) ||
            ref_u16(s+12u) != ref_u16(early+26u) ||
            (ref_u16(s+8u)&3u) != (n && mode >= 2u ? 0u : 2u)) return 0;
    }
    if (ref_u16(scope+6u) != 134u || (ref_u16(scope+8u)&0xc000u)) return 0;
    uint32_t elapsed = (uint16_t)(ref_u16(scope+108u)-ref_u16(scope+56u));
    if (commit == 17u && (elapsed < 32u || elapsed > 1000u)) return 0;
    if (commit == 18u && !(ref_u16(scope+112u)&0x4000u)) return 0;
    if (commit == 19u && !(ref_u16(scope+112u)&0x8000u)) return 0;
    return 1;
}
static inline uint32_t ref_source_frame(uint8_t packet[CLEAN_HT_STIMULUS_STORAGE],
                                       const uint8_t nonce[14], uint32_t index) {
    return REF_CLEAN_HANDOFF ? clean_ht_handoff_frame(packet,nonce,index) :
                              clean_ht_stimulus_frame(packet,nonce,index);
}
static inline uint32_t ref_source_control_length(const uint8_t nonce[14]) {
    return REF_CLEAN_HANDOFF ? clean_ht_handoff_length(nonce,0u) : CLEAN_HT_STIMULUS_MPDU;
}
static inline int ref_stimulus_record_valid(const uint8_t *r, const uint8_t nonce[14],
                                            uint32_t index) {
    uint8_t packet[CLEAN_HT_STIMULUS_STORAGE];
    uint32_t cmd = ref_source_frame(packet,nonce,index);
    uint32_t total = clean_tx_length(cmd), kind = index%3u;
    return cmd && badger_bytes_load_u32(r) == index &&
        badger_bytes_load_u32(r+4u) == cmd &&
        badger_bytes_load_u32(r+12u)-badger_bytes_load_u32(r+8u) < 1000000u &&
        badger_bytes_load_u32(r+20u)-badger_bytes_load_u32(r+16u) >=
            (kind ? clean_ht20_airtime_us(total-6u,nonce[12]) :
                    clean_ofdm_airtime_us(total-6u,0u)) &&
        badger_bytes_load_u32(r+20u)-badger_bytes_load_u32(r+16u) < 100000u &&
        !badger_bytes_load_u32(r+24u) && badger_bytes_load_u32(r+28u) == 7u &&
        badger_bytes_load_u32(r+32u) == total &&
        badger_bytes_load_u32(r+36u) == diag_crc32(packet,sizeof(packet)) &&
        badger_bytes_load_u32(r+40u) == CLEAN_TX_DIAG_MAGIC &&
        badger_bytes_equal(r+44u,packet+CLEAN_HT_DATA_HEADER,6u) &&
        ref_u16(r+50u) == (kind ? 0x42u : 0x41u) &&
        ref_u16(r+52u) == (kind ? clean_ht20_rates[nonce[12]].phyctl1 :
                                 clean_ofdm_rates[0].phyctl1) &&
        ref_u16(r+54u) == 0x826u &&
        badger_bytes_load_u32(r+60u) == nonce[13];
}
static inline uint32_t ref_stimulus_rx_index(const uint8_t *packet, uint32_t size,
                                             const uint8_t nonce[14]) {
    uint32_t length = ref_source_control_length(nonce);
    if (size != 18u+length) return CLEAN_HT_STIMULUS_COUNT;
    uint32_t sequence = ref_u16(packet+18u+22u)>>4u, index = sequence/2u;
    uint8_t expected[CLEAN_HT_STIMULUS_STORAGE];
    if ((sequence&1u) || index%3u == 2u ||
        !ref_source_frame(expected,nonce,index) ||
        packet[15] != expected[CLEAN_HT_DATA_HEADER] || packet[17] ||
        packet[16] != ((index%3u == 1u ? CLEAN_RX_UNSUPPORTED_PHY : CLEAN_RX_OFDM)|nonce[13]) ||
        (index%3u == 1u && (!ref_u16(packet+10u) ||
                          ref_u16(packet+10u) > CLEAN_OBSERVER_RECORDS)) ||
        !badger_bytes_equal(packet+18u,expected+CLEAN_HT_DATA_HEADER+6u,
                           length)) return CLEAN_HT_STIMULUS_COUNT;
    return index;
}
static inline uint32_t ref_early_stimulus_rx_index(const uint8_t *packet, uint32_t size,
                                                   const uint8_t nonce[14]) {
    if (!CLEAN_RX_HT_EARLY_FIFO || !REF_CLEAN_STIMULUS || REF_CLEAN_HANDOFF ||
        size != 18u+CLEAN_HT_STIMULUS_MPDU) return CLEAN_HT_STIMULUS_COUNT;
    uint32_t identity = ref_u16(packet+10u);
    uint32_t owner = identity&~CLEAN_OBSERVER_EARLY_TAG;
    uint32_t sequence = ref_u16(packet+18u+22u)>>4u, index = sequence/2u;
    uint8_t expected[CLEAN_HT_STIMULUS_STORAGE];
    if (!(identity&CLEAN_OBSERVER_EARLY_TAG) || !owner || owner > CLEAN_OBSERVER_RECORDS ||
        (sequence&1u) || index%3u != 2u || packet[15] || packet[17] ||
        packet[16] != (CLEAN_RX_UNSUPPORTED_PHY|nonce[13]) ||
        !clean_ht_stimulus_frame(expected,nonce,index) ||
        !badger_bytes_equal(packet+18u,expected+CLEAN_HT_DATA_HEADER+10u,
                           CLEAN_HT_STIMULUS_MPDU)) return CLEAN_HT_STIMULUS_COUNT;
    return index;
}
static inline int ref_early_export_valid(const uint8_t *exported, const uint8_t nonce[14],
                                         uint32_t captured, uint32_t *mask) {
    uint32_t saved = badger_bytes_load_u32(exported+4u);
    uint32_t total = badger_bytes_load_u32(exported+8u);
    if (badger_bytes_load_u32(exported) != REF_EARLY_EXPORT_MAGIC ||
        saved > REF_EARLY_EXPORT_SLOTS || total < saved ||
        total-saved != badger_bytes_load_u32(exported+12u)) return 0;
    uint32_t previous = 0u, seen = 0u;
    for (uint32_t i = 0u; i < saved; ++i) {
        const uint8_t *r = exported+REF_EARLY_EXPORT_HEADER+i*REF_EARLY_EXPORT_RECORD;
        uint32_t size = badger_bytes_load_u32(r), ordinal = badger_bytes_load_u32(r+4u);
        if (size != 18u+CLEAN_HT_STIMULUS_MPDU || ordinal <= previous ||
            ordinal > captured) return 0;
        const uint8_t *packet = r+12u;
        if (ref_u16(packet) != size || ref_u16(packet+2u) != (uint16_t)~size ||
            packet[4] != (uint8_t)(ordinal-1u) || packet[5] != 2u ||
            packet[7] != 14u || packet[14] != 0x20u ||
            ref_u16(packet+12u) != CLEAN_RX_PHASE_MAGIC) return 0;
        uint32_t index = ref_early_stimulus_rx_index(r+12u,size,nonce);
        if (index == CLEAN_HT_STIMULUS_COUNT ||
            index != badger_bytes_load_u32(r+8u) || (seen&(1u<<index))) return 0;
        seen |= 1u<<index; previous = ordinal;
    }
    for (uint32_t i = REF_EARLY_EXPORT_HEADER+saved*REF_EARLY_EXPORT_RECORD;
         i < REF_EARLY_EXPORT_BYTES; ++i)
        if (exported[i]) return 0;
    *mask = seen;
    return 1;
}

static inline uint32_t ref_handoff_rx_index(const uint8_t *packet, uint32_t size,
                                           const uint8_t nonce[14]) {
    if (!REF_CLEAN_HANDOFF || size < 18u+44u) return CLEAN_HT_STIMULUS_COUNT;
    uint32_t sequence = ref_u16(packet+40u)>>4u, index = sequence/2u, ordinal = sequence&1u;
    uint32_t length = clean_ht_handoff_length_at(nonce,index,ordinal);
    uint8_t expected[CLEAN_HT_STIMULUS_STORAGE];
    if (index%3u != 2u || size != 18u+length ||
        !clean_ht_handoff_frame(expected,nonce,index)) return CLEAN_HT_STIMULUS_COUNT;
    uint32_t offset = CLEAN_HT_DATA_HEADER+10u+
        (ordinal ? 4u+((clean_ht_handoff_length_at(nonce,index,0u)+3u)&~3u) : 0u);
    if (packet[15] != nonce[12] || packet[16] != (CLEAN_RX_UNSUPPORTED_PHY|nonce[13]) ||
        packet[17] || !(ref_u16(packet+10u)&~CLEAN_OBSERVER_ADVANCE_TAG) ||
        (ref_u16(packet+10u)&~CLEAN_OBSERVER_ADVANCE_TAG) > CLEAN_OBSERVER_RECORDS ||
        !badger_bytes_equal(packet+18u,expected+offset,length) ||
        diag_crc32(packet+18u,length-4u) != badger_bytes_load_u32(packet+size-4u))
        return CLEAN_HT_STIMULUS_COUNT;
    return index;
}
static inline int ref_timer_calibration_valid(uint32_t ticks, uint32_t frames) {
    return ticks && ticks <= 100000u && frames >= 32u && frames <= 1024u &&
        ticks*100u >= frames;
}
static inline uint64_t ref_timer_us(uint32_t value, uint32_t ticks, uint32_t frames) {
    return (uint64_t)value*frames*1000u/ticks;
}
#define REF_WINDOW_BASE 128u
#define REF_WINDOW_MAGIC 0x39444e57u
#define REF_WINDOW_INTERVAL 100000u
#define REF_WINDOW_RECORDS 35u
#define REF_WINDOW_RECORD_BYTES 160u
#define REF_WINDOW_HEADER_BYTES 128u
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
        badger_bytes_load_u32(window+28u) > 63u ||
        ((badger_bytes_load_u32(window+28u)&16u) &&
         (!(badger_bytes_load_u32(window+28u)&1u) ||
          (badger_bytes_load_u32(window+28u)&12u))) ||
        ((badger_bytes_load_u32(window+28u)&32u) &&
         !(badger_bytes_load_u32(window+28u)&16u)) ||
        ((badger_bytes_load_u32(window+28u)&8u) &&
         !(badger_bytes_load_u32(window+28u)&4u)) ||
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
    if (!(badger_bytes_load_u32(window+28u)&20u))
        for (uint32_t i = 96u; i < REF_WINDOW_HEADER_BYTES; ++i)
            if (window[i]) return 0;
    if ((badger_bytes_load_u32(window+28u)&4u) &&
        ref_u16(window+96u) > 4u && ref_u16(window+96u) != 0xffffu) return 0;
    if ((badger_bytes_load_u32(window+28u)&16u) && ref_u16(window+98u) > 4u) return 0;
    if ((badger_bytes_load_u32(window+28u)&16u) &&
        !(badger_bytes_load_u32(window+28u)&32u))
        for (uint32_t i = 112u; i < REF_WINDOW_HEADER_BYTES; ++i)
            if (window[i]) return 0;
    if ((badger_bytes_load_u32(window+28u)&32u) && ref_u16(window+116u) &&
        (ref_u16(window+120u) != 2u || !(ref_u16(window+122u)&0x1000u) ||
         ref_u16(window+126u) < 250u)) return 0;
    for (uint32_t i = 0u; i < count; ++i) {
        const uint8_t *r = window+REF_WINDOW_HEADER_BYTES+i*REF_WINDOW_RECORD_BYTES;
        uint32_t time = badger_bytes_load_u32(r), duration = badger_bytes_load_u32(r+4u);
        if (time < end || time >= 45000000u || !duration ||
            duration >= 45000000u-time) return 0;
        uint16_t reset = ref_u16(r+158u);
        if (badger_bytes_load_u32(window+28u)&4u) {
            if (reset > 4u && reset != 0xffffu) return 0;
        } else if (reset) return 0;
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
    if (CLEAN_RX_HT_OBSERVER_FIFO && (captured != posted || posted != consumed)) return 0;
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
#define REF_COPY_BASE (REF_EARLY_BASE + 2u*(CLEAN_OBSERVER_COPY_SHM-CLEAN_OBSERVER_EARLY_SHM))
#define REF_COPY_FAULT_BASE (REF_EARLY_BASE+2u*CLEAN_OBSERVER_EARLY_WORDS)
#define REF_COPY_FAULT_MAGIC 0x31504346u
_Static_assert(REF_COPY_BASE+2u*CLEAN_OBSERVER_COPY_WORDS <= REF_TRACE_BYTES,
    "Receive-copy probe must fit the existing early SHM export");
_Static_assert(REF_COPY_BASE+2u*CLEAN_OBSERVER_COPY_WORDS <= REF_COPY_FAULT_BASE,
    "Receive-copy probe must not overlap controller-fault evidence");
_Static_assert(REF_COPY_FAULT_BASE+32u <= REF_TRACE_BYTES,
    "Copy fault evidence must fit beyond the complete early SHM export");
_Static_assert(REF_EARLY_RX_DUPLICATES+4u <= REF_TRACE_BYTES,
    "Early delivery accounting must fit beyond all snapshot and fault records");
_Static_assert(REF_EARLY_EXPORT_BASE+REF_EARLY_EXPORT_BYTES <= REF_TRACE_BYTES,
    "Separate early-packet export must fit one full report");
#define REF_ADVANCED_SAMPLE 0x100u
#define REF_EARLY_SAMPLE 0x200u
static inline int ref_copy_probe_valid(const uint8_t *copy, const uint8_t *early,
                                       const uint8_t *second) {
    uint32_t owner = ref_u16(copy), phase = ref_u16(copy+4u);
    if (!owner) {
        for (uint32_t i = 0u; i < 2u*CLEAN_OBSERVER_COPY_WORDS; ++i)
            if (copy[i]) return 0;
        return !ref_u16(early+24u);
    }
    if (owner > CLEAN_OBSERVER_RECORDS || owner != ref_u16(early) ||
        ref_u16(copy+2u) != ref_u16(early+2u) ||
        ref_u16(early+24u) != 1u ||
        (phase != 1u && phase != 3u && !(CLEAN_RX_HT_COPY_WAIT && phase == 5u)) ||
        ref_u16(copy+6u) != CLEAN_OBSERVER_COPY_VERSION) return 0;
    for (uint32_t i = 0u; i < 4u; ++i)
        if (ref_u16(copy+8u+2u*i) != CLEAN_OBSERVER_COPY_CANARY) return 0;
    return phase != 3u || (ref_u16(second) == owner &&
        ref_u16(second+2u) == ref_u16(copy+2u) &&
        ref_u16(copy+20u) >= 50u && ref_u16(copy+20u) <= 306u);
}
static inline uint32_t ref_sample_slot(const uint8_t *samples, uint32_t count,
                                       uint32_t length, uint32_t identity, uint32_t advanced) {
    for (uint32_t i = 0u; i < count; ++i) {
        const uint8_t *saved = samples+i*REF_SAMPLE_BYTES;
        if (badger_bytes_load_u32(saved) != length) continue;
        if (badger_bytes_load_u32(saved+8u)&REF_EARLY_SAMPLE)
            return (advanced&CLEAN_OBSERVER_EARLY_TAG) &&
                identity > badger_bytes_load_u32(saved+4u) ? i : REF_SAMPLES;
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
_Static_assert(REF_SAMPLE_BASE+CLEAN_OBSERVER_DMA_BYTES <= REF_EARLY_BASE,
               "DMA diagnostic must fit the replaced MPDU sample area");
_Static_assert(REF_EARLY_BASE+CLEAN_OBSERVER_EARLY_WORDS*2u <= REF_TRACE_BYTES,
               "Complete MPDU samples and early snapshot must fit the trace transport");

static inline const char *ref_observer_outcome_name(uint32_t outcome) {
    static const char *const names[] = {
        "open","good","bad_fcs","mismatch_or_short","length_reject",
        "abort_or_fault","watchdog","new_rx","new_prefix"
    };
    return outcome < sizeof(names)/sizeof(names[0]) ? names[outcome] : 0;
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
    if (CLEAN_RX_HT_VARIABLE && profile >= 4u && profile < 20u) return 128u<<(profile&1u);
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

static inline uint32_t ref_stimulus_prefix_index(const uint8_t *record,
                                                 const uint8_t nonce[14]) {
    const uint8_t *m = record+36u;
    uint32_t sequence = ref_u16(m+22u)>>4u, index = sequence/2u;
    uint8_t expected[CLEAN_HT_STIMULUS_STORAGE];
    if ((sequence&1u) || index%3u != 1u ||
        !clean_ht_stimulus_frame(expected,nonce,index) ||
        !badger_bytes_equal(record+12u,expected+CLEAN_HT_DATA_HEADER,6u) ||
        !badger_bytes_equal(m,expected+CLEAN_HT_DATA_HEADER+6u,40u))
        return CLEAN_HT_STIMULUS_COUNT;
    return index;
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
    if (CLEAN_RX_HT_EARLY_FIFO && ref_u16(early) && ref_u16(early+20u) == 2u)
        return "fifo_candidate_not_in_final_snapshot";
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

static inline int ref_early_f2_correlated(const uint8_t *log, const uint8_t *early,
                                         const uint8_t *sample, const uint8_t mac[6],
                                         uint32_t channel) {
#if CLEAN_RX_HT_EARLY_FIFO
    uint32_t length = badger_bytes_load_u32(sample);
    if (!ref_early_correlated(log,early,mac) || ref_u16(early+20u) != 2u ||
        ref_u16(early+26u) != ref_u16(early) ||
        length != ref_u16(early+4u)-6u ||
        badger_bytes_load_u32(sample+4u) != ref_u16(early) ||
        badger_bytes_load_u32(sample+8u) != (REF_EARLY_SAMPLE|CLEAN_RX_UNSUPPORTED_PHY|channel))
        return 0;
    const uint8_t *body = early+2u*CLEAN_OBSERVER_EARLY_HEADER;
    return badger_bytes_equal(sample+16u,body,length) &&
        diag_crc32(body,length-4u) == badger_bytes_load_u32(sample+12u);
#else
    (void)log; (void)early; (void)sample; (void)mac; (void)channel;
    return 0;
#endif
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
    uint32_t pair_psdu = 4u+((first_length+3u)&~3u)+4u+second_length;
    uint32_t terminal = ref_u16(record+32u)&0xc100u;
    int boundary = ref_u16(early+15u) == pair_psdu && (terminal&0xc000u) == 0x4000u;
#if CLEAN_RX_HT_HANDOFF_GUARD
    boundary |= ref_u16(early+15u) == CLEAN_HT_HANDOFF_PSDU &&
        pair_psdu == 478u && terminal == 0x100u;
#endif
    return (CLEAN_RX_HT_VARIABLE ?
            first_length >= 44u && first_length <= 480u &&
            second_length >= 44u && second_length <= 554u :
            ((first_length == 170u && second_length == 298u) ||
             (first_length == 298u && second_length == 170u))) &&
        ref_u16(early+26u) == second_length &&
        boundary &&
        !(first_sequence&15u) && !(second_sequence&15u) &&
        (first_sequence>>4u) != (second_sequence>>4u) &&
        (first[0]&0xfcu) == 0x88u && (second[0]&0xfcu) == 0x88u &&
        (first[1]&7u) == (second[1]&7u) && (first[1]&3u) != 3u && !(first[1]&4u) &&
        (first[24]&15u) == (second[24]&15u);
#else
    (void)log; (void)early; (void)sample; (void)mac;
    return 0;
#endif
}

static inline int ref_handoff_bad_second(const uint8_t *log, const uint8_t *early,
                                         const uint8_t *second, const uint8_t nonce[14]) {
    uint8_t expected[CLEAN_HT_STIMULUS_STORAGE], mac[6];
    mac[0] = 2u;
    for (uint32_t i = 0u; i < 5u; ++i) mac[i+1u] = nonce[i];
    uint32_t first = clean_ht_handoff_length_at(nonce,2u,0u);
    uint32_t length = clean_ht_handoff_length_at(nonce,2u,1u);
    uint32_t offset = 18u+4u+((first+3u)&~3u);
    if (!REF_CLEAN_HANDOFF || nonce[10] != 2u || length > CLEAN_OBSERVER_SECOND_MAX ||
        !clean_ht_handoff_frame(expected,nonce,2u) || !ref_early_correlated(log,early,mac) ||
        ref_u16(early+24u) != 1u || ref_u16(second) != ref_u16(early) ||
        ref_u16(second+2u) != ref_u16(early+2u) || ref_u16(second+4u) != length+6u ||
        (ref_u16(second+6u)&0xc100u) != (CLEAN_RX_HT_HANDOFF_GUARD ? 0u : 0x4000u) ||
        ref_u16(second+20u) != 2u ||
        !badger_bytes_equal(early+CLEAN_OBSERVER_EARLY_HEADER*2u,expected+18u,first) ||
        !badger_bytes_equal(second+CLEAN_OBSERVER_SECOND_HEADER*2u,expected+offset,length))
        return 0;
    return diag_crc32(second+CLEAN_OBSERVER_SECOND_HEADER*2u,length-4u) !=
        badger_bytes_load_u32(second+CLEAN_OBSERVER_SECOND_HEADER*2u+length-4u);
}

_Static_assert(REF_SUBMIT_STATS + 48u <= REF_COUNTER_BEFORE,
               "Query metadata must not overlap raw counter replies");
_Static_assert(REF_DUMP_AFTER + REF_DUMP_BYTES <= REF_TRACE_BYTES &&
    REF_OBSERVER_DETAIL + CLEAN_OBSERVER_WORDS * 2u <= REF_SAMPLE_BASE &&
    REF_SAMPLE_BASE + REF_SAMPLE_BYTES * REF_SAMPLES <= REF_TRACE_BYTES,
    "Reference evidence must fit the USB trace");
#endif

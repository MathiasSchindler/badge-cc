#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "protocol.h"
#include "monitor.h"
#include "../src/ht20.h"
#include "../../src/platform/badger/graphics.h"

static void monitor_encoding(void) {
    uint32_t fcs_masks[4] = {1u, 2u, 4u, 4u};
    assert(ref_monitor_qualified(fcs_masks));
    for (uint32_t i = 0u; i < 4u; ++i) {
        uint32_t saved = fcs_masks[i];
        fcs_masks[i] = 0u;
        assert(!ref_monitor_qualified(fcs_masks));
        fcs_masks[i] = saved;
    }
    fcs_masks[3] = 8u;
    assert(!ref_monitor_qualified(fcs_masks));
    fcs_masks[3] = 12u;
    assert(ref_monitor_qualified(fcs_masks));
    uint8_t trace[REF_TRACE_BYTES] = {0}, nonce[14] = {1u,2u,3u};
    uint8_t packet[WIFI_CONTROL_MAX], body[14];
    memcpy(body, nonce, sizeof(body));
    uint32_t length = wifi_data_frame(packet, 0u, 0u, body, sizeof(body));
    assert(length == 32u);
    for (uint32_t i = 0u; i < REF_MONITOR_SLOTS; ++i) {
        packet[5] = (uint8_t)(i + 1u);
        ref_monitor_packet(trace, nonce, packet, length, 10u + i);
        const uint8_t *slot = trace + REF_MONITOR_PACKETS + i*REF_MONITOR_SLOT_BYTES;
        assert(ref_monitor_slot_valid(slot));
        assert(badger_bytes_load_u32(slot + 4u) == 10u + i);
        assert(!memcmp(slot + 16u, packet, length));
        assert(ref_monitor_contains(slot + 16u, length, body, sizeof(body)));
        body[13] ^= 1u;
        assert(!ref_monitor_contains(slot + 16u, length, body, sizeof(body)));
        body[13] ^= 1u;
    }
    assert(badger_bytes_load_u32(trace + 76u) == REF_MONITOR_SLOTS);
    uint8_t saved[REF_MONITOR_SLOTS*REF_MONITOR_SLOT_BYTES];
    memcpy(saved, trace + REF_MONITOR_PACKETS, sizeof(saved));
    ref_monitor_packet(trace, nonce, packet, length, 99u);
    assert(badger_bytes_load_u32(trace + 80u) == 1u);
    ref_monitor_packet(trace, nonce, packet, REF_MONITOR_PACKET_BYTES + 1u, 99u);
    assert(badger_bytes_load_u32(trace + 84u) == 1u);
    nonce[13] ^= 1u;
    ref_monitor_packet(trace, nonce, packet, length, 99u);
    assert(badger_bytes_load_u32(trace + 88u) == 1u);
    assert(badger_bytes_load_u32(trace + 72u) == REF_MONITOR_SLOTS + 3u);
    assert(badger_bytes_load_u32(trace + 92u) == REF_MONITOR_SLOTS + 2u);
    assert(!memcmp(saved, trace + REF_MONITOR_PACKETS, sizeof(saved)));
    uint8_t *slot = trace + REF_MONITOR_PACKETS;
    slot[16u + 2u] ^= 1u;
    assert(!ref_monitor_slot_valid(slot));
    slot[16u + 2u] ^= 1u;
    slot[12u] ^= 1u;
    assert(!ref_monitor_slot_valid(slot));
    slot[12u] ^= 1u;
    badger_bytes_store_u32(slot, REF_MONITOR_PACKET_BYTES + 1u);
    assert(!ref_monitor_slot_valid(slot));
    assert(!ref_monitor_contains(packet, length, body, length + 1u));
    assert(!ref_monitor_contains(packet, length, body, 0u));
    memset(trace, 0, sizeof(trace));
    uint8_t large[REF_MONITOR_PACKET_BYTES - 18u] = {0};
    memcpy(large, nonce, sizeof(nonce));
    length = wifi_data_frame(packet, 0u, 0u, large, sizeof(large));
    assert(length == REF_MONITOR_PACKET_BYTES);
    ref_monitor_packet(trace, nonce, packet, length, 100u);
    assert(ref_monitor_slot_valid(trace + REF_MONITOR_PACKETS));
    assert(badger_bytes_load_u32(trace + REF_MONITOR_PACKETS) == REF_MONITOR_PACKET_BYTES);
    for (uint32_t bytes = 170u; bytes <= 298u; bytes += 128u) {
        uint8_t mpdu[298];
        for (uint32_t i = 0u; i < bytes; ++i) mpdu[i] = (uint8_t)(i ^ 0x5au);
        badger_bytes_store_u32(mpdu + bytes - 4u, diag_crc32(mpdu, bytes - 4u));
        memcpy(packet, mpdu, bytes);
        assert(ref_monitor_contains(packet, bytes, mpdu, bytes));
        assert(ref_monitor_contains(packet, bytes - 4u, mpdu, bytes - 4u));
        assert(!ref_monitor_contains(packet, bytes - 4u, mpdu, bytes));
        packet[8u] ^= 1u;
        assert(!ref_monitor_contains(packet, bytes, mpdu, bytes - 4u));
        packet[8u] ^= 1u;
        packet[bytes - 1u] ^= 1u;
        assert(ref_monitor_contains(packet, bytes, mpdu, bytes - 4u));
        assert(!ref_monitor_contains(packet, bytes, mpdu, bytes));
    }
}

static void window_encoding(void) {
    monitor_encoding();
    assert(ref_timer_calibration_valid(16000u,32u));
    assert(!ref_timer_calibration_valid(0u,32u));
    assert(!ref_timer_calibration_valid(100001u,32u));
    assert(!ref_timer_calibration_valid(16000u,31u));
    assert(!ref_timer_calibration_valid(16000u,1025u));
    assert(!ref_timer_calibration_valid(1u,1024u));
    assert(ref_timer_us(500u,16000u,32u) == 1000u);
    assert(ref_timer_us(0xffffffffu,16000u,32u) == 8589934590ull);
    uint8_t window[REF_WINDOW_BYTES] = {0};
    assert(!ref_window_valid(window));
    badger_bytes_store_u32(window,REF_WINDOW_MAGIC);
    badger_bytes_store_u32(window+4u,1u);
    badger_bytes_store_u32(window+12u,REF_WINDOW_INTERVAL);
    badger_bytes_store_u32(window+16u,16000u);
    badger_bytes_store_u32(window+20u,32u);
    uint8_t *r = window+REF_WINDOW_HEADER_BYTES;
    badger_bytes_store_u32(r+4u,10u);
    assert(ref_window_valid(window));
    badger_bytes_store_u32(window+24u,2u);
    assert(!ref_window_valid(window));
    badger_bytes_store_u32(window+24u,0u);
    window[32u] = 1u;
    assert(!ref_window_valid(window));
    badger_bytes_store_u32(window+24u,1u);
    assert(ref_window_valid(window));
    window[28u] = 1u;
    assert(ref_window_valid(window));
    window[64u] = 2u;
    assert(ref_window_valid(window));
    window[28u] = 0u;
    assert(!ref_window_valid(window));
    window[28u] = 2u;
    assert(!ref_window_valid(window));
    window[28u] = 3u;
    assert(ref_window_valid(window));
    window[28u] = 4u;
    assert(!ref_window_valid(window));
    window[28u] = 5u;
    assert(ref_window_valid(window));
    window[96u] = 2u;
    window[98u] = 1u;
    assert(ref_window_valid(window));
    window[28u] = 7u;
    assert(ref_window_valid(window));
    window[28u] = 6u;
    assert(!ref_window_valid(window));
    window[28u] = 9u;
    assert(!ref_window_valid(window));
    window[28u] = 11u;
    assert(!ref_window_valid(window));
    window[28u] = 13u;
    assert(ref_window_valid(window));
    window[28u] = 15u;
    assert(ref_window_valid(window));
    window[28u] = 16u;
    assert(!ref_window_valid(window));
    window[28u] = 5u;
    window[96u] = 5u;
    assert(!ref_window_valid(window));
    window[96u] = window[97u] = 0xffu;
    assert(ref_window_valid(window));
    r[158u] = 2u;
    assert(ref_window_valid(window));
    r[158u] = 5u;
    assert(!ref_window_valid(window));
    r[158u] = r[159u] = 0xffu;
    assert(ref_window_valid(window));
    window[28u] = 3u;
    assert(!ref_window_valid(window));
    memset(window+96u,0,32u);
    assert(!ref_window_valid(window));
    r[158u] = r[159u] = 0u;
    window[28u] = window[32u] = window[64u] = 0u;
    badger_bytes_store_u32(window+24u,0u);
    assert(!ref_window_coherent(r));
    badger_bytes_store_u32(r+108u,2u);
    badger_bytes_store_u32(r+140u,2u);
    badger_bytes_store_u32(r+144u,2u);
    assert(ref_window_coherent(r));
    badger_bytes_store_u32(r+144u,4u);
    assert(!ref_window_coherent(r));
    badger_bytes_store_u32(r+108u,3u);
    badger_bytes_store_u32(r+140u,3u);
    badger_bytes_store_u32(r+144u,3u);
    assert(!ref_window_coherent(r));
    assert(ref_window_valid(window));
    badger_bytes_store_u32(window+4u,REF_WINDOW_RECORDS+1u);
    assert(!ref_window_valid(window));
    badger_bytes_store_u32(window+4u,2u);
    uint8_t *next = r+REF_WINDOW_RECORD_BYTES;
    badger_bytes_store_u32(next+4u,10u);
    assert(!ref_window_valid(window));
    badger_bytes_store_u32(next,REF_WINDOW_INTERVAL);
    assert(ref_window_valid(window));
    badger_bytes_store_u32(next+4u,0xffffffffu);
    assert(!ref_window_valid(window));
    badger_bytes_store_u32(next+4u,10u);
    window[sizeof(window)-1u] = 1u;
    assert(!ref_window_valid(window));
    window[sizeof(window)-1u] = 0u;
    badger_bytes_store_u32(window+8u,3u);
    assert(ref_window_valid(window));
    badger_bytes_store_u32(window+12u,REF_WINDOW_INTERVAL+1u);
    assert(!ref_window_valid(window));
    badger_bytes_store_u32(window+12u,REF_WINDOW_INTERVAL);
    badger_bytes_store_u32(window+4u,0u);
    assert(!ref_window_valid(window));
    badger_bytes_store_u32(window+4u,REF_WINDOW_RECORDS);
    for (uint32_t i = 0u; i < REF_WINDOW_RECORDS; ++i) {
        uint8_t *entry = window+REF_WINDOW_HEADER_BYTES+i*REF_WINDOW_RECORD_BYTES;
        badger_bytes_store_u32(entry,i*REF_WINDOW_INTERVAL);
        badger_bytes_store_u32(entry+4u,10u);
    }
    assert(ref_window_valid(window));
    badger_bytes_store_u32(r,45000000u);
    assert(!ref_window_valid(window));
    badger_bytes_store_u32(r,0u);
    badger_bytes_store_u32(r+4u,0u);
    assert(!ref_window_valid(window));
    badger_bytes_store_u32(r+4u,10u);
    badger_bytes_store_u32(r+108u,4u);
    badger_bytes_store_u32(r+140u,2u);
    badger_bytes_store_u32(r+144u,2u);
    assert(!ref_window_coherent(r));
    badger_bytes_store_u32(window+16u,0u);
    assert(!ref_window_valid(window));
    badger_bytes_store_u32(window+16u,16000u);
    badger_bytes_store_u32(window+20u,31u);
    assert(!ref_window_valid(window));
    badger_bytes_store_u32(window+20u,32u);
    assert(ref_window_valid(window));
    window[0] ^= 1u;
    assert(!ref_window_valid(window));
}

static void phy_snapshot_commit(void) {
    uint8_t snapshot[CLEAN_OBSERVER_PHY_BYTES] = {0};
    assert(ref_phy_snapshot_valid(snapshot) == !CLEAN_RX_HT_PHY_SNAPSHOT);
    badger_bytes_store_u32(snapshot,CLEAN_OBSERVER_PHY_MAGIC);
    assert(!ref_phy_snapshot_valid(snapshot));
    badger_bytes_store_u32(snapshot+4u,1u);
    assert(!ref_phy_snapshot_valid(snapshot));
    badger_bytes_store_u32(snapshot+4u,3u);
    assert(ref_phy_snapshot_valid(snapshot) == !!CLEAN_RX_HT_PHY_SNAPSHOT);
    memset(snapshot+8u,0xff,sizeof(snapshot)-8u);
    assert(ref_phy_snapshot_valid(snapshot) == !!CLEAN_RX_HT_PHY_SNAPSHOT);
    badger_bytes_store_u32(snapshot+4u,7u);
    assert(!ref_phy_snapshot_valid(snapshot));
    badger_bytes_store_u32(snapshot+4u,3u);
    snapshot[0] ^= 1u;
    assert(!ref_phy_snapshot_valid(snapshot));
}

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
        int expected = !CLEAN_RX_HT_OBSERVER_FIFO || (!dropped && !pending);
        assert(ref_delivery_accounted(trace) == expected);
        if (!expected) continue;
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

static void fixed_stimulus(void) {
    uint8_t nonce[14] = {1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,3u,0u,6u};
    uint8_t packet[CLEAN_HT_STIMULUS_STORAGE];
    for (uint32_t mcs = 0u; mcs < 8u; ++mcs) {
        nonce[12] = (uint8_t)mcs;
        for (uint32_t index = 0u; index < CLEAN_HT_STIMULUS_COUNT; ++index) {
            uint32_t cmd = clean_ht_stimulus_frame(packet,nonce,index), kind = index%3u;
            uint32_t length = kind == 2u ? 1118u : 554u;
            assert(cmd && clean_tx_length(cmd) == length+6u);
            assert(kind ? ref_sig_valid(packet+8u) : clean_ofdm_plcp_valid(packet+8u,length));
            if (kind) assert(!!(packet[11]&8u) == (kind == 2u));
            for (uint32_t body = 0u; body < (kind == 2u ? 2u : 1u); ++body) {
                const uint8_t *d = packet+14u+body*560u;
                const uint8_t *m = kind == 2u ? d+4u : d;
                assert(ref_u16(m+22u)>>4u == 2u*index+body);
                assert(m[10] == 2u && !memcmp(m+11u,nonce,5u));
                assert(!memcmp(m+26u,nonce,14u));
                assert(diag_crc32(m,550u) == badger_bytes_load_u32(m+550u));
                if (kind == 2u) {
                    assert((ref_u16(d)>>4u) == 554u && !(d[0]&15u));
                    assert(d[2] == clean_ampdu_delimiter_crc(d) && d[3] == 0x4eu);
                    if (!body) assert(!d[558] && !d[559]);
                }
            }
            assert(clean_ht_stimulus_command(cmd,packet) ==
                   (CLEAN_RX_HT_STIMULUS && kind == 2u));
            if (kind == 1u)
                assert(clean_ht_data_command_valid(cmd,1u,0u,packet));
            if (kind != 2u && !REF_CLEAN_HANDOFF) {
                uint8_t received[18u+CLEAN_HT_STIMULUS_MPDU] = {0};
                memcpy(received+18u,packet+14u,CLEAN_HT_STIMULUS_MPDU);
                received[10] = kind ? 1u : 0u;
                received[15] = packet[8];
                received[16] = (uint8_t)(6u | (kind ? CLEAN_RX_UNSUPPORTED_PHY : CLEAN_RX_OFDM));
                assert(ref_stimulus_rx_index(received,sizeof(received),nonce) == index);
                assert(ref_stimulus_rx_index(received,sizeof(received)-1u,nonce) ==
                       CLEAN_HT_STIMULUS_COUNT);
                received[60] ^= 1u;
                assert(ref_stimulus_rx_index(received,sizeof(received),nonce) ==
                       CLEAN_HT_STIMULUS_COUNT);
                received[60] ^= 1u;
                received[16] ^= 0x80u;
                assert(ref_stimulus_rx_index(received,sizeof(received),nonce) ==
                       CLEAN_HT_STIMULUS_COUNT);
            }
            uint8_t record[2u*CLEAN_OBSERVER_RECORD_WORDS] = {0};
            memcpy(record+12u,packet+8u,6u);
            memcpy(record+36u,packet+14u,40u);
            assert(ref_stimulus_prefix_index(record,nonce) ==
                   (kind == 1u ? index : CLEAN_HT_STIMULUS_COUNT));
            record[12] ^= 1u;
            assert(ref_stimulus_prefix_index(record,nonce) == CLEAN_HT_STIMULUS_COUNT);
            record[12] ^= 1u; record[36u+26u] ^= 1u;
            assert(ref_stimulus_prefix_index(record,nonce) == CLEAN_HT_STIMULUS_COUNT);
            if (kind == 2u) {
                assert(!clean_ht_data_command_valid(cmd,0u,0u,packet));
                assert(!clean_ht_data_command_valid(cmd,1u,1u,packet));
                for (uint32_t bit = 0u; bit < 32u; ++bit)
                    assert(!clean_ht_stimulus_command(cmd^(1u<<bit),packet));
            }
            uint8_t r[REF_STIMULUS_RECORD_BYTES] = {0};
            badger_bytes_store_u32(r,index); badger_bytes_store_u32(r+4u,cmd);
            badger_bytes_store_u32(r+8u,100u); badger_bytes_store_u32(r+12u,200u);
            badger_bytes_store_u32(r+16u,1000u); badger_bytes_store_u32(r+20u,10000u);
            badger_bytes_store_u32(r+28u,7u); badger_bytes_store_u32(r+32u,length+6u);
            badger_bytes_store_u32(r+36u,diag_crc32(packet,sizeof(packet)));
            badger_bytes_store_u32(r+40u,CLEAN_TX_DIAG_MAGIC);
            memcpy(r+44u,packet+8u,6u);
            r[50] = kind ? 0x42u : 0x41u;
            uint16_t phyctl1 = kind ? clean_ht20_rates[mcs].phyctl1 : clean_ofdm_rates[0].phyctl1;
            r[52] = (uint8_t)phyctl1; r[53] = (uint8_t)(phyctl1>>8u);
            r[54] = 0x26u; r[55] = 8u;
            r[56] = (uint8_t)(length+2u); r[57] = (uint8_t)((length+2u)>>8u);
            badger_bytes_store_u32(r+60u,6u);
            if (REF_CLEAN_HANDOFF) continue;
            assert(ref_stimulus_record_valid(r,nonce,index));
            r[24] = 1u; assert(!ref_stimulus_record_valid(r,nonce,index)); r[24] = 0u;
            r[28] = 3u; assert(!ref_stimulus_record_valid(r,nonce,index)); r[28] = 7u;
            r[36] ^= 1u; assert(!ref_stimulus_record_valid(r,nonce,index)); r[36] ^= 1u;
            r[50] ^= 1u; assert(!ref_stimulus_record_valid(r,nonce,index)); r[50] ^= 1u;
            r[60] = 7u; assert(!ref_stimulus_record_valid(r,nonce,index));
        }
    }
    nonce[12] = 0u;
    uint32_t cmd = clean_ht_stimulus_frame(packet,nonce,2u);
    for (uint32_t i = 0u; i < sizeof(packet); ++i) {
        packet[i] ^= 1u;
        assert(!clean_ht_stimulus_command(cmd,packet));
        packet[i] ^= 1u;
    }
    assert(!clean_ht_stimulus_frame(packet,nonce,CLEAN_HT_STIMULUS_COUNT));
    nonce[12] = 8u;
    assert(!clean_ht_stimulus_frame(packet,nonce,2u));
    puts("Fixed stimulus: all rates/indices, exact lengths/delimiters/FCS/padding, gate/station/command/byte rejection and TX evidence guards pass");
}

static void handoff_stimulus(void) {
    uint8_t nonce[14] = {1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,0u,1u,0u,6u};
    uint8_t packet[CLEAN_HT_STIMULUS_STORAGE], received[18u+298u];
    for (uint32_t order = 1u; order <= 2u; ++order)
    for (uint32_t mode = 0u; mode < CLEAN_HT_HANDOFF_CASES; ++mode)
    for (uint32_t mcs = 0u; mcs < 8u; ++mcs)
    for (uint32_t index = 0u; index < CLEAN_HT_STIMULUS_COUNT; ++index) {
        nonce[10] = (uint8_t)mode; nonce[11] = (uint8_t)order; nonce[12] = (uint8_t)mcs;
        uint32_t cmd = clean_ht_handoff_frame(packet,nonce,index), kind = index%3u;
        uint32_t first = order == 1u ? 170u : 298u, second = 468u-first;
        assert(cmd && clean_tx_length(cmd) == 6u+(kind == 2u ? CLEAN_HT_HANDOFF_PSDU : first));
        assert(kind ? ref_sig_valid(packet+8u) : clean_ofdm_plcp_valid(packet+8u,first));
        if (kind == 2u) assert(ref_u16(packet+9u) == CLEAN_HT_HANDOFF_PSDU && (packet[11]&8u));
#if CLEAN_RX_HT_HANDOFF_GUARD
        if (kind == 2u) {
            uint32_t at = 14u+4u+((first+3u)&~3u)+4u+((second+3u)&~3u);
            const uint8_t *d = packet+at, *guard = d+4u;
            assert(ref_u16(d)>>4u == CLEAN_HT_HANDOFF_GUARD_BYTES);
            assert(d[2] == clean_ampdu_delimiter_crc(d) && d[3] == 0x4eu);
            assert(at+4u+CLEAN_HT_HANDOFF_GUARD_BYTES == 14u+CLEAN_HT_HANDOFF_PSDU);
            assert(packet[at-1u] == 0u && packet[at-2u] == 0u);
            assert(guard[0] == 0x88u && guard[1] == 2u && guard[24] == 0x60u &&
                guard[43] == 2u && ref_u16(guard+22u)>>4u == 2u*index+2u);
            assert(!memcmp(guard+26u,nonce,14u));
            assert(diag_crc32(guard,CLEAN_HT_HANDOFF_GUARD_BYTES-4u) ==
                badger_bytes_load_u32(guard+CLEAN_HT_HANDOFF_GUARD_BYTES-4u));
        }
#endif
        for (uint32_t i = CLEAN_HT_DATA_HEADER+clean_tx_length(cmd); i < sizeof(packet); ++i)
            assert(!packet[i]);
        for (uint32_t ordinal = 0u; ordinal < (kind == 2u ? 2u : 1u); ++ordinal) {
            uint32_t length = ordinal ? second : first;
            uint32_t at = 14u+(kind == 2u ? 4u+(ordinal ? 4u+((first+3u)&~3u) : 0u) : 0u);
            const uint8_t *mpdu = packet+at;
            int bad = index == 2u && ((mode == 1u && !ordinal) || (mode == 2u && ordinal));
            assert(ref_u16(mpdu+22u)>>4u == 2u*index+ordinal);
            assert(!memcmp(mpdu+26u,nonce,14u));
            assert((diag_crc32(mpdu,length-4u) == badger_bytes_load_u32(mpdu+length-4u)) == !bad);
            if (kind == 2u) {
                const uint8_t *d = mpdu-4u;
                assert(mpdu[1] == (ordinal ? 2u : 0x22u));
                assert(ref_u16(d)>>4u == length+(index == 2u && mode == 4u && ordinal ? 8u : 0u));
                assert((d[2] == clean_ampdu_delimiter_crc(d)) ==
                    !(index == 2u && mode == 3u && ordinal));
                assert(d[3] == 0x4eu);
            }
            memset(received,0,sizeof(received));
            memcpy(received+18u,mpdu,length);
            received[10] = kind ? 1u : 0u;
            received[11] = kind == 2u && ordinal ? 0x80u : 0u;
            received[15] = packet[8u];
            received[16] = (uint8_t)((kind ? CLEAN_RX_UNSUPPORTED_PHY : CLEAN_RX_OFDM)|6u);
            if (REF_CLEAN_HANDOFF) {
                if (kind == 2u)
                    assert(ref_handoff_rx_index(received,18u+length,nonce) ==
                        (bad ? CLEAN_HT_STIMULUS_COUNT : index));
                else assert(ref_stimulus_rx_index(received,18u+length,nonce) == index);
                received[60] ^= 1u;
                assert(ref_handoff_rx_index(received,18u+length,nonce) == CLEAN_HT_STIMULUS_COUNT);
            }
        }
        if (kind == 2u) {
            assert(clean_ht_handoff_command(cmd,packet) == CLEAN_RX_HT_HANDOFF_STIMULUS);
            assert(clean_ht_data_command_valid(cmd,1u,0u,packet) == CLEAN_RX_HT_HANDOFF_STIMULUS);
            assert(!clean_ht_data_command_valid(cmd,0u,0u,packet));
            assert(!clean_ht_data_command_valid(cmd,1u,1u,packet));
            for (uint32_t bit = 0u; bit < 32u; ++bit)
                assert(!clean_ht_handoff_command(cmd^(1u<<bit),packet));
            if (mcs == 0u && index == 2u)
                for (uint32_t byte = 0u; byte < sizeof(packet); ++byte) {
                    packet[byte] ^= 1u;
                    assert(!clean_ht_handoff_command(cmd,packet));
                    packet[byte] ^= 1u;
                }
        }
    }
    nonce[10] = CLEAN_HT_HANDOFF_CASES;
    assert(!clean_ht_handoff_frame(packet,nonce,2u));
    nonce[10] = 0u; nonce[11] = 3u;
    assert(!clean_ht_handoff_frame(packet,nonce,2u));
    nonce[11] = 1u; nonce[12] = 8u;
    assert(!clean_ht_handoff_frame(packet,nonce,2u));
    nonce[12] = 0u;
    assert(!clean_ht_handoff_frame(packet,nonce,CLEAN_HT_STIMULUS_COUNT));
    if (REF_CLEAN_HANDOFF) for (uint32_t order = 1u; order <= 2u; ++order) {
        uint8_t log[CLEAN_OBSERVER_WORDS*2u] = {0};
        uint8_t early[CLEAN_OBSERVER_EARLY_WORDS*2u] = {0};
        uint8_t second[CLEAN_OBSERVER_SECOND_HEADER*2u+CLEAN_OBSERVER_SECOND_MAX] = {0};
        nonce[10] = 2u; nonce[11] = (uint8_t)order;
        assert(clean_ht_handoff_frame(packet,nonce,2u));
        uint32_t first = clean_ht_handoff_length(nonce,0u), last = 468u-first;
        log[0] = log[2] = 1u;
        uint8_t *record = log+2u*CLEAN_OBSERVER_HEADER_WORDS;
        memcpy(record+12u,packet+8u,6u); record[20] = 7u;
        early[0] = 1u; early[2] = 7u;
        early[4] = (uint8_t)(first+6u); early[5] = (uint8_t)((first+6u)>>8u);
        early[7] = 1u; early[20] = 2u; early[24] = 1u;
        memcpy(early+14u,packet+8u,6u);
        memcpy(early+CLEAN_OBSERVER_EARLY_HEADER*2u,packet+18u,first);
        second[0] = 1u; second[2] = 7u;
        second[4] = (uint8_t)(last+6u); second[5] = (uint8_t)((last+6u)>>8u);
        second[7] = CLEAN_RX_HT_HANDOFF_GUARD ? 0u : 0x40u; second[20] = 2u;
        memcpy(second+CLEAN_OBSERVER_SECOND_HEADER*2u,
            packet+18u+4u+((first+3u)&~3u),last);
        assert(ref_handoff_bad_second(log,early,second,nonce));
#if CLEAN_RX_HT_COPY_PROBE
        uint8_t copy[2u*CLEAN_OBSERVER_COPY_WORDS] = {0};
        assert(!ref_copy_probe_valid(copy,early,second));
        early[24] = 0u;
        assert(ref_copy_probe_valid(copy,early,second));
        early[24] = 1u;
        copy[0] = 1u; copy[2] = 7u; copy[4] = 1u;
        copy[6] = CLEAN_OBSERVER_COPY_VERSION;
        for (uint32_t i = 0u; i < 4u; ++i) {
            copy[8u+2u*i] = 0x5au; copy[9u+2u*i] = 0xa5u;
        }
        assert(ref_copy_probe_valid(copy,early,second));
        const uint32_t copy_faults[] = {0u,2u,4u,6u,8u,10u,12u,14u};
        for (uint32_t i = 0u; i < sizeof(copy_faults)/sizeof(copy_faults[0]); ++i) {
            copy[copy_faults[i]] ^= 1u;
            assert(!ref_copy_probe_valid(copy,early,second));
            copy[copy_faults[i]] ^= 1u;
        }
#if CLEAN_RX_HT_COPY_WAIT
        copy[4] = 5u;
        assert(ref_copy_probe_valid(copy,early,second));
#endif
        copy[4] = 3u;
        copy[20] = (uint8_t)(last+6u); copy[21] = (uint8_t)((last+6u)>>8u);
        assert(ref_copy_probe_valid(copy,early,second));
        second[2] ^= 1u;
        assert(!ref_copy_probe_valid(copy,early,second));
        second[2] ^= 1u;
        copy[20] = 49u; copy[21] = 0u;
        assert(!ref_copy_probe_valid(copy,early,second));
        copy[20] = 51u; copy[21] = 1u;
        assert(!ref_copy_probe_valid(copy,early,second));
#endif
        const uint32_t early_faults[] = {0u,2u,4u,7u,14u,20u,24u,
            CLEAN_OBSERVER_EARLY_HEADER*2u+22u};
        for (uint32_t i = 0u; i < sizeof(early_faults)/sizeof(early_faults[0]); ++i) {
            early[early_faults[i]] ^= 1u;
            assert(!ref_handoff_bad_second(log,early,second,nonce));
            early[early_faults[i]] ^= 1u;
        }
        const uint32_t second_faults[] = {0u,2u,4u,20u,CLEAN_OBSERVER_SECOND_HEADER*2u,
            CLEAN_OBSERVER_SECOND_HEADER*2u+1u,
            CLEAN_OBSERVER_SECOND_HEADER*2u+last-1u};
        for (uint32_t i = 0u; i < sizeof(second_faults)/sizeof(second_faults[0]); ++i) {
            second[second_faults[i]] ^= 1u;
            assert(!ref_handoff_bad_second(log,early,second,nonce));
            second[second_faults[i]] ^= 1u;
        }
        second[7] ^= 0x40u;
        assert(!ref_handoff_bad_second(log,early,second,nonce));
    }
    puts("Controlled handoff: both orders, five cases, eight MCS, all indices, exact gates and byte mutations pass");
}

int main(void) {
    handoff_stimulus();
    assert(badger_gfx_glyph_row('A',0u) == 14u);
    assert(badger_gfx_glyph_row('0',0u) == 14u);
    assert(badger_gfx_glyph_row('A',7u) == 0u);
    assert(badger_gfx_glyph_row(' ',0u) == 0u);
    window_encoding();
    phy_snapshot_commit();
    delivery_accounting();
    fixed_stimulus();
    uint8_t frame[REF_FRAME_MAX+8u], nonce[16], peer[6] = {2u,1u,2u,3u,4u,5u};
    for (uint32_t i = 0u; i < 16u; ++i) nonce[i] = (uint8_t)i;
    nonce[11] = 0u;
    uint8_t prepared_frames[REF_EARLY_PREPARED][REF_FRAME_MAX];
    uint32_t prepared_digests[REF_EARLY_PREPARED];
    assert(!ref_prepare_early(prepared_frames,prepared_digests,nonce,peer));
    for (uint32_t profile = 1u; profile <= 3u; ++profile) {
        nonce[11] = (uint8_t)profile;
        assert(ref_prepare_early(prepared_frames,prepared_digests,nonce,peer));
        uint32_t expected_digest = REF_DIGEST_INITIAL;
        for (uint32_t i = 0u; i < REF_EARLY_PREPARED; ++i) {
            uint32_t size = ref_frame(frame,nonce,peer,i);
            assert(!memcmp(frame,prepared_frames[i],size));
            expected_digest = ref_digest(expected_digest,frame,size);
            assert(prepared_digests[i] == expected_digest);
        }
    }
    nonce[11] = 0u;
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
    for (uint32_t profile = 1u; profile <= 3u; ++profile) {
        nonce[11] = (uint8_t)profile;
        for (uint32_t i = 0u; i < REF_FRAME_COUNT; ++i) {
            memset(frame,0xa5,sizeof(frame));
            uint32_t size = ref_frame(frame,nonce,peer,i);
            assert(size == (64u<<profile) && size == ref_fixture_size((uint8_t)profile));
            assert(badger_bytes_load_u32(frame+size-4u) == diag_crc32(frame,size-4u));
            assert(badger_bytes_load_u32(frame+22u) == i);
            for (uint32_t j = size; j < sizeof(frame); ++j) assert(frame[j] == 0xa5u);
        }
    }
    nonce[11] = 4u;
    assert(!ref_frame(frame,nonce,peer,0u) && !ref_fixture_size(4u));
    nonce[11] = 0u;
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
    uint8_t reason_record[CLEAN_OBSERVER_RECORD_WORDS*2u] = {0};
    uint8_t reason_early[CLEAN_OBSERVER_EARLY_HEADER*2u] = {0};
    assert(!strcmp(ref_early_reason(reason_record,reason_early,1u),"invalid_ht_sig"));
    assert(clean_ht_ampdu_probe_sig(reason_record+12u,0u));
    reason_record[92] = 176u;
    assert(!strcmp(ref_early_reason(reason_record,reason_early,1u),"eligible_not_retained"));
    reason_early[0] = 1u;
    reason_early[20] = 2u;
    assert(!strcmp(ref_early_reason(reason_record,reason_early,1u),"retained_multi_candidate"));
    assert(!strcmp(ref_early_reason(reason_record,reason_early,2u),"prior_multi_already_retained"));
    reason_record[92] = (CLEAN_OBSERVER_EARLY_MAX+7u)&255u;
    reason_record[93] = (CLEAN_OBSERVER_EARLY_MAX+7u)>>8u;
    assert(!strcmp(ref_early_reason(reason_record,reason_early,2u),"early_storage_limit"));
    reason_record[92] = 49u; reason_record[93] = 0u;
    assert(!strcmp(ref_early_reason(reason_record,reason_early,2u),"short_good_hint"));
    reason_record[92] = (CLEAN_HT_AMPDU_PROBE_BYTES+6u)&255u;
    reason_record[93] = (CLEAN_HT_AMPDU_PROBE_BYTES+6u)>>8u;
    assert(!strcmp(ref_early_reason(reason_record,reason_early,2u),"inconsistent_body_geometry"));
    reason_record[92] = (CLEAN_HT_AMPDU_PROBE_BYTES+2u)&255u;
    reason_record[93] = (CLEAN_HT_AMPDU_PROBE_BYTES+2u)>>8u;
    assert(!strcmp(ref_early_reason(reason_record,reason_early,2u),"single_body_good_hint"));
    reason_record[92] = reason_record[93] = 0u;
    assert(!strcmp(ref_early_reason(reason_record,reason_early,2u),"no_nonterminal_good_hint"));
    reason_record[22] = 1u;
    reason_record[28] = (CLEAN_HT_AMPDU_PROBE_BYTES+6u)&255u;
    reason_record[29] = (CLEAN_HT_AMPDU_PROBE_BYTES+6u)>>8u;
    assert(!strcmp(ref_early_reason(reason_record,reason_early,2u),"no_nonterminal_good_hint"));
    reason_record[28] = (CLEAN_HT_AMPDU_PROBE_BYTES+2u)&255u;
    reason_record[29] = (CLEAN_HT_AMPDU_PROBE_BYTES+2u)>>8u;
    assert(!strcmp(ref_early_reason(reason_record,reason_early,2u),"terminal_single_body"));
    assert(clean_ht_ampdu_probe_sig(reason_record+12u,1u));
    assert(!strcmp(ref_early_reason(reason_record,reason_early,2u),"unsupported_early_mcs"));
    assert(clean_ht20_sig(reason_record+12u,252u,0u));
    assert(!strcmp(ref_early_reason(reason_record,reason_early,2u),"not_ampdu_format"));
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
    uint8_t guard_sig[6];
    assert(clean_ht20_ampdu_sig(guard_sig,564u,0u));
    memcpy(early+14u,guard_sig,6u); memcpy(record+12u,guard_sig,6u);
    record[33] = 1u;
    assert(ref_pair_correlated(log,early,sample,mac) == CLEAN_RX_HT_HANDOFF_GUARD);
    for (uint32_t status = 0u; status < 3u; ++status) {
        record[33] = status == 0u ? 0u : status == 1u ? 0x41u : 0x81u;
        assert(!ref_pair_correlated(log,early,sample,mac));
    }
    memcpy(early+14u,pair_sig,6u); memcpy(record+12u,pair_sig,6u);
    record[33] = 0x40u;
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

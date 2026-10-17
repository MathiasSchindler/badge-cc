#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ht_station.h"
#include "../src/ht20.h"
#include "frames.h"

static void packet_tests(void) {
    static const uint16_t sizes[] = {28u,29u,60u,88u,512u,1528u,2026u,2027u,2332u};
    uint8_t packet[CLEAN_HT_DATA_STORAGE], nonce[14] = {1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u};
    for (uint32_t mcs = 0u; mcs < 8u; ++mcs) {
        nonce[13] = (uint8_t)mcs;
        for (uint32_t i = 0u; i < sizeof(sizes)/sizeof(sizes[0]); ++i) {
            uint32_t length = sizes[i], total = length + 6u;
            nonce[11] = (uint8_t)length; nonce[12] = (uint8_t)(length >> 8u);
            assert(clean_ht_data_frame(packet, nonce));
            uint32_t cmd = CLEAN_HT_DATA_CMD | clean_tx_length_bits(total) | (1u << 19u);
            assert(clean_ht_data_command_valid(cmd, 1u, 0u, packet));
            assert(!clean_ht_data_command_valid(cmd, 0u, 0u, NULL));
            assert(!clean_ht_data_command_valid(cmd, 1u, 1u, NULL));
            assert(clean_ht_data_match(packet + 14u, length, nonce));
            assert(clean_frame_valid(packet + 14u, length));
            clean_ht_rx_t trace = {.magic=CLEAN_HT_RX_MAGIC,.captures=1u,
                                  .rxe_length=(uint16_t)total,.phy={2u,0u,0u,0u}};
            memcpy(trace.sig, packet + 8u, 6u);
            memcpy(trace.mpdu, packet + 14u,
                   length < sizeof(trace.mpdu) ? length : sizeof(trace.mpdu));
            assert(clean_ht_data_trace_valid(&trace, nonce) == (length <= sizeof(trace.mpdu)));
            trace.phy[0] = 3u;
            assert(!clean_ht_data_trace_valid(&trace, nonce));
            trace.phy[0] = 2u; trace.captures = 0u;
            assert(!clean_ht_data_trace_valid(&trace, nonce));
            assert(diag_crc32(packet + 14u, length - 4u) ==
                   badger_bytes_load_u32(packet + 14u + length - 4u));
            for (uint32_t j = 8u + total; j < sizeof(packet); ++j) assert(!packet[j]);
            for (uint32_t bit = 0u; bit < 32u; ++bit)
                assert(!clean_ht_data_command_valid(cmd ^ (1u << bit), 1u, 0u, packet));
            for (uint32_t bit = 0u; bit < 14u*8u; ++bit) {
                packet[bit/8u] ^= (uint8_t)(1u << (bit & 7u));
                assert(!clean_ht_data_command_valid(cmd, 1u, 0u, packet));
                packet[bit/8u] ^= (uint8_t)(1u << (bit & 7u));
            }
            for (uint32_t pos = 0u; pos < length; ++pos) {
                packet[14u+pos] ^= 1u;
                assert(!clean_ht_data_match(packet + 14u, length, nonce));
                packet[14u+pos] ^= 1u;
            }
            static const uint8_t unsafe_offsets[] = {0u,1u,2u,3u,4u,10u,16u,22u};
            static const uint8_t unsafe_masks[] = {0x80u,0x40u,1u,1u,1u,2u,1u,1u};
            for (uint32_t fault = 0u; fault < sizeof(unsafe_offsets); ++fault) {
                uint8_t *m = packet + 14u;
                m[unsafe_offsets[fault]] ^= unsafe_masks[fault];
                badger_bytes_store_u32(m + length - 4u, diag_crc32(m, length - 4u));
                assert(!clean_ht_data_command_valid(cmd, 1u, 0u, packet));
                m[unsafe_offsets[fault]] ^= unsafe_masks[fault];
                badger_bytes_store_u32(m + length - 4u, diag_crc32(m, length - 4u));
            }
            if (length > 28u) {
                uint8_t *m = packet + 14u;
                m[24] ^= 1u;
                badger_bytes_store_u32(m + length - 4u, diag_crc32(m, length - 4u));
                assert(clean_ht_data_command_valid(cmd, 1u, 0u, packet));
                assert(!clean_ht_data_match(m, length, nonce));
                m[24] ^= 1u;
                badger_bytes_store_u32(m + length - 4u, diag_crc32(m, length - 4u));
            }
            for (uint32_t channel = 1u; channel <= 11u; ++channel) {
                for (uint32_t format = 0u; format < 4u; ++format) {
                    uint8_t version = clean_ht_rx_version(1u, (uint16_t)format,
                                                          packet + 8u, length);
                    assert(version == (format == 2u ? 1u : 0u));
                    assert(!clean_ht_rx_version(0u, (uint16_t)format, NULL, length));
                }
                uint8_t flags = CLEAN_RX_HT20 | CLEAN_RX_UNSUPPORTED_PHY | channel;
                assert(clean_ht_rx_metadata((uint8_t)mcs, flags, 1u, length, channel, mcs));
                for (uint32_t bit = 0u; bit < 8u; ++bit) {
                    assert(!clean_ht_rx_metadata((uint8_t)mcs, flags ^ (1u << bit),
                                                1u, length, channel, mcs));
                    assert(!clean_ht_rx_metadata((uint8_t)(mcs ^ (1u << bit)),
                                                flags, 1u, length, channel, mcs));
                    assert(!clean_ht_rx_metadata((uint8_t)mcs, flags,
                        (uint8_t)(1u ^ (1u << bit)), length, channel, mcs));
                }
            }
            assert(!clean_ht_rx_metadata((uint8_t)mcs, 0xc0u, 1u, length, 0u, mcs));
            assert(!clean_ht_rx_metadata((uint8_t)mcs, 0xccu, 1u, length, 12u, mcs));
            assert(!clean_ht_rx_version(1u, 2u, packet + 8u, length + 1u));
        }
    }
    uint32_t cmd = CLEAN_HT_DATA_CMD | clean_tx_length_bits(34u) | (1u << 19u);
    assert(!clean_ht_data_command_valid(cmd & ~(1u << 19u), 1u, 0u, NULL));
    assert(!clean_ht_data_command_valid(CLEAN_HT_DATA_CMD, 1u, 0u, NULL));
    assert(!clean_ht_data_command_valid(CLEAN_HT_DATA_CMD | clean_tx_length_bits(4095u) |
                                       (1u << 19u), 1u, 0u, NULL));
    memset(packet, 0xa5, sizeof(packet));
    nonce[11] = 27u; nonce[12] = 0u;
    assert(!clean_ht_data_frame(packet, nonce));
    for (uint32_t i = 0u; i < sizeof(packet); ++i) assert(packet[i] == 0xa5u);
    nonce[11] = 28u; nonce[13] = 8u;
    assert(!clean_ht_data_frame(packet, nonce));
    nonce[11] = (uint8_t)(CLEAN_HT_DATA_MAX_MPDU + 1u);
    nonce[12] = (uint8_t)((CLEAN_HT_DATA_MAX_MPDU + 1u) >> 8u);
    nonce[13] = 0u;
    assert(!clean_ht_data_frame(packet, nonce));
    assert(!clean_ht_data_match(NULL, UINT32_MAX, nonce));
    puts("HT data v1: all MCS, size/fragment boundaries, version/flags/FCS and station/default gates pass; NOT RF");
}

static void peer_tests(void) {
    uint8_t cap[26] = {0}, op[22] = {0};
    uint8_t wmm[24] = {
        0u,0x50u,0xf2u,2u,1u,1u,0x83u,0u,
        3u,0xa4u,0u,0u, 0x27u,0xa4u,0u,0u,
        0x42u,0x43u,94u,0u, 0x62u,0x32u,47u,0u
    };
    cap[0] = 2u; cap[2] = 0x1bu; cap[3] = 255u;
    op[0] = 1u; op[1] = 5u; op[6] = 1u;
    clean_ht_peer_t peer = {0};
    assert(clean_ht_peer_fallback(&peer, 1u) & CLEAN_HT_FALLBACK_NO_HT);
    clean_ht_peer_ie(&peer, 45u, cap, sizeof(cap));
    clean_ht_peer_ie(&peer, 61u, op, sizeof(op));
    clean_ht_peer_ie(&peer, 221u, wmm, sizeof(wmm));
    assert(clean_ht_peer_fallback(&peer, 1u) == CLEAN_HT_FALLBACK_MAC);
    assert(peer.rx_mcs == 255u && peer.basic_mcs == 1u && peer.width40 == 1u);
    assert(peer.ampdu_exponent == 3u && peer.mpdu_spacing == 6u);
    assert(peer.wmm.present && peer.wmm.parameter_set == 3u && peer.wmm.uapsd);
    assert(peer.wmm.ac[0].aifsn == 3u && peer.wmm.ac[1].aifsn == 7u);
    assert(peer.wmm.ac[2].txop_32us == 94u && peer.wmm.ac[3].txop_32us == 47u);
    assert(clean_ht_peer_fallback(&peer, 6u) & CLEAN_HT_FALLBACK_CHANNEL);
    assert(clean_ht_peer_fallback(&peer, 0u) & CLEAN_HT_FALLBACK_CHANNEL);
    clean_ht_peer_t next = peer;
    next.unsupported_basic = 1u;
    assert(clean_ht_peer_fallback(&next, 1u) & CLEAN_HT_FALLBACK_MCS);
    next = peer; next.rx_mcs = 0xfeu;
    assert(clean_ht_peer_fallback(&next, 1u) & CLEAN_HT_FALLBACK_MCS);
    next = peer; next.wmm.ac[0].acm = 1u;
    assert(clean_ht_peer_fallback(&next, 1u) & CLEAN_HT_FALLBACK_WMM);
    next = peer; next.protection = 1u;
    assert(clean_ht_peer_fallback(&next, 1u) & CLEAN_HT_FALLBACK_PROTECTION);
    next = peer; next.capability &= (uint16_t)~2u;
    assert(clean_ht_peer_fallback(&next, 1u) & CLEAN_HT_FALLBACK_MALFORMED);
    for (uint32_t id = 0u; id < 3u; ++id) {
        uint32_t ie = id == 0u ? 45u : id == 1u ? 61u : 221u;
        const uint8_t *p = id == 0u ? cap : id == 1u ? op : wmm;
        uint32_t size = id == 0u ? 26u : id == 1u ? 22u : 24u;
        next = peer;
        clean_ht_peer_ie(&next, ie, p, size);
        assert(next.invalid);
        for (uint32_t truncated = id == 2u ? 4u : 0u; truncated < size; ++truncated) {
            memset(&next, 0, sizeof(next));
            clean_ht_peer_ie(&next, ie, p, truncated);
            assert(next.invalid);
        }

        memset(&next, 0, sizeof(next));
        uint8_t extra[27] = {0};
        memcpy(extra, p, size);
        clean_ht_peer_ie(&next, ie, extra, size + 1u);
        assert(next.invalid);
    }
    for (uint32_t fault = 0u; fault < 8u; ++fault) {
        uint8_t bad[24];
        memcpy(bad, wmm, sizeof(bad));
        if (fault == 0u) bad[4] = 0u;
        if (fault == 1u) bad[5] = 2u;
        if (fault == 2u) bad[7] = 1u;
        if (fault == 3u) bad[6] |= 0x10u;
        if (fault == 4u) bad[12] = bad[8];
        if (fault == 5u) bad[8] |= 0x80u;
        if (fault == 6u) bad[8] &= 0xf0u;
        if (fault == 7u) bad[9] = 0x34u;
        memset(&next, 0, sizeof(next));
        clean_ht_peer_ie(&next, 221u, bad, sizeof(bad));
        assert(next.invalid && !next.wmm.present);
    }
    for (uint32_t permutation = 0u; permutation < 4u; ++permutation) {
        uint8_t reordered[24];
        memcpy(reordered, wmm, 8u);
        for (uint32_t i = 0u; i < 4u; ++i)
            memcpy(reordered + 8u + 4u*i, wmm + 8u + 4u*((i + permutation) & 3u), 4u);
        memset(&next, 0, sizeof(next));
        clean_ht_peer_ie(&next, 221u, reordered, sizeof(reordered));
        assert(!next.invalid && next.wmm.present);
        for (uint32_t i = 0u; i < 4u; ++i) {
            assert(next.wmm.ac[i].aifsn == peer.wmm.ac[i].aifsn);
            assert(next.wmm.ac[i].txop_32us == peer.wmm.ac[i].txop_32us);
        }
    }
    memset(&next, 0, sizeof(next));
    clean_ht_peer_ie(&next, 221u, NULL, 0u);
    assert(!next.invalid);
    uint8_t info[7] = {0u,0x50u,0xf2u,2u,0u,1u,0x80u};
    clean_ht_peer_ie(&next, 221u, info, sizeof(info));
    assert(!next.invalid && next.wmm.information && !next.wmm.present);
    assert(clean_ht_peer_fallback(&next, 1u) & CLEAN_HT_FALLBACK_WMM);
    clean_ht_peer_ie(&next, 221u, info, sizeof(info));
    assert(next.invalid);
    uint8_t detail[2048];
    memset(detail, 0xa5, sizeof(detail));
    clean_ht_peer_report(&peer, 1u, detail);
    assert(badger_bytes_load_u32(detail + CLEAN_HT_PEER_DETAIL) == CLEAN_HT_PEER_MAGIC);
    assert(badger_bytes_load_u32(detail + CLEAN_HT_PEER_DETAIL + 4u) == CLEAN_HT_FALLBACK_MAC);
    for (uint32_t i = 0u; i < sizeof(detail); ++i)
        if (i < CLEAN_HT_PEER_DETAIL || i >= CLEAN_HT_PEER_DETAIL + CLEAN_HT_PEER_BYTES)
            assert(detail[i] == 0xa5u);
    puts("HT/WMM peer profile: capability/operation/EDCA, malformed/duplicate IEs, AC order, basic MCS and explicit legacy fallback pass; NOT EDCA execution");
}

static void amsdu_tests(void) {
    uint8_t nonce[14] = {1u,2u,3u,4u,5u,6u};
    nonce[11] = (uint8_t)CLEAN_AMSDU_MPDU_BYTES;
    nonce[12] = (uint8_t)(CLEAN_AMSDU_MPDU_BYTES >> 8u);
    uint8_t body[CLEAN_AMSDU_BYTES];
    for (uint32_t i = 0u; i < sizeof(body); ++i)
        body[i] = clean_ht_amsdu_byte(i + 26u, nonce);
    clean_amsdu_t aggregate;
    clean_amsdu_subframe_t frame;
    assert(clean_amsdu_open(&aggregate, body, sizeof(body)));
    assert(clean_amsdu_next(&aggregate, &frame) && frame.length == 2304u);
    assert(frame.destination == body && frame.source == body + 6u &&
           frame.payload == body + 14u);
    assert(clean_amsdu_next(&aggregate, &frame) && frame.length == 1505u);
    assert(frame.payload == body + 2334u && !clean_amsdu_next(&aggregate, &frame));
    for (uint32_t length = 0u; length < sizeof(body); ++length)
        assert(clean_amsdu_open(&aggregate, body, length) == (length == 2318u));
    assert(!clean_amsdu_open(&aggregate, body, sizeof(body) + 1u));
    assert(!clean_amsdu_open(&aggregate, NULL, sizeof(body)));
    assert(!clean_amsdu_next(&aggregate, &frame));
    body[2318] = 0x89u; body[2319] = 0xacu;
    assert(clean_amsdu_open(&aggregate, body, sizeof(body)));
    body[2318] = body[2319] = 0u;
    for (uint32_t fault = 0u; fault < 7u; ++fault) {
        uint8_t saved[14];
        memcpy(saved, body + 2320u, sizeof(saved));
        if (fault == 0u) body[2326] |= 1u;
        if (fault == 1u) body[2332] = 10u;
        if (fault == 2u) body[2332] = body[2333] = 0u;
        if (fault == 3u) body[2333]++;
        if (fault == 4u) body[2333]--;
        if (fault == 5u) memcpy(body + 2320u, "\xaa\xaa\x03\x00\x00\x00", 6u);
        if (fault == 6u) body[2332] = 0xffu;
        assert(!clean_amsdu_open(&aggregate, body, sizeof(body)));
        assert(!clean_amsdu_next(&aggregate, &frame));
        memcpy(body + 2320u, saved, sizeof(saved));
    }
#if CLEAN_RX_HT_RX
    uint8_t packet[CLEAN_HT_DATA_STORAGE];
    clean_qos_rx_t qos;
    clean_qos_rx_init(&qos);
    for (uint32_t mcs = 0u; mcs < 8u; ++mcs) {
        nonce[13] = (uint8_t)mcs;
        assert(clean_ht_data_frame(packet, nonce));
        assert(packet[0] == CLEAN_HT_AMSDU_VERSION && packet[14] == 0x88u);
        uint32_t cmd = CLEAN_HT_DATA_CMD |
            clean_tx_length_bits(CLEAN_AMSDU_MPDU_BYTES + 6u) | (1u << 19u);
        assert(clean_ht_data_command_valid(cmd, 1u, 0u, packet));
        assert(!clean_ht_data_command_valid(cmd, 1u, 1u, NULL));
        assert(!clean_ht_data_command_valid(cmd, 0u, 0u, NULL));
        assert(clean_ht_data_match(packet + 14u, CLEAN_AMSDU_MPDU_BYTES, nonce));
        assert(clean_qos_rx(&qos, packet + 14u, CLEAN_AMSDU_MPDU_BYTES, mcs) == CLEAN_MAC_DELIVER);
        assert(clean_mac_retry(packet + 14u, CLEAN_AMSDU_MPDU_BYTES));
        assert(clean_qos_rx(&qos, packet + 14u, CLEAN_AMSDU_MPDU_BYTES, mcs) == CLEAN_MAC_DUPLICATE);
        packet[15] &= (uint8_t)~8u;
        badger_bytes_store_u32(packet + 14u + CLEAN_AMSDU_MPDU_BYTES - 4u,
                              diag_crc32(packet + 14u, CLEAN_AMSDU_MPDU_BYTES - 4u));
        for (uint32_t bit = 0u; bit < 14u * 8u; ++bit) {
            packet[bit/8u] ^= (uint8_t)(1u << (bit & 7u));
            assert(!clean_ht_data_command_valid(cmd, 1u, 0u, packet));
            packet[bit/8u] ^= (uint8_t)(1u << (bit & 7u));
        }
        for (uint32_t offset = 0u; offset < CLEAN_AMSDU_MPDU_BYTES; ++offset) {
            packet[14u+offset] ^= 1u;
            assert(!clean_ht_data_match(packet + 14u, CLEAN_AMSDU_MPDU_BYTES, nonce));
            packet[14u+offset] ^= 1u;
        }
    }
#else
    assert(!clean_ht_data_size_valid(CLEAN_AMSDU_MPDU_BYTES));
#endif
    puts("A-MSDU: full 3839-byte validation/deaggregation, truncation, malformed final subframe and LLC-as-DA rejection pass; NOT negotiated HT");
}

static void ack_packet_tests(void) {
    uint8_t nonce[14] = {1u,2u,3u,4u,5u,6u,0u,8u,9u,10u,11u,60u,0u,0u};
    uint8_t packet[CLEAN_HT_DATA_STORAGE];
    uint32_t cmd = CLEAN_HT_DATA_CMD | clean_tx_length_bits(CLEAN_HT_ACK_MPDU + 6u) |
        (1u << 19u);
    if (!CLEAN_RX_HT_RESPONSE) {
        assert(!clean_ht_ack_frame(NULL, NULL));
        assert(!clean_ht_clocked_frame(NULL, NULL));
        assert(!clean_ht_clocked_command_valid(cmd, 1u, 3u, NULL));
        assert(!clean_ht_ack_match(NULL, 60u, NULL));
        assert(clean_ht_data_frame(packet, nonce));
        packet[0] = CLEAN_HT_ACK_VERSION;
        assert(!clean_ht_data_command_valid(cmd, 1u, 0u, packet));
        return;
    }
    for (uint32_t mcs = 0u; mcs < 8u; ++mcs)
        for (uint32_t tid = 0u; tid < 16u; ++tid) {
            nonce[6] = (uint8_t)tid; nonce[13] = (uint8_t)mcs;
            assert(clean_ht_ack_frame(packet, nonce));
            assert(clean_ht_data_command_valid(cmd, 1u, 0u, packet));
            assert(clean_ht_ack_match(packet + 14u, 60u, nonce));
            assert(!clean_ht_data_match(packet + 14u, 60u, nonce));
            assert(packet[38u] == tid && packet[39u] == 0u);
            assert(packet[16u] == 10u + clean_ofdm_airtime_us(14u,
                (uint32_t)clean_ht_ack_rate(0x150u, mcs) - 4u));
            assert(!clean_ht_data_command_valid(cmd, 0u, 0u, NULL));
            assert(!clean_ht_data_command_valid(cmd, 1u, 1u, NULL));
            for (uint32_t bit = 0u; bit < 32u; ++bit)
                assert(!clean_ht_data_command_valid(cmd ^ (1u << bit), 1u, 0u, packet));
            for (uint32_t bit = 0u; bit < (14u + 60u) * 8u; ++bit) {
                packet[bit/8u] ^= (uint8_t)(1u << (bit & 7u));
                assert(!clean_ht_data_command_valid(cmd, 1u, 0u, packet));
                packet[bit/8u] ^= (uint8_t)(1u << (bit & 7u));
            }
            for (uint32_t pos = 0u; pos < 60u; ++pos) {
                uint8_t *m = packet + 14u;
                m[pos] ^= 1u;
                assert(!clean_ht_ack_match(m, 60u, nonce));
                if (pos < 56u) {
                    badger_bytes_store_u32(m + 56u, diag_crc32(m, 56u));
                    assert(!clean_ht_data_command_valid(cmd, 1u, 0u, packet));
                }
                m[pos] ^= 1u;
                badger_bytes_store_u32(m + 56u, diag_crc32(m, 56u));
            }
            assert(clean_ht_clocked_frame(packet, nonce));
            assert(!clean_ht_data_command_valid(cmd, 1u, 0u, packet));
            assert(clean_ht_clocked_command_valid(cmd, 1u, 3u, packet));
            assert(!clean_ht_clocked_command_valid(cmd, 0u, 3u, NULL));
            for (uint32_t profile = 0u; profile < 5u; ++profile)
                if (profile != 3u)
                    assert(!clean_ht_clocked_command_valid(cmd, 1u, profile, NULL));
            for (uint32_t bit = 0u; bit < 32u; ++bit)
                assert(!clean_ht_clocked_command_valid(cmd ^ (1u << bit), 1u, 3u, packet));
            for (uint32_t bit = 0u; bit < (14u + 60u) * 8u; ++bit) {
                packet[bit/8u] ^= (uint8_t)(1u << (bit & 7u));
                assert(!clean_ht_clocked_command_valid(cmd, 1u, 3u, packet));
                packet[bit/8u] ^= (uint8_t)(1u << (bit & 7u));
            }
        }
    nonce[11] = 59u;
    assert(!clean_ht_ack_frame(packet, nonce));
    nonce[11] = 60u; nonce[13] = 8u;
    assert(!clean_ht_ack_frame(packet, nonce));
    puts("HT normal-ACK fixture: all MCS/TIDs, complete fixed profile, fresh identity and default/station gates pass; NOT RF");
}

static void ack_evidence_tests(void) {
    for (uint32_t basic = 0u; basic < 4096u; ++basic)
        assert(clean_ht_clocked_policy_valid(3u, (uint16_t)basic, 1u, 3u) == (basic == 0x150u));
    for (uint32_t profile = 0u; profile < 5u; ++profile)
        for (uint32_t slot = 0u; slot < 4u; ++slot)
            for (uint32_t response = 0u; response < 5u; ++response)
                assert(clean_ht_clocked_policy_valid(profile, 0x150u, slot, response) ==
                       (profile == 3u && slot == 1u && response == 3u));
    uint8_t nonce[14] = {1u,2u,3u,4u,5u,6u,0u,8u,9u,10u,11u,60u,0u,0u};
    clean_ack_rx_t rx = {.magic=CLEAN_ACK_RX_MAGIC,.count=1u,.phy=1u};
    uint8_t *m = (uint8_t *)rx.mpdu;
    m[0] = 0xd4u;
    for (uint32_t i = 0u; i < 6u; ++i) m[4u+i] = clean_ht_ack_byte(10u+i, nonce);
    badger_bytes_store_u32(m + 10u, diag_crc32(m, 10u));
    assert(clean_ofdm_plcp_rate((uint8_t *)rx.plcp, 14u, 0u));
    assert(clean_ht_ack_evidence(&rx, nonce));
    for (uint32_t bit = 0u; bit < 14u*8u; ++bit) {
        m[bit/8u] ^= (uint8_t)(1u << (bit & 7u));
        assert(!clean_ht_ack_evidence(&rx, nonce));
        m[bit/8u] ^= (uint8_t)(1u << (bit & 7u));
    }
    for (uint32_t i = 0u; i < 10u; ++i) {
        m[i] ^= 1u;
        badger_bytes_store_u32(m + 10u, diag_crc32(m, 10u));
        assert(clean_ht_ack_evidence(&rx, nonce) == (i == 2u || i == 3u));
        m[i] ^= 1u;
        badger_bytes_store_u32(m + 10u, diag_crc32(m, 10u));
    }
    for (uint32_t count = 0u; count < 10u; ++count) {
        rx.count = (uint16_t)count;
        assert(clean_ht_ack_evidence(&rx, nonce) == (count == 1u));
    }
    rx.count = 1u; rx.magic ^= 1u;
    assert(!clean_ht_ack_evidence(&rx, nonce));
    rx.magic ^= 1u;
    for (uint32_t phy = 0u; phy < 4u; ++phy) {
        rx.phy = (uint16_t)phy;
        assert(clean_ht_ack_evidence(&rx, nonce) == (phy == 1u));
    }
    rx.phy = 1u;
    for (uint32_t mcs = 0u; mcs < 8u; ++mcs)
        for (uint32_t rate = 0u; rate < 8u; ++rate) {
            nonce[13] = (uint8_t)mcs;
            assert(clean_ofdm_plcp_rate((uint8_t *)rx.plcp, 14u, rate));
            assert(clean_ht_ack_evidence(&rx, nonce) ==
                   (rate + 4u == (uint32_t)clean_ht_ack_rate(0x150u, mcs)));
        }
    nonce[13] = 0u;
    assert(clean_ofdm_plcp_rate((uint8_t *)rx.plcp, 14u, 0u));
    uint8_t *plcp = (uint8_t *)rx.plcp;
    for (uint32_t bit = 0u; bit < 6u*8u; ++bit) {
        plcp[bit/8u] ^= (uint8_t)(1u << (bit & 7u));
        assert(!clean_ht_ack_evidence(&rx, nonce));
        plcp[bit/8u] ^= (uint8_t)(1u << (bit & 7u));
    }
    for (uint32_t mcs = 0u; mcs < 9u; ++mcs)
        for (uint32_t slot = 0u; slot < 30u; ++slot) {
            uint32_t timeout = clean_ht_ack_timeout_us(mcs, slot);
            assert(timeout == (mcs < 8u && (slot == 9u || slot == 20u) ?
                              10u + slot + 64u + 304u : 0u));
        }
    puts("Clocked HT: isolated v4 gates, all-MCS ACK timeout, saved ACK FCS/address/freshness checks pass; NOT RF");
}

int main(void) {
    packet_tests();
    ack_packet_tests();
    ack_evidence_tests();
    peer_tests();
    amsdu_tests();
    return 0;
}

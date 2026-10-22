#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mac.h"
#include "mac_soak.h"
#include "sdio_ring.h"
#include "../badge/station_rx.h"

static void fcs(uint8_t *m, uint32_t n) {
    uint32_t crc = clean_mac_fcs(m, n - 4u);
    for (uint32_t i = 0u; i < 4u; ++i) m[n - 4u + i] = (uint8_t)(crc >> (8u * i));
}

static uint32_t frame(uint8_t *m, uint16_t seq, uint8_t frag, uint8_t flags,
                       uint32_t payload) {
    assert(payload <= CLEAN_MSDU_BYTES);
    memset(m, 0, 28u + payload);
    m[0] = 8u;
    m[1] = flags;
    m[4] = 2u; m[10] = 4u; m[16] = 6u;
    m[22] = (uint8_t)((seq << 4u) | frag);
    m[23] = (uint8_t)(seq >> 4u);
    for (uint32_t i = 0u; i < payload; ++i) m[24u + i] = (uint8_t)(seq + frag + i);
    fcs(m, 28u + payload);
    return 28u + payload;
}

static void contention(void) {
    clean_dcf_t s;
    clean_dcf_init(&s, 0u, 1u, 1u);
    assert(s.slot_us == 9u && s.cw == 15u && s.slots <= s.cw);
    s.slots = 2u;
    for (uint32_t now = 0u; now < 46u; ++now) assert(!clean_dcf_poll(&s, now, 0));
    assert(clean_dcf_poll(&s, 46u, 0));
    clean_dcf_init(&s, 0u, 1u, 0u);
    s.slots = 2u;
    for (uint32_t now = 0u; now <= 75u; ++now) assert(!clean_dcf_poll(&s, now, 0));
    assert(s.slots == 1u);
    assert(!clean_dcf_poll(&s, 76u, 1));
    for (uint32_t now = 77u; now < 147u; ++now) assert(!clean_dcf_poll(&s, now, 0));
    assert(clean_dcf_poll(&s, 147u, 0));
    assert(!clean_dcf_poll(&s, 148u, 1));
    assert(s.slots == 0u);
    for (uint32_t now = 149u; now < 199u; ++now) assert(!clean_dcf_poll(&s, now, 0));
    assert(clean_dcf_poll(&s, 199u, 0));
    assert(!clean_dcf_poll(&s, 1000u, 0)); /* unsampled gap */
    for (uint32_t now = 1001u; now < 1050u; ++now) assert(!clean_dcf_poll(&s, now, 0));
    assert(clean_dcf_poll(&s, 1050u, 0));
    clean_dcf_nav(&s, 1050u, 100u);
    clean_dcf_nav(&s, 1060u, 20u);
    assert(s.nav_until == 1150u);
    clean_dcf_nav(&s, 1070u, 200u);
    assert(s.nav_until == 1270u);
    clean_dcf_nav(&s, 1080u, 0x8001u);
    assert(s.nav_until == 1270u);
    for (uint32_t now = 1080u; now < 1320u; ++now) assert(!clean_dcf_poll(&s, now, 0));
    assert(clean_dcf_poll(&s, 1320u, 0));
    clean_dcf_bad_rx(&s, 1320u);
    for (uint32_t now = 1320u; now < 1684u; ++now) assert(!clean_dcf_poll(&s, now, 0));
    assert(clean_dcf_poll(&s, 1684u, 0));
    clean_dcf_good_rx(&s);
    assert(s.ifs_us == 50u);
    uint16_t expected[] = {31u, 63u, 127u, 255u, 511u, 1023u, 1023u, 1023u};
    for (uint32_t i = 0u; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        clean_dcf_complete(&s, 2000u + i, 0);
        assert(s.cw == expected[i] && s.slots <= s.cw);
    }
    clean_dcf_complete(&s, 3000u, 1);
    assert(s.cw == 15u);
    clean_dcf_complete(&s, 3001u, 0);
    clean_dcf_bad_rx(&s, 3002u);
    clean_dcf_new_frame(&s, 3003u);
    assert(s.cw == 15u && s.ifs_us == 364u);
    clean_dcf_init(&s, 0xfffffff0u, 0u, 1u);
    s.slots = 0u;
    clean_dcf_nav(&s, 0xfffffff0u, 32u);
    for (uint32_t i = 0u; i < 60u; ++i)
        assert(!clean_dcf_poll(&s, 0xfffffff0u + i, 0));
    assert(clean_dcf_poll(&s, 0x2cu, 0));
    clean_dcf_t a, b;
    clean_dcf_init(&a, 0u, 12345u, 1u);
    clean_dcf_init(&b, 0u, 12345u, 1u);
    uint32_t selected = 0u;
    for (uint32_t i = 0u; i < 100000u; ++i) {
        assert(a.slots == b.slots && a.slots <= 15u);
        selected |= 1u << a.slots;
        clean_dcf_complete(&a, i, 1);
        clean_dcf_complete(&b, i, 1);
    }
    assert(selected == 0xffffu);
    clean_dcf_init(&s, 100u, 12345u, 1u);
    clean_dcf_complete(&s, 101u, 0);
    clean_dcf_bad_rx(&s, 102u);
    clean_dcf_nav(&s, 102u, 1000u);
    uint16_t slots = s.slots;
    uint32_t random = s.random;
    clean_dcf_slot(&s, 103u, 0u);
    assert(s.slot_us == 20u && s.ifs_us == 364u && !s.idle && !s.idle_us);
    assert(s.cw == 31u && s.slots == slots && s.random == random && s.nav_until == 1102u);
    clean_dcf_slot(&s, 104u, 1u);
    assert(s.slot_us == 9u && s.ifs_us == 342u && s.cw == 31u && s.nav_until == 1102u);
    clean_dcf_good_rx(&s);
    clean_dcf_slot(&s, 105u, 0u);
    assert(s.ifs_us == 50u && s.nav_valid);
}

static void edca(void) {
    uint8_t bytes[16] = {
        3u,0xa4u,0u,0u, 0x27u,0xa4u,0u,0u,
        0x42u,0x43u,94u,0u, 0x62u,0x32u,47u,0u
    };
    clean_edca_ac_t ac[4];
    assert(clean_edca_decode(ac, bytes) && clean_edca_valid(ac));
    clean_edca_t s = {0};
    assert(clean_edca_configure(&s, ac, 0u, 1u, 1u));
    static const uint8_t mapping[8] = {0u,1u,1u,0u,2u,2u,3u,3u};
    static const uint16_t cw[] = {15u,15u,7u,3u};
    static const uint16_t maximum[] = {1023u,1023u,15u,7u};
    for (uint32_t tid = 0u; tid < 8u; ++tid) {
        assert(clean_edca_select(&s, tid, 0u) == mapping[tid]);
        clean_dcf_t *d = &s.ac[s.selected];
        assert(d->cw_min == cw[s.selected] && d->cw_max == maximum[s.selected]);
        d->slots = 2u;
        uint32_t aifs = 10u + ac[s.selected].aifsn * 9u;
        for (uint32_t now = 0u; now < aifs + 18u; ++now)
            assert(!clean_dcf_poll(d, now, 0));
        assert(clean_dcf_poll(d, aifs + 18u, 0));
        for (uint32_t retry = 0u; retry < 12u; ++retry) {
            clean_dcf_complete(d, 1000u + retry, 0);
            assert(d->cw <= d->cw_max && d->slots <= d->cw);
        }
        assert(d->cw == maximum[s.selected]);
        clean_dcf_complete(d, 2000u, 1);
        assert(d->cw == cw[s.selected]);
        clean_dcf_bad_rx(d, 2000u);
        assert(d->ifs_us == aifs + 314u);
        clean_dcf_slot(d, 2001u, 0u);
        assert(d->ifs_us == 10u + ac[s.selected].aifsn * 20u + 314u);
        clean_dcf_good_rx(d);
        clean_dcf_slot(d, 2002u, 1u);
        uint32_t limit = ac[s.selected].txop_32us * 32u;
        assert(clean_edca_txop_valid(&s, limit ? limit : 100000u));
        assert(clean_edca_txop_valid(&s, limit + 1u) == !limit);
    }
    clean_edca_t before = s;
    assert(clean_edca_select(&s, 8u, 0u) < 0 && !memcmp(&before, &s, sizeof(s)));
    assert(clean_edca_configure(&s, ac, 0u, 1u, 1u));
    before = s;
    clean_dcf_complete(&s.ac[0], 1u, 0);
    for (uint32_t i = 1u; i < 4u; ++i)
        assert(!memcmp(&s.ac[i], &before.ac[i], sizeof(s.ac[i])));
    s.ac[0].slots = 3u;
    clean_dcf_nav(&s.ac[0], 2u, 100u);
    clean_dcf_bad_rx(&s.ac[0], 2u);
    clean_dcf_t saved = s.ac[0];
    assert(clean_edca_select(&s, 6u, 3u) == 3);
    assert(!memcmp(&s.ac[0], &saved, sizeof(saved)));
    assert(clean_edca_select(&s, 0u, 4u) == 0);
    assert(s.ac[0].cw == saved.cw && s.ac[0].slots == saved.slots &&
           s.ac[0].random == saved.random && s.ac[0].ifs_us == saved.ifs_us &&
           s.ac[0].nav_until == saved.nav_until && s.ac[0].nav_valid);
    ac[3].acm = 1u;
    assert(clean_edca_configure(&s, ac, 0u, 1u, 1u));
    assert(clean_edca_select(&s, 6u, 0u) == 2);
    ac[2].acm = ac[0].acm = 1u;
    assert(clean_edca_configure(&s, ac, 0u, 1u, 1u));
    assert(clean_edca_select(&s, 6u, 0u) == 1);
    ac[1].acm = 1u;
    assert(clean_edca_configure(&s, ac, 0u, 1u, 1u));
    before = s;
    assert(clean_edca_select(&s, 6u, 0u) < 0 && !memcmp(&before, &s, sizeof(s)));
    ac[0].ecw_max = 11u;
    assert(!clean_edca_configure(&s, ac, 0u, 1u, 1u));
    assert(!memcmp(&before, &s, sizeof(s)));
    for (uint32_t fault = 0u; fault < 3u; ++fault) {
        uint8_t saved = bytes[0];
        bytes[0] = fault == 0u ? 0x83u : fault == 1u ? 1u : 0x23u;
        clean_edca_ac_t original[4];
        memcpy(original, ac, sizeof(ac));
        assert(!clean_edca_decode(ac, bytes) && !memcmp(original, ac, sizeof(ac)));
        bytes[0] = saved;
    }
    puts("EDCA: AP record decoding, all TIDs/ACs, AIFS, CW limits, ACM downgrade and TXOP bounds pass; NOT RF");
}

static void retries_and_nav(void) {
    uint8_t m[CLEAN_MPDU_BYTES], before[CLEAN_MPDU_BYTES], own[6] = {2u};
    assert(clean_mac_fcs((const uint8_t *)"123456789", 9u) == 0xcbf43926u);
    uint32_t n = frame(m, 4095u, 7u, 0x41u, CLEAN_MSDU_BYTES);
    memcpy(before, m, n);
    for (uint32_t attempt = 0u; attempt < 8u; ++attempt) {
        assert(clean_mac_retry(m, n) && clean_mac_valid(m, n));
        for (uint32_t i = 0u; i < n - 4u; ++i)
            assert(m[i] == (i == 1u ? (uint8_t)(before[i] | 8u) : before[i]));
    }
    m[n - 1u] ^= 1u;
    memcpy(before, m, n);
    assert(!clean_mac_retry(m, n) && !memcmp(m, before, n));
    n = frame(m, 1u, 0u, 0u, 0u);
    m[4] = 4u; m[2] = 100u; fcs(m, n);
    assert(clean_mac_nav_duration(m, n, own) == 100);
    m[4] = own[0]; fcs(m, n);
    assert(clean_mac_nav_duration(m, n, own) == 0);
    m[4] = 4u; m[3] = 128u; fcs(m, n);
    assert(clean_mac_nav_duration(m, n, own) == 0);
    for (uint32_t type = 0u; type < 256u; ++type) {
        memset(m, 0, 32u);
        m[0] = (uint8_t)type; m[2] = 100u; m[4] = 4u;
        n = type == 0xb4u ? 20u : 14u;
        fcs(m, n);
        int duration = clean_mac_nav_duration(m, n, own);
        if (type == 0xb4u || type == 0xc4u || type == 0xd4u) assert(duration == 100);
        else assert(duration <= 0);
    }
}

static void receive(void) {
    clean_mac_rx_t s;
    uint8_t m[CLEAN_MPDU_BYTES], output[24u + CLEAN_MSDU_BYTES + 4u];
    clean_mac_rx_init(&s);
    uint32_t n = frame(m, 4095u, 0u, 0u, 8u);
    assert(clean_mac_rx(&s, m, n, 0u) == CLEAN_MAC_DELIVER);
    assert(clean_mac_retry(m, n));
    assert(clean_mac_rx(&s, m, n, 1u) == CLEAN_MAC_DUPLICATE);
    n = frame(m, 0u, 0u, 8u, 8u);
    assert(clean_mac_rx(&s, m, n, 2u) == CLEAN_MAC_DELIVER);
    n = frame(m, 4095u, 0u, 8u, 8u);
    assert(clean_mac_rx(&s, m, n, 3u) == CLEAN_MAC_DUPLICATE);
    assert(clean_mac_rx(&s, m, n, CLEAN_MAC_RX_LIFETIME_US) == CLEAN_MAC_DUPLICATE);
    assert(clean_mac_rx(&s, m, n, 2000000u) == CLEAN_MAC_DUPLICATE);
    assert(clean_mac_rx(&s, m, n, UINT32_MAX - 1u) == CLEAN_MAC_DUPLICATE);
    assert(clean_mac_rx(&s, m, n, 1u) == CLEAN_MAC_DUPLICATE);
    n = frame(m, 4095u, 0u, 0u, 8u);
    assert(clean_mac_rx(&s, m, n, 2u) == CLEAN_MAC_DELIVER);
    assert(clean_mac_retry(m, n));
    assert(clean_mac_rx(&s, m, n, 3u) == CLEAN_MAC_DUPLICATE);
    m[0] = 0x10u; fcs(m, n);
    assert(clean_mac_rx(&s, m, n, CLEAN_MAC_RX_LIFETIME_US + 1u) == CLEAN_MAC_DELIVER);
    assert(clean_mac_rx(&s, m, n, CLEAN_MAC_RX_LIFETIME_US + 2u) == CLEAN_MAC_DUPLICATE);
    n = frame(m, 5u, 0u, 4u, 1000u);
    assert(clean_mac_rx(&s, m, n, 1000000u) == CLEAN_MAC_FRAGMENT);
    memcpy(output, m, 1024u);
    assert(clean_mac_retry(m, n));
    assert(clean_mac_rx(&s, m, n, 1000001u) == CLEAN_MAC_DUPLICATE);
    n = frame(m, 6u, 0u, 4u, 10u);
    assert(clean_mac_rx(&s, m, n, 1000002u) == CLEAN_MAC_FRAGMENT_BUSY);
    n = frame(m, 6u, 0u, 0u, 10u);
    assert(clean_mac_rx(&s, m, n, 1000003u) == CLEAN_MAC_DELIVER);
    n = frame(m, 5u, 2u, 0u, 10u);
    assert(clean_mac_rx(&s, m, n, 1000004u) == CLEAN_MAC_FRAGMENT_ORDER);
    n = frame(m, 5u, 1u, 4u, 1000u);
    m[16] ^= 2u; fcs(m, n);
    assert(clean_mac_rx(&s, m, n, 1000005u) == CLEAN_MAC_FRAGMENT_ORDER);
    m[16] ^= 2u; fcs(m, n);
    assert(clean_mac_rx(&s, m, n, 1000006u) == CLEAN_MAC_FRAGMENT);
    memcpy(output + 1024u, m + 24u, 1000u);
    assert(clean_mac_retry(m, n));
    assert(clean_mac_rx(&s, m, n, 1000007u) == CLEAN_MAC_DUPLICATE);
    n = frame(m, 5u, 2u, 0u, 304u);
    memcpy(output + 2024u, m + 24u, 304u);
    assert(clean_mac_rx(&s, m, n, 1000008u) == CLEAN_MAC_DELIVER);
    assert(s.length == sizeof(output) && clean_mac_valid(s.frame, s.length));
    output[1] &= (uint8_t)~12u;
    assert(!memcmp(output, s.frame, sizeof(output) - 4u));
    assert(clean_mac_retry(m, n));
    assert(clean_mac_rx(&s, m, n, 1000009u) == CLEAN_MAC_DUPLICATE);
    n = frame(m, 7u, 0u, 4u, 10u);
    assert(clean_mac_rx(&s, m, n, 2000000u) == CLEAN_MAC_FRAGMENT);
    n = frame(m, 7u, 1u, 0u, 10u);
    assert(clean_mac_rx(&s, m, n, 2000000u + CLEAN_MAC_RX_LIFETIME_US) == CLEAN_MAC_FRAGMENT_ORDER);
    assert(s.expired == 1u);
    n = frame(m, 7u, 0u, 12u, 10u);
    assert(clean_mac_rx(&s, m, n, 2000001u + CLEAN_MAC_RX_LIFETIME_US) == CLEAN_MAC_FRAGMENT);
    n = frame(m, 7u, 1u, 8u, 10u);
    assert(clean_mac_rx(&s, m, n, 2000002u + CLEAN_MAC_RX_LIFETIME_US) == CLEAN_MAC_DELIVER);
    assert(clean_mac_rx(&s, m, n, 5000000u) == CLEAN_MAC_DUPLICATE);
    n = frame(m, 8u, 0u, 4u, CLEAN_MSDU_BYTES);
    assert(clean_mac_rx(&s, m, n, 3000000u) == CLEAN_MAC_FRAGMENT);
    n = frame(m, 8u, 1u, 0u, 1u);
    assert(clean_mac_rx(&s, m, n, 3000001u) == CLEAN_MAC_INVALID);
    n = frame(m, 8u, 0u, 0u, 10u);
    assert(clean_mac_rx(&s, m, n, 3000002u) == CLEAN_MAC_DELIVER);
    assert(clean_mac_retry(m, n));
    assert(clean_mac_rx(&s, m, n, 3000002u + CLEAN_MAC_RX_LIFETIME_US) == CLEAN_MAC_DUPLICATE);
    clean_mac_rx_init(&s);
    n = frame(m, 9u, 0u, 64u, 32u);
    assert(clean_mac_rx(&s, m, n, 0u) == CLEAN_MAC_PROTECTED);
    assert(clean_mac_retry(m, n));
    assert(clean_mac_rx(&s, m, n, 1u) == CLEAN_MAC_PROTECTED);
    m[1] &= (uint8_t)~64u; m[0] = 0x88u; fcs(m, n);
    assert(clean_mac_rx(&s, m, n, 2u) == CLEAN_MAC_UNSUPPORTED);
    n = frame(m, 10u, 15u, 4u, 1u);
    assert(clean_mac_rx(&s, m, n, 3u) == CLEAN_MAC_INVALID);
    for (uint32_t size = 0u; size <= CLEAN_MPDU_BYTES; ++size) {
        memset(m, 0, sizeof(m));
        m[0] = 8u;
        m[4] = 1u;
        if (size >= 4u) fcs(m, size);
        int result = clean_mac_rx(&s, m, size, 4u);
        assert(result == CLEAN_MAC_INVALID || result == CLEAN_MAC_UNSUPPORTED);
    }
}

static void sequence_soak(void) {
    clean_mac_rx_t a, b;
    uint8_t m[CLEAN_MPDU_BYTES];
    clean_mac_rx_init(&a); clean_mac_rx_init(&b);
    for (uint32_t i = 0u; i < 100000u; ++i) {
        static const uint32_t payloads[3] = {32u,1500u,CLEAN_MSDU_BYTES};
        uint32_t n = frame(m, (uint16_t)(i & 4095u), 0u, 0u, payloads[i % 3u]);
        assert(clean_mac_rx(&a, m, n, i) == CLEAN_MAC_DELIVER);
        assert(clean_mac_rx(&b, m, n, i) == CLEAN_MAC_DELIVER);
        assert(clean_mac_retry(m, n));
        assert(clean_mac_rx(&a, m, n, i) == CLEAN_MAC_DUPLICATE);
        assert(clean_mac_rx(&b, m, n, i) == CLEAN_MAC_DUPLICATE);
    }
    assert(a.delivered == 100000u && b.delivered == 100000u);
    assert(a.duplicates == 100000u && b.duplicates == 100000u);
}

static void station_transmit(void) {
    uint8_t tx[CLEAN_TX_STORAGE_BYTES], before[CLEAN_TX_STORAGE_BYTES];
    for (uint32_t n = 14u; n <= CLEAN_MPDU_BYTES; ++n) {
        clean_cck11_plcp(tx, n);
        assert(clean_cck11_valid(tx, n));
        for (uint32_t bit = 0u; bit < 48u; ++bit) {
            tx[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            assert(!clean_cck11_valid(tx, n));
            tx[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        }
    }
    for (uint32_t rate = 0u; rate < 12u; ++rate) {
        assert(clean_ack_airtime_us(0xfffu, rate) ==
            (rate >= 4u ? clean_ofdm_airtime_us(14u, rate - 4u) :
             (rate == 0u ? 304u : rate == 1u ? 248u : rate == 2u ? 213u : 203u)));
    }
    assert(!clean_ack_airtime_us(0u, 0u));
    for (uint32_t rate = 0u; rate < 12u; ++rate) {
        assert(clean_ack_timeout_us(rate, 9u) == (rate < 4u ? 387u : 133u));
        assert(clean_ack_timeout_us(rate, 20u) == (rate < 4u ? 398u : 144u));
    }
    assert(!clean_ack_timeout_us(12u, 9u) && !clean_ack_timeout_us(3u, 0u));
    clean_station_ap_t ap = {0};
    ap.rates = ap.basic_rates = 0xfffu;
    uint32_t n = 6u + frame(tx + 6u, 123u, 0u, 65u, 64u);
    assert(clean_ofdm_plcp_rate(tx, n - 6u, 0u));
    uint32_t cmd = 5u | clean_tx_length_bits(n) | (1u << 18u) | CLEAN_TX_ACK_REQUIRED;
    assert(clean_station_tx_prepare(tx, n, &ap, &cmd));
    assert((cmd & (1u << 18u)) && clean_station_u16(tx + 8u) == 60u);
    memcpy(before, tx, n);
    ap.erp = 2u;
    assert(clean_station_tx_prepare(tx, n, &ap, &cmd));
    assert(!(cmd & (1u << 18u)) && clean_cck11_valid(tx, n - 6u));
    assert(clean_station_u16(tx + 8u) == 213u);
    assert(clean_mac_valid(tx + 6u, n - 6u));
    for (uint32_t i = 6u; i < n - 4u; ++i)
        if (i != 8u && i != 9u) assert(tx[i] == before[i]);
    assert(clean_mac_retry(tx + 6u, n - 6u));
    memcpy(before, tx, n);
    assert(clean_station_tx_prepare(tx, n, &ap, &cmd));
    assert(!memcmp(before, tx, n));
    ap.erp = 0u;
    assert(clean_station_tx_prepare(tx, n, &ap, &cmd));
    assert((cmd & (1u << 18u)) && clean_ofdm_plcp_valid(tx, n - 6u));
    for (uint32_t i = 6u; i < n - 4u; ++i)
        if (i != 8u && i != 9u) assert(tx[i] == before[i]);
    ap.erp = 2u;
    ap.rates &= (uint16_t)~8u;
    assert(!clean_station_tx_prepare(tx, n, &ap, &cmd));
}

static void station_delivery(void) {
    clean_mac_rx_t s;
    clean_mac_rx_init(&s);
    uint8_t packet[CLEAN_RX_PACKET_BYTES] = {0}, detail[2048] = {0};
    const uint8_t own[6] = {2u}, peer[6] = {4u};
    uint32_t size = 18u + frame(packet + 18u, 1u, 0u, 0u, 32u);
    assert(clean_station_rx(&s, packet, &size, own, peer, 0u) == CLEAN_MAC_DELIVER);
    assert(size == 78u);
    assert(clean_mac_retry(packet + 18u, size - 18u));
    assert(clean_station_rx(&s, packet, &size, own, peer, 1u) == CLEAN_MAC_DUPLICATE);
    assert(size == 0u);
    for (uint32_t frag = 0u; frag < 16u; ++frag) {
        size = 18u + frame(packet + 18u, 2u, (uint8_t)frag,
                           frag == 15u ? 0u : 4u, 144u);
        int result = clean_station_rx(&s, packet, &size, own, peer, 2u + frag);
        assert(result == (frag == 15u ? CLEAN_MAC_DELIVER : CLEAN_MAC_FRAGMENT));
        if (frag != 15u) assert(size == 0u);
    }
    assert(size == 18u + 28u + CLEAN_MSDU_BYTES);
    assert(packet[17] == CLEAN_RX_REASSEMBLED);
    assert(clean_mac_valid(packet + 18u, size - 18u));
    assert(badger_bytes_load_u32(packet) == (size | ((size ^ 0xffffu) << 16u)));
    size = 18u + frame(packet + 18u, 3u, 0u, 64u, 32u);
    assert(clean_station_rx(&s, packet, &size, own, peer, 20u) == CLEAN_MAC_PROTECTED);
    assert(size == 78u);
    packet[18u + 4u] = 8u;
    assert(clean_station_rx(&s, packet, &size, own, peer, 21u) == CLEAN_MAC_IGNORED);
    clean_station_rx_report(&s, detail);
    assert(badger_bytes_load_u32(detail + CLEAN_STATION_MAC_DETAIL) == CLEAN_STATION_MAC_MAGIC);
    assert(badger_bytes_load_u32(detail + CLEAN_STATION_MAC_DETAIL + 4u) == 2u);
    assert(badger_bytes_load_u32(detail + CLEAN_STATION_MAC_DETAIL + 8u) == 1u);
    clean_station_rx_report_at(&s, detail, CLEAN_STATION_PROTECTED_DETAIL);
    assert(badger_bytes_load_u32(detail + CLEAN_STATION_PROTECTED_DETAIL + 4u) == 2u);
    clean_mac_rx_init(&s);
    uint32_t n = frame(packet, 12u, 0u, 0u, 32u);
    assert(clean_mac_rx(&s, packet, n, 0u) == CLEAN_MAC_DELIVER);
    assert(clean_mac_rx_authenticated(&s, packet, n, 1u) == CLEAN_MAC_INVALID);
    packet[1] |= 72u; fcs(packet, n);
    assert(clean_mac_rx(&s, packet, n, 2u) == CLEAN_MAC_PROTECTED);
    assert(clean_mac_rx_authenticated(&s, packet, n, 2u) == CLEAN_MAC_DELIVER);
    assert(clean_mac_rx_authenticated(&s, packet, n, 3u) == CLEAN_MAC_DUPLICATE);
    n = frame(packet, 13u, 0u, 2u, 0u);
    packet[0] = 0x48u; fcs(packet, n);
    assert(clean_mac_rx(&s, packet, n, 4u) == CLEAN_MAC_DELIVER);
    assert(clean_mac_retry(packet, n));
    assert(clean_mac_rx(&s, packet, n, 5u) == CLEAN_MAC_DUPLICATE);
    packet[1] |= 4u; fcs(packet, n);
    assert(clean_mac_rx(&s, packet, n, 6u) == CLEAN_MAC_INVALID);
}

static void soak_protocol(void) {
    uint8_t nonce[14] = {1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,14u};
    nonce[13] = 0u;
    uint8_t packet[CLEAN_TX_STORAGE_BYTES];
    clean_soak_status_t s = {.target = CLEAN_SOAK_MAX_FRAMES};
    for (uint32_t wrap = 0u; wrap < 2u; ++wrap) {
        uint32_t at = wrap ? 0xfffffff0u : 123u;
        assert(clean_soak_close_pending(at, at));
        assert(clean_soak_close_pending(at, at + CLEAN_SOAK_RETRY_US));
        assert(clean_soak_close_pending(at, at + CLEAN_SOAK_CLOSE_US - 1u));
        assert(!clean_soak_close_pending(at, at + CLEAN_SOAK_CLOSE_US));
    }
    s.elapsed_us = 0xffffffffu; s.elapsed_hi = 1u;
    assert(clean_soak_elapsed(&s) == 8589934591ULL);
    s.elapsed_us = (uint32_t)CLEAN_SOAK_MAX_US;
    s.elapsed_hi = (uint32_t)(CLEAN_SOAK_MAX_US >> 32u);
    assert(clean_soak_elapsed(&s) == CLEAN_SOAK_MAX_US);
    assert((uint64_t)CLEAN_SOAK_HOST_SECONDS * 1000000u >
           CLEAN_SOAK_MAX_US + 5000000u);
    assert((uint64_t)CLEAN_SOAK_POLLS * CLEAN_SOAK_POLL_US >
           CLEAN_SOAK_MAX_US + 5000000u);
    for (uint32_t profile = 0u; profile <= 4u; ++profile)
        for (uint32_t drop = 0u; drop <= CLEAN_TEST_ACK_MAX; ++drop)
            assert(clean_test_ack_count(9u | (drop << 8u), profile) ==
                   (profile == 3u ? (int)drop : -1));
    assert(clean_test_ack_count(9u | ((CLEAN_TEST_ACK_MAX + 1u) << 8u), 3u) == -1);
    assert(clean_test_ack_count(9u | 0x80000000u, 3u) == -1);
    assert(clean_test_ack_count(8u, 3u) == -1);
    for (uint32_t i = 0u; i < CLEAN_SOAK_MAX_FRAMES; ++i) {
        uint32_t n = clean_soak_frame(packet, nonce, 1u, i);
        assert(n == clean_soak_size(i) + 34u && n <= CLEAN_TX_BYTES);
        assert(clean_soak_receive(&s, packet + 6u, n - 6u, nonce, 2u) == 1);
    }
    assert(s.received == CLEAN_SOAK_MAX_FRAMES && !s.corrupt && !s.duplicates);
    uint32_t n = clean_soak_frame(packet, nonce, 1u, CLEAN_SOAK_MAX_FRAMES - 1u);
    assert(clean_soak_receive(&s, packet + 6u, n - 6u, nonce, 2u) == -1 && s.duplicates == 1u);
    s.received = 0u; s.failure = 0u;
    n = clean_soak_frame(packet, nonce, 1u, 1u);
    assert(clean_soak_receive(&s, packet + 6u, n - 6u, nonce, 2u) == -1 && s.out_of_order == 1u);
    n = clean_soak_frame(packet, nonce, 1u, 0u);
    packet[40] ^= 1u;
    fcs(packet + 6u, n - 6u);
    assert(clean_soak_receive(&s, packet + 6u, n - 6u, nonce, 2u) == -1 && s.corrupt == 1u);
    n = clean_soak_frame(packet, nonce, 2u, 0u);
    assert(clean_soak_receive(&s, packet + 6u, n - 6u, nonce, 2u) == 0);
    for (uint32_t length = 0u; length < 60u; ++length)
        assert(clean_soak_receive(&s, packet + 6u, length, nonce, 2u) == 0);
    puts("MAC: continuous-fixture sequence/wrap, all payload sizes, corruption, duplicates and ordering OK; NOT OTA");
}

static void soak_fragment_protocol(void) {
    uint8_t nonce[14] = {1u,2u,3u,4u,5u,6u,7u,8u};
    uint8_t tx[CLEAN_TX_STORAGE_BYTES], packet[CLEAN_RX_PACKET_BYTES], expected[CLEAN_TX_STORAGE_BYTES];
    const uint32_t indices[] = {0u, 4095u, 4096u, 99999u};
    for (uint32_t profile = 5u; profile < CLEAN_SOAK_PAYLOADS; ++profile)
        for (uint32_t phy = 0u; phy < CLEAN_SOAK_PHYS; ++phy)
            for (uint32_t role = 1u; role <= 2u; ++role)
                for (uint32_t test = 0u; test < sizeof(indices) / sizeof(indices[0]); ++test) {
                    nonce[13] = (uint8_t)((profile << 3u) | phy);
                    uint32_t count = clean_soak_fragments(nonce), index = indices[test];
                    assert(count == (profile == 5u ? 2u : 16u));
                    assert(!clean_soak_fragment_frame(tx, nonce, (uint8_t)role, index, count));
                    clean_mac_rx_t rx;
                    clean_mac_rx_init(&rx);
                    clean_soak_status_t s = {.target = CLEAN_SOAK_MAX_FRAMES, .received = index};
                    uint8_t own[6], peer[6];
                    clean_soak_address(own, nonce, (uint8_t)(3u - role));
                    clean_soak_address(peer, nonce, (uint8_t)role);
                    uint32_t size = 0u, tick = UINT32_MAX - 100u;
                    for (uint32_t f = 0u; f < count; ++f) {
                        uint32_t length = clean_soak_fragment_frame(tx, nonce, (uint8_t)role, index, f);
                        assert(length == 34u + CLEAN_MSDU_BYTES / count);
                        assert(clean_mac_valid(tx + 6u, length - 6u));
                        assert(clean_station_tx_tid(tx + 6u, length - 6u, 0u) == 0);
                        assert((tx[28] & 15u) == f);
                        assert(!!(tx[7] & 4u) == (f + 1u < count));
                        uint32_t rate = clean_soak_rate(nonce);
                        uint32_t ack = rate == 4u ? clean_ofdm_airtime_us(14u, 0u) :
                            clean_cck_airtime_us(14u, rate, clean_soak_short(nonce));
                        uint32_t airtime = rate == 4u ? clean_ofdm_airtime_us(length - 6u, 0u) :
                            clean_cck_airtime_us(length - 6u, rate, clean_soak_short(nonce));
                        uint32_t duration = tx[8] | ((uint32_t)tx[9] << 8u);
                        assert(duration == 10u + ack +
                            (f + 1u < count ? 20u + ack + airtime : 0u));
                        memset(packet, 0, 18u);
                        packet[15] = rate == 4u ? clean_ofdm_rates[0].signal :
                            clean_cck_rates[rate].signal;
                        packet[16] = 1u | (rate == 4u ? CLEAN_RX_OFDM :
                            clean_soak_short(nonce) ? CLEAN_RX_SHORT_PREAMBLE : 0u);
                        memcpy(packet + 18u, tx + 6u, length - 6u);
                        size = length + 12u;
                        int mac = clean_station_rx(&rx, packet, &size, own, peer, tick++);
                        assert(mac == (f + 1u < count ? CLEAN_MAC_FRAGMENT : CLEAN_MAC_DELIVER));
                        if (f + 1u < count) assert(!size);
                        else {
                            assert(size == 18u + 28u + CLEAN_MSDU_BYTES && packet[17] == 1u);
                            assert(clean_soak_receive_packet(&s, packet, size, nonce,
                                                            (uint8_t)(3u - role)) == 1);
                            uint32_t full = clean_soak_frame(expected, nonce, (uint8_t)role, index);
                            uint32_t first = clean_soak_fragment_frame(tx, nonce, (uint8_t)role, index, 0u);
                            assert(first);
                            expected[8] = tx[8]; expected[9] = tx[9];
                            fcs(expected + 6u, full - 6u);
                            assert(!memcmp(packet + 18u, expected + 6u, full - 6u));
                            /* Reconstructed transport must not masquerade as raw OTA evidence. */
                            --s.received; packet[17] = 0u;
                            assert(clean_soak_receive_packet(&s, packet, size, nonce,
                                                            (uint8_t)(3u - role)) == -1);
                            ++s.received;
                            length = clean_soak_fragment_frame(tx, nonce, (uint8_t)role, index, f);
                        }
                        assert(clean_mac_retry(tx + 6u, length - 6u));
                        memcpy(packet + 18u, tx + 6u, length - 6u);
                        size = length + 12u;
                        assert(clean_station_rx(&rx, packet, &size, own, peer, tick++) == CLEAN_MAC_DUPLICATE);
                        assert(!size);
                    }
                    assert(rx.delivered == 1u && rx.fragments == count - 1u &&
                           rx.duplicates == count && !rx.expired && !rx.fragment_errors);
                }
    puts("MAC fragment fixture: 2/16 full-MSDU OTA templates, all PHY/preamble profiles, both roles, "
         "sequence/timer wrap, exact reassembly and every-fragment retries pass; NOT OTA");
    uint8_t m[28u + CLEAN_MSDU_BYTES + 17u] = {0};
    m[0] = 8u; m[1] = 5u;
    for (uint32_t fragment = 0u; fragment < 16u; ++fragment) {
        m[22] = (uint8_t)fragment;
        for (uint32_t protected_frame = 0u; protected_frame < 2u; ++protected_frame) {
            m[1] = (uint8_t)(1u | (protected_frame ? 64u : 0u));
            uint32_t overhead = protected_frame * 16u;
            if (fragment) {
                assert(clean_station_tx_tid(m, 28u + overhead, 0u) == -1);
                assert(clean_station_tx_tid(m, 29u + overhead, 0u) == 0);
                assert(clean_station_tx_tid(m, 28u + overhead + CLEAN_MSDU_BYTES, 0u) == 0);
                assert(clean_station_tx_tid(m, 29u + overhead + CLEAN_MSDU_BYTES, 0u) == -1);
            }
            m[1] |= 4u;
            assert(clean_station_tx_tid(m, 29u + overhead, 0u) == (fragment == 15u ? -1 : 0));
            for (uint32_t flag = 0u; flag < 3u; ++flag) {
                uint8_t saved = m[1];
                m[1] = (uint8_t)(saved ^ (flag == 0u ? 1u : flag == 1u ? 2u : 128u));
                assert(clean_station_tx_tid(m, 29u + overhead, 0u) == -1);
                m[1] = saved;
            }
        }
    }
    for (uint32_t fc = 0u; fc < 256u; ++fc) if (fc != 8u) {
        m[0] = (uint8_t)fc; m[1] = 5u; m[22] = 0u;
        assert(clean_station_tx_tid(m, 32u, 1u) == -1);
    }
}

static uint32_t qos_frame(uint8_t *m, uint16_t seq, uint8_t frag, uint8_t flags,
                           uint8_t tid, uint32_t payload) {
    uint32_t n = frame(m, seq, frag, flags, payload) + 2u;
    memmove(m + 26u, m + 24u, payload);
    m[0] = 0x88u; m[24] = tid; m[25] = 0u;
    fcs(m, n);
    return n;
}

static void qos_receive(void) {
    clean_qos_rx_t s;
    uint8_t m[CLEAN_MPDU_BYTES], detail[2048] = {0};
    clean_qos_rx_init(&s);
    for (uint32_t tid = 0u; tid < 16u; ++tid) {
        uint32_t n = qos_frame(m, 4095u, 0u, 4u, (uint8_t)tid, 16u);
        assert(clean_qos_rx(&s, m, n, 0u) == CLEAN_MAC_FRAGMENT);
        assert(clean_mac_retry(m, n));
        assert(clean_qos_rx(&s, m, n, 1u) == CLEAN_MAC_DUPLICATE);
    }
    for (uint32_t tid = 0u; tid < 16u; ++tid) {
        uint32_t n = qos_frame(m, 4095u, 1u, 0u, (uint8_t)tid, 16u);
        assert(clean_qos_rx(&s, m, n, 2u) == CLEAN_MAC_DELIVER);
        const clean_mac_rx_t *r = &s.tid[tid];
        assert(r->length == 62u && clean_mac_valid(r->frame, r->length));
        assert(r->frame[24] == tid && !(r->frame[22] & 15u) && !(r->frame[1] & 12u));
        for (uint32_t i = 0u; i < 32u; ++i)
            assert(r->frame[26u+i] == (uint8_t)(4095u + i/16u + i%16u));
        assert(clean_mac_retry(m, n));
        assert(clean_qos_rx(&s, m, n, 3u) == CLEAN_MAC_DUPLICATE);
        n = qos_frame(m, 0u, 0u, 8u, (uint8_t)tid, CLEAN_MSDU_BYTES);
        assert(clean_qos_rx(&s, m, n, 4u) == CLEAN_MAC_DELIVER);
        assert(clean_qos_rx(&s, m, n, 5u) == CLEAN_MAC_DUPLICATE);
    }
    clean_mac_rx_t legacy;
    clean_mac_rx_init(&legacy);
    clean_station_qos_report_at(&legacy, &s, detail, CLEAN_STATION_PROTECTED_DETAIL);
    assert(badger_bytes_load_u32(detail + CLEAN_STATION_PROTECTED_DETAIL + 4u) == 32u);
    assert(badger_bytes_load_u32(detail + CLEAN_STATION_PROTECTED_DETAIL + 8u) == 48u);
    assert(badger_bytes_load_u32(detail + CLEAN_STATION_PROTECTED_DETAIL + 12u) == 16u);
    for (uint32_t fault = 0u; fault < 8u; ++fault) {
        uint32_t n = qos_frame(m, 1u, 0u, 0u, 0u, 1u);
        if (fault < 2u) m[24] = (uint8_t)(0x40u + fault*0x20u);
        if (fault == 2u) m[1] = 3u;
        if (fault == 3u) m[1] = 128u;
        if (fault == 4u) m[0] = 0xc8u;
        if (fault == 5u) m[24] = 128u;
        if (fault == 6u) m[1] = 4u, m[22] |= 15u;
        fcs(m, n);
        if (fault == 7u) m[n - 1u] ^= 1u;
        assert(clean_qos_rx(&s, m, n, 6u) < 0);
        assert(s.tid[0].delivered == 2u);
    }
    uint32_t n = qos_frame(m, 2u, 0u, 0u, 0x20u, 0u);
    m[0] = 0xc8u; fcs(m, n);
    assert(clean_qos_rx(&s, m, n, 7u) == CLEAN_MAC_DELIVER);
    for (uint32_t size = 0u; size < 30u; ++size)
        assert(clean_qos_rx(&s, m, size, 8u) == CLEAN_MAC_INVALID);
    clean_qos_rx_init(&s);
    uint32_t start = UINT32_MAX - 10u;
    for (uint32_t tid = 0u; tid < 16u; ++tid) {
        n = qos_frame(m, 10u, 0u, 4u, (uint8_t)tid, 32u);
        assert(clean_qos_rx(&s, m, n, start) == CLEAN_MAC_FRAGMENT);
    }
    clean_qos_rx_expire(&s, start + CLEAN_MAC_RX_LIFETIME_US);
    for (uint32_t tid = 0u; tid < 16u; ++tid) {
        assert(!s.tid[tid].active && !s.tid[tid].length && s.tid[tid].expired == 1u);
        for (uint32_t i = 0u; i < sizeof(s.tid[tid].frame); ++i) assert(!s.tid[tid].frame[i]);
        n = qos_frame(m, 10u, 1u, 0u, (uint8_t)tid, 32u);
        assert(clean_qos_rx(&s, m, n, start + CLEAN_MAC_RX_LIFETIME_US + 1u) ==
               CLEAN_MAC_FRAGMENT_ORDER);
    }
    clean_qos_rx_init(&s);
    n = qos_frame(m, 1u, 0u, 0x44u, 3u, 1u);
    assert(clean_qos_rx(&s, m, n, 0u) == CLEAN_MAC_PROTECTED && !s.tid[3].active);
    assert(clean_qos_rx_authenticated(&s, m, n, 0u) == CLEAN_MAC_FRAGMENT);
    n = qos_frame(m, 1u, 1u, 0u, 3u, 1u);
    assert(clean_qos_rx_authenticated(&s, m, n, 1u) == CLEAN_MAC_INVALID);
    assert(clean_qos_rx(&s, m, n, 1u) == CLEAN_MAC_FRAGMENT_ORDER);
    n = qos_frame(m, 1u, 1u, 64u, 3u, 1u);
    m[10] ^= 2u; fcs(m, n);
    assert(clean_qos_rx_authenticated(&s, m, n, 1u) == CLEAN_MAC_FRAGMENT_ORDER);
    n = qos_frame(m, 2u, 0u, 0u, 0u, 17u);
    m[24] |= 128u;
    memset(m + 26u, 0, 17u);
    m[26] = 2u; m[32] = 4u; m[39] = 3u;
    fcs(m, n);
    assert(clean_qos_rx(&s, m, n, 2u) == CLEAN_MAC_DELIVER);
    assert(clean_mac_retry(m, n));
    assert(clean_qos_rx(&s, m, n, 3u) == CLEAN_MAC_DUPLICATE);
    m[39] = 4u; fcs(m, n);
    assert(clean_qos_rx(&s, m, n, 4u) == CLEAN_MAC_INVALID);
    m[39] = 3u; m[1] |= 64u; fcs(m, n);
    assert(clean_qos_rx_authenticated(&s, m, n, 5u) == CLEAN_MAC_DELIVER);
    puts("QoS RX: independent TID sequences/fragments, duplicate/wrap, bounds, policy gates and expiry/wiping pass; NOT EDCA or Block ACK");
}

static void station_qos_delivery(void) {
    clean_mac_rx_t legacy;
    clean_qos_rx_t qos;
    clean_mac_rx_init(&legacy);
    clean_qos_rx_init(&qos);
    uint8_t packet[CLEAN_RX_PACKET_BYTES] = {0};
    const uint8_t own[6] = {2u}, peer[6] = {4u};
    uint32_t size = 18u + qos_frame(packet + 18u, 7u, 0u, 0u, 0u, 32u);
    uint32_t original = size;
    assert(clean_station_qos_rx(&legacy, 0, packet, &size, own, peer, 0u) ==
           CLEAN_MAC_IGNORED && size == original && !qos.tid[0].delivered);
    for (uint32_t tid = 0u; tid < 8u; ++tid) {
        size = 18u + qos_frame(packet + 18u, 4095u, 0u, 6u, (uint8_t)tid, 32u);
        assert(clean_station_qos_rx(&legacy, &qos, packet, &size, own, peer, tid) ==
               CLEAN_MAC_FRAGMENT && !size);
    }
    for (uint32_t tid = 0u; tid < 8u; ++tid) {
        size = 18u + qos_frame(packet + 18u, 4095u, 1u, 2u, (uint8_t)tid, 32u);
        assert(clean_station_qos_rx(&legacy, &qos, packet, &size, own, peer, 20u + tid) ==
               CLEAN_MAC_DELIVER);
        assert(size == 18u + 30u + 64u && packet[17] == CLEAN_RX_REASSEMBLED);
        assert(packet[42] == tid && clean_mac_valid(packet + 18u, size - 18u));
        assert(badger_bytes_load_u32(packet) == (size | ((size ^ 0xffffu) << 16u)));
        size = 18u + qos_frame(packet + 18u, 4095u, 1u, 10u, (uint8_t)tid, 32u);
        assert(clean_station_qos_rx(&legacy, &qos, packet, &size, own, peer, 40u + tid) ==
               CLEAN_MAC_DUPLICATE && !size);
    }
    size = 18u + qos_frame(packet + 18u, 0u, 0u, 0x42u, 0u, 32u);
    original = size;
    assert(clean_station_qos_rx(&legacy, &qos, packet, &size, own, peer, 60u) ==
           CLEAN_MAC_PROTECTED && size == original && qos.tid[0].delivered == 1u);
    packet[22] = 8u;
    assert(clean_station_qos_rx(&legacy, &qos, packet, &size, own, peer, 61u) ==
           CLEAN_MAC_IGNORED && size == original);
    assert(!legacy.delivered);
    puts("Station QoS: negotiated dispatch, interleaved TID reassembly, rewritten F2 length/FCS, duplicates and protected bypass pass");
}

static void station_edca_tx(void) {
    uint8_t tx[CLEAN_TX_BYTES] = {0};
    uint8_t *m = tx + 6u;
    uint32_t n = qos_frame(m, 1u, 0u, 1u, 0u, 32u);
    for (uint32_t tid = 0u; tid < 8u; ++tid) {
        m[24] = (uint8_t)tid;
        assert(clean_station_tx_tid(m, n, 1u) == (int)tid);
        m[1] |= 64u;
        assert(clean_station_tx_tid(m, n, 1u) == (int)tid);
        m[1] &= (uint8_t)~64u;
        assert(clean_station_tx_tid(m, n, 0u) == -1);
    }
    for (uint32_t size = 0u; size < 30u; ++size)
        assert(clean_station_tx_tid(m, size, 1u) == -1);
    const uint8_t bad_control[] = {8u, 16u, 32u, 64u, 128u};
    for (uint32_t i = 0u; i < sizeof(bad_control); ++i) {
        m[24] = bad_control[i];
        assert(clean_station_tx_tid(m, n, 1u) == -1);
    }
    m[24] = 0u;
    m[25] = 1u; assert(clean_station_tx_tid(m, n, 1u) == -1); m[25] = 0u;
    for (uint32_t bit = 0u; bit < 8u; ++bit) {
        if (!(0x87u & (1u << bit))) continue;
        m[1] ^= (uint8_t)(1u << bit);
        assert(clean_station_tx_tid(m, n, 1u) == -1);
        m[1] ^= (uint8_t)(1u << bit);
    }
    m[0] = 0xc8u;
    assert(clean_station_tx_tid(m, 30u, 1u) == 0);
    assert(clean_station_tx_tid(m, n, 1u) == -1);
    m[0] = 0x88u;
    assert(clean_station_tx_tid(m, 30u + CLEAN_MSDU_BYTES, 1u) == 0);
    assert(clean_station_tx_tid(m, 31u + CLEAN_MSDU_BYTES, 1u) == -1);
    m[1] |= 64u;
    assert(clean_station_tx_tid(m, 45u, 1u) == -1);
    assert(clean_station_tx_tid(m, 46u, 1u) == 0);
    assert(clean_station_tx_tid(m, 46u + CLEAN_MSDU_BYTES, 1u) == 0);
    assert(clean_station_tx_tid(m, 47u + CLEAN_MSDU_BYTES, 1u) == -1);
    m[1] &= (uint8_t)~64u;
    m[0] = 8u; assert(clean_station_tx_tid(m, n, 0u) == 0);
    m[0] = 0xd0u; m[1] = 0u; assert(clean_station_tx_tid(m, n, 0u) == 6);
    m[22] |= 1u; assert(clean_station_tx_tid(m, n, 1u) == -1);
    const uint32_t lengths[] = {36u, 1510u, CLEAN_TX_BYTES};
    for (uint32_t i = 0u; i < 3u; ++i) {
        uint32_t length = lengths[i], mpdu = length - 6u;
        for (uint32_t rate = 0u; rate < 8u; ++rate) {
            assert(clean_ofdm_plcp_rate(tx, mpdu, rate));
            assert(clean_legacy_exchange_us(tx, length, 1u, 0u, 0xfffu) ==
                   clean_ofdm_airtime_us(mpdu, rate) + 10u +
                   clean_ofdm_airtime_us(14u, rate));
            assert(!clean_legacy_exchange_us(tx, length, 1u, 1u, 0xfffu));
        }
        for (uint32_t rate = 0u; rate < 4u; ++rate) {
            assert(clean_cck_plcp_rate(tx, mpdu, rate));
            for (uint32_t short_plcp = 0u; short_plcp < 2u; ++short_plcp) {
                uint32_t expected = rate || !short_plcp ?
                    clean_cck_airtime_us(mpdu, rate, short_plcp) + 10u +
                    clean_cck_airtime_us(14u, rate, 0u) : 0u;
                assert(clean_legacy_exchange_us(tx, length, 0u, short_plcp, 0xfffu) == expected);
            }
        }
    }
    assert(!clean_legacy_exchange_us(tx, 0u, 0u, 0u, 0xfffu));
    assert(!clean_legacy_exchange_us(tx, CLEAN_TX_BYTES + 1u, 0u, 0u, 0xfffu));
    assert(!clean_legacy_exchange_us(tx, 36u, 0u, 0u, 0u));
    puts("Station EDCA TX: management/legacy/QoS selection, fail-closed headers and exact all-rate/size/preamble exchange airtimes pass");
}

static void receive_relevance(void) {
    uint8_t m[CLEAN_MPDU_BYTES + 1u] = {0};
    const uint8_t own[6] = {2u,3u,4u,5u,6u,7u};
    const uint8_t peer[6] = {2u,8u,9u,10u,11u,12u};
    memcpy(m + 4u, own, sizeof(own));
    memcpy(m + 10u, peer, sizeof(peer));
    for (uint32_t n = 0u; n < 14u; ++n)
        assert(!clean_mac_rx_relevant(m, n, own, peer));
    for (uint32_t fc = 0u; fc < 256u; ++fc) {
        m[0] = (uint8_t)fc;
        assert(clean_mac_rx_relevant(m, 14u, own, peer));
        assert(clean_mac_rx_relevant(m, CLEAN_MPDU_BYTES, own, peer));
        assert(!clean_mac_rx_relevant(m, CLEAN_MPDU_BYTES + 1u, own, peer));
    }
    m[0] = 0x80u;
    for (uint32_t bit = 0u; bit < 48u; ++bit) {
        m[4u + bit / 8u] ^= (uint8_t)(1u << (bit & 7u));
        assert(!clean_mac_rx_relevant(m, 14u, own, peer));
        assert(clean_mac_rx_relevant(m, 28u, own, peer) == (bit == 0u));
        m[4u + bit / 8u] ^= (uint8_t)(1u << (bit & 7u));
    }
    memset(m + 4u, 255, 6u);
    for (uint32_t n = 14u; n < 28u; ++n)
        assert(!clean_mac_rx_relevant(m, n, own, peer));
    for (uint32_t fc = 0u; fc < 256u; ++fc) {
        m[0] = (uint8_t)fc;
        uint32_t type = (fc >> 2u) & 3u;
        assert(clean_mac_rx_relevant(m, 28u, own, peer) == (type == 0u || type == 2u));
    }
    m[0] = 0x88u;
    for (uint32_t bit = 0u; bit < 48u; ++bit) {
        m[10u + bit / 8u] ^= (uint8_t)(1u << (bit & 7u));
        assert(!clean_mac_rx_relevant(m, 28u, own, peer));
        m[10u + bit / 8u] ^= (uint8_t)(1u << (bit & 7u));
    }
    puts("Station RX selection: own-address diagnostics and pinned-peer group traffic retained; unrelated traffic excluded");
}

static void capture_queue(void) {
    uint8_t detail[2048] = {0};
    clean_mac_trace_status_t trace = {.rx_filtered=123u,.rx_capture_max_us=456u};
    assert(CLEAN_STATION_TRACE_DETAIL + sizeof(trace) <= CLEAN_SOAK_FAULT_DETAIL);
    memcpy(detail + CLEAN_STATION_TRACE_DETAIL, &trace, sizeof(trace));
    memset(detail + CLEAN_SOAK_FAULT_DETAIL, 0xa5, 2u * CLEAN_RX_FAULT_WORDS);
    assert(!memcmp(detail + CLEAN_STATION_TRACE_DETAIL, &trace, sizeof(trace)));
    for (uint32_t start = 0xfffffff0u; start != 16u; ++start) {
        uint32_t slots[8], captured = start, published = start, posted = start, consumed = start;
        uint16_t correlations[8];
        for (uint32_t round = 0u; round < 8u; ++round) {
            for (uint32_t n = 0u; n < 8u; ++n) {
                assert(clean_sdio_credit_available(captured, published, 8u) == 1);
                slots[captured & 7u] = captured;
                correlations[captured & 7u] = (uint16_t)(0x8000u | ((captured % 16u)+1u));
                ++captured;
            }
            assert(!clean_sdio_credit_available(captured, published, 8u));
            assert(clean_sdio_credit_available(captured + 1u, published, 8u) == -1);
            for (uint32_t n = 0u; n < 8u; ++n) {
                assert(slots[published & 7u] == published);
                uint16_t index = (uint16_t)(published+1u);
                assert(correlations[(index-1u)&7u] ==
                    (uint16_t)(0x8000u | ((published % 16u)+1u)));
                assert(clean_sdio_credit_available(posted, consumed, CLEAN_SDIO_WINDOW) == 1);
                ++posted;
                ++published;
                assert(!clean_sdio_credit_available(posted, consumed, CLEAN_SDIO_WINDOW));
                assert(clean_sdio_credit_available(captured, published, 8u) == 1);
                ++consumed;
            }
            assert(captured == published && posted == consumed);
        }
    }
    assert(clean_sdio_drain_state(8u,0u,8u,0u,0u,0u) == 1);
    assert(clean_sdio_drain_state(9u,0u,8u,0u,0u,0u) == -1);
    assert(clean_sdio_drain_state(0u,1u,8u,0u,0u,0u) == -1);
    assert(clean_sdio_drain_state(0u,0u,0u,0u,0u,0u) == -1);
    assert(clean_sdio_drain_state(0u,0u,17u,0u,0u,0u) == -1);
    assert(clean_sdio_drain_state(1u,0u,8u,0xfffffff0u,
        0xfffffff0u+CLEAN_SDIO_DRAIN_US-1u,CLEAN_SDIO_DRAIN_POLLS-1u) == 1);
    assert(clean_sdio_drain_state(1u,0u,8u,0xfffffff0u,
        0xfffffff0u+CLEAN_SDIO_DRAIN_US,0u) == -2);
    assert(clean_sdio_drain_state(1u,0u,8u,0u,0u,CLEAN_SDIO_DRAIN_POLLS) == -2);
    assert(clean_sdio_drain_state(1u,1u,8u,0u,CLEAN_SDIO_DRAIN_US,
        CLEAN_SDIO_DRAIN_POLLS) == 0);
    assert(clean_sdio_drain_state(3u,0xfffffffbu,8u,0u,0u,0u) == 1);
    puts("RX staging: eight-slot FIFO, immutable correlation across counter wrap, one-credit drain and bounded stop pass");
}

static void fcs_reference(void) {
    uint8_t data[CLEAN_MPDU_BYTES], value;
    assert(clean_mac_fcs((const uint8_t *)"123456789", 9u) == 0xcbf43926u);
    for (uint32_t i = 0u; i < 256u; ++i) {
        value = (uint8_t)i;
        assert(clean_mac_fcs(&value, 1u) == diag_crc32(&value, 1u));
    }
    uint32_t random = 0x12345678u;
    for (uint32_t i = 0u; i < sizeof(data); ++i) {
        random ^= random << 13u;
        random ^= random >> 17u;
        random ^= random << 5u;
        data[i] = (uint8_t)random;
    }
    for (uint32_t length = 0u; length <= sizeof(data); ++length)
        assert(clean_mac_fcs(data, length) == diag_crc32(data, length));
    puts("MAC FCS: nibble implementation matches independent bitwise CRC for known vector, every byte and every supported length");
}

static void pause_psm(void) {
    const uint32_t controls[] = {0xc0020403u, 0xc1d20403u, 0xc1d20401u,
                                 0x84020400u, 0xffffffffu};
    for (uint32_t i = 0u; i < sizeof(controls) / sizeof(controls[0]); ++i) {
        uint32_t running = controls[i], paused = clean_mac_pause_psm(running);
        assert(!(paused & 2u));
        assert((paused & 1u) == (running & 1u));
        assert((paused ^ running) == (running & 2u));
        assert(clean_mac_pause_psm(paused) == paused);
    }
    puts("MAC control: PSM pause preserves RX enable, filtering and all non-PSM control bits");
}

typedef struct {
    uint16_t shm[0x1000u];
    uint32_t count, writes, reads, fault;
    uint8_t drop_write, corrupt_read;
} response_memory_t;

static void response_write(void *context, uint32_t word, uint16_t value) {
    response_memory_t *m = context;
    assert(m->writes < m->count && !m->reads);
    assert(word == clean_response_rate_word(m->writes++));
    if (word != m->fault || !m->drop_write) m->shm[word] = value;
}

static uint16_t response_read(void *context, uint32_t word) {
    response_memory_t *m = context;
    assert(m->writes == m->count && m->reads < m->count);
    assert(word == clean_response_rate_word(m->reads++));
    return m->shm[word] ^ (uint16_t)(word == m->fault && m->corrupt_read);
}

static void response_policy(void) {
    uint16_t words[CLEAN_RESPONSE_RATE_WORDS], saved[CLEAN_RESPONSE_RATE_WORDS];
    memset(words, 0xa5, sizeof(words));
    memcpy(saved, words, sizeof(saved));
    assert(!clean_response_rates_prepare(words, 0u, 0u));
    assert(!clean_response_rates_prepare(words, 0x1000u, 0u));
    assert(!clean_response_rates_prepare(words, 0x150u, 2u));
    assert(!memcmp(words, saved, sizeof(words)));
    for (uint32_t ht = 0u; ht < 2u; ++ht)
        for (uint32_t basic = 1u; basic < 4096u; ++basic) {
            assert(clean_response_rates_prepare(words, (uint16_t)basic, ht));
            for (uint32_t i = 0u; i < 12u; ++i) {
                uint8_t plcp[6]; uint16_t phy;
                int rate = clean_ack_rate((uint16_t)basic, i);
                assert(rate >= 0 && clean_ack_template(plcp, (uint32_t)rate, &phy));
                assert(!memcmp(words + 4u*i, plcp, sizeof(plcp)));
                assert(words[4u*i + 3u] == phy);
                assert(words[48u + i] == clean_ack_airtime_us((uint16_t)basic, i));
            }
            if (!ht) for (uint32_t i = CLEAN_RESPONSE_LEGACY_WORDS;
                          i < CLEAN_RESPONSE_RATE_WORDS; ++i) assert(!words[i]);
        }
    assert(clean_response_rates_prepare(words, 0x150u, 1u));
    static const uint16_t airtime[8] = {50u,38u,38u,34u,34u,34u,34u,34u};
    for (uint32_t i = 0u; i < 8u; ++i) {
        uint32_t at = CLEAN_RESPONSE_LEGACY_WORDS + CLEAN_HT_ACK_RECORD_WORDS*i;
        assert(words[at + 4u] == 0x41u && words[at + 5u] == airtime[i]);
    }
    for (uint32_t ht = 0u; ht < 2u; ++ht) {
        uint32_t count = ht ? CLEAN_RESPONSE_RATE_WORDS : CLEAN_RESPONSE_LEGACY_WORDS;
        for (uint32_t fault = 0u; fault <= 2u*count; ++fault) {
            response_memory_t m;
            memset(&m, 0, sizeof(m));
            memset(m.shm, 0xa5, sizeof(m.shm));
            m.count = count;
            m.fault = fault < 2u*count ? clean_response_rate_word(fault/2u) : 0u;
            m.drop_write = (uint8_t)(fault < 2u*count && !(fault & 1u));
            m.corrupt_read = (uint8_t)(fault < 2u*count && (fault & 1u));
            clean_response_io_t io = {.read=response_read, .write=response_write, .context=&m};
            int result = clean_response_rates_install(&io, 0x150u, ht);
            assert(result == (fault == 2u*count));
            assert(m.writes == count);
            if (result) {
                assert(m.reads == count);
                for (uint32_t i = 0u; i < 0x1000u; ++i) {
                    if (i >= CLEAN_ACK_TABLE && i < CLEAN_ACK_TABLE + CLEAN_RESPONSE_LEGACY_WORDS)
                        assert(m.shm[i] == words[i - CLEAN_ACK_TABLE]);
                    else if (ht && i >= CLEAN_HT_ACK_TABLE &&
                             i < CLEAN_HT_ACK_TABLE + 8u*CLEAN_HT_ACK_RECORD_WORDS)
                        assert(m.shm[i] == words[CLEAN_RESPONSE_LEGACY_WORDS + i - CLEAN_HT_ACK_TABLE]);
                    else assert(m.shm[i] == 0xa5a5u);
                }
            } else {
                assert(io.failed_word == m.fault && m.reads == fault/2u + 1u);
                assert(io.actual == (m.drop_write ? 0xa5a5u :
                    (uint16_t)(words[fault/2u] ^ 1u)));
            }
        }
        response_memory_t m;
        memset(&m, 0, sizeof(m));
        clean_response_io_t io = {.read=response_read, .write=response_write, .context=&m};
        assert(!clean_response_rates_install(&io, 0u, ht));
        assert(!m.reads && !m.writes && !io.failed_word && !io.actual);
    }
    puts("Policy response tables: all 4095 basic-rate masks, legacy/HT tables, complete write-before-read, every dropped write/corrupt read, invalid-input atomicity and held-capture preservation pass; NOT SIFS qualification");
}

int main(void) {
    pause_psm();
    response_policy();
    fcs_reference();
    contention();
    edca();
    retries_and_nav();
    receive();
    sequence_soak();
    station_delivery();
    station_transmit();
    soak_protocol();
    soak_fragment_protocol();
    qos_receive();
    station_qos_delivery();
    station_edca_tx();
    receive_relevance();
    capture_queue();
    puts("MAC: DCF/NAV/EIFS/wrap, retry identity, FCS, duplicates and bounded reassembly OK");
    puts("MAC: 100000 native deliveries per simulated direction across 3 sizes; NOT RF qualification");
    return 0;
}

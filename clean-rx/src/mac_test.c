#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mac.h"
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
    assert(clean_mac_rx(&s, m, n, CLEAN_MAC_RX_LIFETIME_US) == CLEAN_MAC_DELIVER);
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
    n = frame(m, 8u, 0u, 4u, CLEAN_MSDU_BYTES);
    assert(clean_mac_rx(&s, m, n, 3000000u) == CLEAN_MAC_FRAGMENT);
    n = frame(m, 8u, 1u, 0u, 1u);
    assert(clean_mac_rx(&s, m, n, 3000001u) == CLEAN_MAC_INVALID);
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
        assert(clean_ack_timeout_us(rate, 9u) == (rate < 4u ? 343u : 89u));
        assert(clean_ack_timeout_us(rate, 20u) == (rate < 4u ? 354u : 100u));
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

int main(void) {
    contention();
    retries_and_nav();
    receive();
    sequence_soak();
    station_delivery();
    station_transmit();
    puts("MAC: DCF/NAV/EIFS/wrap, retry identity, FCS, duplicates and bounded reassembly OK");
    puts("MAC: 100000 native deliveries per simulated direction across 3 sizes; NOT RF qualification");
    return 0;
}

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../radio/wifi_udp.h"

static const uint8_t macs[2][6] = {
    {2u, 0x26u, 0u, 0u, 0u, 1u}, {2u, 0x26u, 0u, 0u, 0u, 2u}
};

static const uint8_t golden_ping[74] = {
    0x02u, 0x26u, 0x00u, 0x00u, 0x00u, 0x02u,
    0x02u, 0x26u, 0x00u, 0x00u, 0x00u, 0x01u, 0x08u, 0x00u,
    0x45u, 0x00u, 0x00u, 0x3cu, 0x00u, 0x01u, 0x40u, 0x00u,
    0x40u, 0x11u, 0x1fu, 0x5cu, 0xc0u, 0xa8u, 0x4du, 0x01u,
    0xc0u, 0xa8u, 0x4du, 0x02u,
    0x77u, 0x3eu, 0x77u, 0x3eu, 0x00u, 0x28u, 0x64u, 0x89u,
    0x55u, 0x32u, 0x36u, 0x45u, 0x01u, 0x00u, 0x20u, 0x00u,
    0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x11u, 0x2eu, 0x4bu, 0x68u, 0x85u, 0xa2u, 0xbfu, 0xdcu,
    0xf9u, 0x16u, 0x33u, 0x50u, 0x6du, 0x8au, 0xa7u, 0xc4u
};

static uint32_t get16(const uint8_t *p) {
    return ((uint32_t)p[0] << 8u) + p[1];
}

static uint32_t get_le32(const uint8_t *p) {
    return (uint32_t)p[0] + ((uint32_t)p[1] << 8u) +
           ((uint32_t)p[2] << 16u) + ((uint32_t)p[3] << 24u);
}

static void set16(uint8_t *p, uint32_t n) {
    p[0] = (uint8_t)(n >> 8u);
    p[1] = (uint8_t)n;
}

static void set_le32(uint8_t *p, uint32_t n) {
    for (uint32_t i = 0u; i != 4u; ++i) p[i] = (uint8_t)(n >> (8u * i));
}

/* Independent byte-at-a-time checksum: fold after every byte, not every word. */
static uint16_t reference_checksum(const uint8_t *p, uint32_t length) {
    uint32_t sum = 0u;
    for (uint32_t i = 0u; i < length; ++i) {
        sum += (uint32_t)p[i] << ((i & 1u) ? 0u : 8u);
        sum = (sum & 65535u) + (sum >> 16u);
    }
    return (uint16_t)~sum;
}

static uint16_t reference_udp_checksum(const uint8_t *f) {
    uint8_t pseudo[1512];
    uint32_t length = get16(f + 38u);
    assert(length <= 1500u);
    memcpy(pseudo, f + 26u, 8u);
    pseudo[8] = 0u;
    pseudo[9] = 17u;
    set16(pseudo + 10u, length);
    memcpy(pseudo + 12u, f + 34u, length);
    return reference_checksum(pseudo, length + 12u);
}

static void fix_ip(uint8_t *f) {
    f[24] = 0u;
    f[25] = 0u;
    set16(f + 24u, reference_checksum(f + 14u, 20u));
}

static void fix_udp(uint8_t *f) {
    f[40] = 0u;
    f[41] = 0u;
    uint16_t sum = reference_udp_checksum(f);
    set16(f + 40u, sum == 0u ? 65535u : sum);
}

static void reference_arp(uint8_t *f, uint8_t source, int response) {
    uint8_t target = (uint8_t)(source ^ 1u);
    memset(f, 0, 60u);
    if (response) memcpy(f, macs[target], 6u);
    else memset(f, 255, 6u);
    memcpy(f + 6u, macs[source], 6u);
    f[12] = 8u;
    f[13] = 6u;
    f[15] = 1u;
    f[16] = 8u;
    f[18] = 6u;
    f[19] = 4u;
    f[21] = response ? 2u : 1u;
    memcpy(f + 22u, macs[source], 6u);
    f[28] = 192u;
    f[29] = 168u;
    f[30] = 77u;
    f[31] = (uint8_t)(source + 1u);
    if (response) memcpy(f + 32u, macs[target], 6u);
    f[38] = 192u;
    f[39] = 168u;
    f[40] = 77u;
    f[41] = (uint8_t)(target + 1u);
}

static void reference_udp(uint8_t *f, uint8_t source, uint8_t type, uint8_t origin,
                           uint32_t sequence, uint32_t ticks) {
    memcpy(f, golden_ping, sizeof(golden_ping));
    memcpy(f, macs[source ^ 1u], 6u);
    memcpy(f + 6u, macs[source], 6u);
    f[29] = (uint8_t)(source + 1u);
    f[33] = (uint8_t)((source ^ 1u) + 1u);
    set16(f + 18u, sequence);
    f[46] = type;
    f[47] = origin;
    set_le32(f + 50u, sequence);
    set_le32(f + 54u, ticks);
    for (uint32_t i = 0u; i < 16u; ++i)
        f[58u + i] = (uint8_t)(sequence * 17u + origin * 53u + i * 29u);
    fix_ip(f);
    fix_udp(f);
}

static void check_wire(const uint8_t *f, uint32_t length, uint8_t source) {
    assert(length <= WIFI_UDP_FRAME_MAX);
    assert(memcmp(f + 6u, macs[source], 6u) == 0);
    if (get16(f + 12u) == 0x806u) {
        uint8_t expected[60];
        assert(length == sizeof(expected));
        reference_arp(expected, source, get16(f + 20u) == 2u);
        assert(memcmp(f, expected, sizeof(expected)) == 0);
        return;
    }
    assert(length == sizeof(golden_ping));
    assert(memcmp(f, macs[source ^ 1u], 6u) == 0);
    assert(get16(f + 12u) == 0x800u && f[14] == 0x45u);
    assert(get16(f + 16u) == 60u && get16(f + 20u) == 0x4000u);
    assert(f[22] == 64u && f[23] == 17u);
    assert(f[26] == 192u && f[27] == 168u && f[28] == 77u);
    assert(f[29] == source + 1u && f[33] == (source ^ 1u) + 1u);
    assert(get16(f + 34u) == 30526u && get16(f + 36u) == 30526u);
    assert(get16(f + 38u) == 40u && get16(f + 40u) != 0u);
    assert(reference_checksum(f + 14u, 20u) == 0u);
    assert(reference_udp_checksum(f) == 0u);
    assert(memcmp(f + 42u, "U26E", 4u) == 0);
    assert(f[46] == 1u || f[46] == 2u);
    assert(f[47] == (f[46] == 1u ? source : (source ^ 1u)));
    assert(f[48] == 32u && f[49] == 0u);
    uint32_t sequence = get_le32(f + 50u);
    assert(sequence >= 1u && sequence <= 100u);
    for (uint32_t i = 0u; i < 16u; ++i)
        assert(f[58u + i] == (uint8_t)(17u * sequence + 53u * f[47] + 29u * i));
}

static void init(wifi_udp_t *net, uint8_t role, uint32_t now) {
    wifi_udp_init(net, role, macs[role], macs[role ^ 1u], now);
    assert(net->stage == WIFI_UDP_ARP && net->error == WIFI_UDP_OK);
    assert(!net->arp_ready && !net->pending && net->sent == 0u);
    assert(net->ip == UINT32_C(0xc0a84d01) + role);
}

static void learn(wifi_udp_t *net, uint8_t role, uint32_t now) {
    uint8_t f[WIFI_UDP_FRAME_MAX], reply[WIFI_UDP_FRAME_MAX];
    uint32_t length = UINT32_MAX;
    init(net, role, now);
    reference_arp(f, (uint8_t)(role ^ 1u), 0);
    assert(wifi_udp_receive(net, now, f, 60u, reply, &length) == WIFI_UDP_SEND_FRAME);
    assert(net->stage == WIFI_UDP_ECHO && net->arp_ready);
    assert(net->arp_rx == 1u && net->arp_tx == 1u);
    check_wire(reply, length, role);
}

static void expect_drop(wifi_udp_t *net, const uint8_t *f, uint32_t length,
                         uint32_t now, wifi_udp_drop_t reason) {
    wifi_udp_t before = *net;
    uint8_t reply[WIFI_UDP_FRAME_MAX];
    uint32_t output = UINT32_MAX;
    memset(reply, 0xa5, sizeof(reply));
    assert(wifi_udp_receive(net, now, f, length, reply, &output) == WIFI_UDP_DROPPED);
    assert(output == 0u && net->last_drop == reason);
    assert(net->dropped == before.dropped + 1u && net->stage == before.stage);
    assert(net->sent == before.sent && net->received == before.received);
    assert(net->lost == before.lost && net->echoed == before.echoed);
    assert(net->arp_rx == before.arp_rx && net->arp_tx == before.arp_tx);
    assert(net->peer_sequence == before.peer_sequence);
    assert(net->arp_ready == before.arp_ready && net->pending == before.pending);
    assert(net->rtt_sum == before.rtt_sum && net->last_rtt == before.last_rtt);
    assert(memcmp(net->samples, before.samples, sizeof(net->samples)) == 0);
    for (uint32_t i = 0u; i < sizeof(reply); ++i) assert(reply[i] == 0xa5u);
}

static void test_config(void) {
    wifi_udp_t net;
    uint8_t frame[WIFI_UDP_FRAME_MAX], bad[6] = {0u};
    uint32_t length = UINT32_MAX;
    wifi_udp_init(NULL, 0u, macs[0], macs[1], 0u);
    assert(wifi_udp_step(NULL, 0u, frame, &length) == WIFI_UDP_ERROR && length == 0u);
    length = UINT32_MAX;
    assert(wifi_udp_receive(NULL, 0u, frame, 0u, frame, &length) == WIFI_UDP_ERROR);
    assert(length == 0u);
    wifi_udp_init(&net, 2u, macs[0], macs[1], 0u);
    assert(net.stage == WIFI_UDP_FAILED && net.error == WIFI_UDP_BAD_CONFIG);
    wifi_udp_init(&net, 0u, NULL, macs[1], 0u);
    assert(net.error == WIFI_UDP_BAD_CONFIG);
    wifi_udp_init(&net, 0u, macs[0], NULL, 0u);
    assert(net.error == WIFI_UDP_BAD_CONFIG);
    wifi_udp_init(&net, 0u, bad, macs[1], 0u);
    assert(net.error == WIFI_UDP_BAD_CONFIG);
    wifi_udp_init(&net, 0u, macs[0], bad, 0u);
    assert(net.error == WIFI_UDP_BAD_CONFIG);
    bad[0] = 1u;
    wifi_udp_init(&net, 0u, bad, macs[1], 0u);
    assert(net.error == WIFI_UDP_BAD_CONFIG);
    wifi_udp_init(&net, 0u, macs[0], bad, 0u);
    assert(net.error == WIFI_UDP_BAD_CONFIG);
    wifi_udp_init(&net, 0u, macs[0], macs[0], 0u);
    assert(net.error == WIFI_UDP_BAD_CONFIG);
    init(&net, 0u, 0u);
    wifi_udp_init(&net, 0u, net.mac, net.peer_mac, 1u);
    assert(net.stage == WIFI_UDP_ARP && net.started == 1u);

    init(&net, 0u, 0u);
    assert(wifi_udp_step(&net, 0u, NULL, &length) == WIFI_UDP_ERROR && length == 0u);
    assert(net.error == WIFI_UDP_BAD_CONFIG);
    init(&net, 0u, 0u);
    assert(wifi_udp_step(&net, 0u, frame, NULL) == WIFI_UDP_ERROR);
    assert(net.error == WIFI_UDP_BAD_CONFIG);
    init(&net, 0u, 0u);
    assert(wifi_udp_receive(&net, 0u, NULL, 0u, frame, &length) == WIFI_UDP_ERROR);
    assert(length == 0u && net.error == WIFI_UDP_BAD_CONFIG);
    init(&net, 0u, 0u);
    assert(wifi_udp_receive(&net, 0u, frame, 0u, NULL, &length) == WIFI_UDP_ERROR);
    assert(length == 0u && net.error == WIFI_UDP_BAD_CONFIG);
    init(&net, 0u, 0u);
    assert(wifi_udp_receive(&net, 0u, frame, 0u, frame, NULL) == WIFI_UDP_ERROR);
    assert(net.error == WIFI_UDP_BAD_CONFIG);
    memset(&net, 0, sizeof(net));
    assert(wifi_udp_step(&net, 0u, frame, &length) == WIFI_UDP_ERROR);
    assert(net.error == WIFI_UDP_BAD_CONFIG && length == 0u);
    init(&net, 0u, 0u);
    net.pending = 1u;
    assert(wifi_udp_step(&net, 0u, frame, &length) == WIFI_UDP_ERROR);
    assert(net.error == WIFI_UDP_BAD_CONFIG);
    init(&net, 0u, 0u);
    net.stage = WIFI_UDP_DONE;
    assert(wifi_udp_step(&net, 0u, frame, &length) == WIFI_UDP_ERROR);
    init(&net, 0u, 0u);
    net.stage = WIFI_UDP_FAILED;
    net.error = WIFI_UDP_TX_ERROR;
    assert(wifi_udp_step(&net, 0u, frame, &length) == WIFI_UDP_ERROR);
    assert(net.error == WIFI_UDP_TX_ERROR);
    assert(wifi_udp_receive(&net, 0u, frame, 0u, frame, &length) == WIFI_UDP_ERROR);
    assert(net.error == WIFI_UDP_TX_ERROR && length == 0u);
    assert(strcmp(wifi_udp_stage_name(WIFI_UDP_DONE), "DONE") == 0);
    assert(strcmp(wifi_udp_error_name(WIFI_UDP_ARP_TIMEOUT), "ARP_TIMEOUT") == 0);
    for (int i = 0; i <= (int)WIFI_UDP_FAILED; ++i)
        assert(strcmp(wifi_udp_stage_name((wifi_udp_stage_t)i), "UNKNOWN") != 0);
    for (int i = 0; i <= (int)WIFI_UDP_RADIO_ERROR; ++i)
        assert(strcmp(wifi_udp_error_name((wifi_udp_error_t)i), "UNKNOWN") != 0);
    assert(strcmp(wifi_udp_stage_name((wifi_udp_stage_t)99), "UNKNOWN") == 0);
    assert(strcmp(wifi_udp_error_name((wifi_udp_error_t)99), "UNKNOWN") == 0);
}

static void test_arp(void) {
    wifi_udp_t net;
    uint8_t f[WIFI_UDP_FRAME_MAX], out[WIFI_UDP_FRAME_MAX], expected[60];
    uint32_t n = UINT32_MAX;
    uint32_t start = UINT32_MAX - 50000u;
    init(&net, 0u, start);
    reference_udp(f, 1u, 1u, 1u, 1u, start);
    expect_drop(&net, f, 74u, start, WIFI_UDP_DROP_ADDRESS);
    for (uint32_t i = 0u; i < 5u; ++i) {
        assert(wifi_udp_step(&net, start + i * 50000u, out, &n) == WIFI_UDP_SEND_FRAME);
        check_wire(out, n, 0u);
        assert(net.arp_attempts == i + 1u && net.arp_tx == i + 1u);
        assert(net.sent == 0u && !net.arp_ready);
        n = UINT32_MAX;
        assert(wifi_udp_step(&net, start + i * 50000u + 49999u, out, &n) == WIFI_UDP_NO_FRAME);
        assert(n == 0u);
    }
    assert(wifi_udp_step(&net, start + 250000u, out, &n) == WIFI_UDP_ERROR);
    assert(net.stage == WIFI_UDP_FAILED && net.error == WIFI_UDP_ARP_TIMEOUT);
    assert(net.sent == 0u && net.arp_tx == 5u && n == 0u);
    init(&net, 0u, 0u);
    assert(wifi_udp_step(&net, 1000000u, out, &n) == WIFI_UDP_ERROR);
    assert(net.error == WIFI_UDP_ARP_TIMEOUT);

    init(&net, 0u, 0u);
    assert(wifi_udp_step(&net, 0u, out, &n) == WIFI_UDP_SEND_FRAME);
    reference_arp(expected, 0u, 0);
    assert(n == sizeof(expected) && memcmp(out, expected, n) == 0);
    reference_arp(f, 1u, 1);
    assert(wifi_udp_receive(&net, 1u, f, 42u, out, &n) == WIFI_UDP_NO_FRAME);
    assert(n == 0u && net.stage == WIFI_UDP_ECHO && net.arp_ready);
    assert(net.arp_rx == 1u && net.arp_tx == 1u);
    reference_arp(f, 1u, 0);
    memset(f + 42u, 0x97, sizeof(f) - 42u);
    assert(wifi_udp_receive(&net, 2u, f, sizeof(f), out, &n) == WIFI_UDP_SEND_FRAME);
    check_wire(out, n, 0u);
    assert(net.arp_rx == 2u && net.arp_tx == 2u);
    memcpy(f, macs[0], 6u);
    memcpy(f + 32u, macs[0], 6u);
    assert(wifi_udp_receive(&net, 3u, f, 42u, out, &n) == WIFI_UDP_SEND_FRAME);
    check_wire(out, n, 0u);
    /* Input/output aliasing is safe, including an ARP response. */
    assert(wifi_udp_receive(&net, 4u, f, 42u, f, &n) == WIFI_UDP_SEND_FRAME);
    check_wire(f, n, 0u);

    learn(&net, 0u, start);
    uint32_t expiry = net.arp_expires;
    assert(wifi_udp_step(&net, expiry, out, &n) == WIFI_UDP_SEND_FRAME);
    assert(net.stage == WIFI_UDP_ARP && !net.arp_ready && net.sent == 0u);
    check_wire(out, n, 0u);
    reference_arp(f, 1u, 1);
    assert(wifi_udp_receive(&net, expiry + 1u, f, 60u, out, &n) == WIFI_UDP_NO_FRAME);
    assert(net.stage == WIFI_UDP_ECHO && net.arp_ready);
    assert(wifi_udp_step(&net, expiry + 1u, out, &n) == WIFI_UDP_SEND_FRAME);
    check_wire(out, n, 0u);
    assert(net.sent == 1u);
}

static void test_arp_refresh(uint32_t start) {
    wifi_udp_t net;
    uint8_t f[WIFI_UDP_FRAME_MAX], out[WIFI_UDP_FRAME_MAX];
    uint32_t n;
    learn(&net, 0u, start);
    uint32_t expiry = net.arp_expires;
    uint32_t refresh = expiry - 200000u;
    assert(wifi_udp_step(&net, refresh, out, &n) == WIFI_UDP_SEND_FRAME);
    assert(n == 60u && net.stage == WIFI_UDP_ECHO && net.arp_ready);
    assert(net.arp_expires == expiry && net.arp_attempts == 1u);
    check_wire(out, n, 0u);
    reference_udp(f, 1u, 1u, 1u, 1u, refresh);
    assert(wifi_udp_receive(&net, refresh + 1u, f, 74u, out, &n) == WIFI_UDP_SEND_FRAME);
    assert(net.echoed == 1u && net.stage == WIFI_UDP_ECHO && net.arp_expires == expiry);
    check_wire(out, n, 0u);
    assert(wifi_udp_step(&net, refresh + 49999u, out, &n) == WIFI_UDP_NO_FRAME);
    reference_arp(f, 1u, 1);
    f[22] ^= 128u;
    assert(wifi_udp_receive(&net, refresh + 50000u, f, 60u, out, &n) == WIFI_UDP_DROPPED);
    assert(net.arp_expires == expiry && net.arp_ready);
    reference_arp(f, 1u, 1);
    assert(wifi_udp_receive(&net, refresh + 50001u, f, 60u, out, &n) == WIFI_UDP_NO_FRAME);
    assert(net.arp_expires == refresh + 50001u + 2000000u && net.arp_attempts == 0u);

    learn(&net, 0u, start);
    uint32_t arp_tx = net.arp_tx;
    assert(wifi_udp_step(&net, refresh - 1u, out, &n) == WIFI_UDP_SEND_FRAME);
    assert(n == 74u && net.pending);
    assert(wifi_udp_step(&net, refresh, out, &n) == WIFI_UDP_NO_FRAME);
    assert(net.pending && net.arp_tx == arp_tx && net.arp_expires == expiry);
    reference_udp(f, 1u, 2u, 0u, 1u, refresh - 1u);
    assert(wifi_udp_receive(&net, refresh + 1u, f, 74u, out, &n) == WIFI_UDP_NO_FRAME);
    assert(!net.pending && net.received == 1u);
    assert(wifi_udp_step(&net, refresh + 1u, out, &n) == WIFI_UDP_NO_FRAME);
    assert(wifi_udp_step(&net, refresh + 10000u, out, &n) == WIFI_UDP_SEND_FRAME);
    assert(n == 60u && net.arp_ready && net.arp_expires == expiry);

    learn(&net, 0u, start);
    for (uint32_t i = 0u; i < 4u; ++i) {
        assert(wifi_udp_step(&net, refresh + i * 50000u, out, &n) == WIFI_UDP_SEND_FRAME);
        assert(n == 60u && net.arp_ready && net.stage == WIFI_UDP_ECHO);
        assert(net.arp_expires == expiry && net.arp_attempts == i + 1u);
        check_wire(out, n, 0u);
    }
    assert(wifi_udp_step(&net, expiry, out, &n) == WIFI_UDP_SEND_FRAME);
    assert(n == 60u && !net.arp_ready && net.stage == WIFI_UDP_ARP);
    assert(net.arp_attempts == 1u && net.arp_expires == expiry + 250000u);
}

static void test_bad_arp(void) {
    wifi_udp_t net;
    uint8_t base[WIFI_UDP_FRAME_MAX], f[WIFI_UDP_FRAME_MAX];
    init(&net, 0u, 0u);
    reference_arp(base, 1u, 1);
    for (uint32_t length = 0u; length < 42u; ++length) {
        uint8_t *short_frame = malloc(length != 0u ? length : 1u);
        assert(short_frame != NULL);
        memcpy(short_frame, base, length);
        expect_drop(&net, short_frame, length, 0u, WIFI_UDP_DROP_LENGTH);
        free(short_frame);
    }
    expect_drop(&net, base, WIFI_UDP_FRAME_MAX + 1u, 0u, WIFI_UDP_DROP_LENGTH);
    const uint32_t address_offsets[] = {0u, 6u, 22u, 28u, 31u, 32u, 38u, 41u};
    for (uint32_t i = 0u; i < sizeof(address_offsets) / sizeof(address_offsets[0]); ++i) {
        memcpy(f, base, 60u);
        f[address_offsets[i]] ^= 4u;
        expect_drop(&net, f, 60u, 0u, WIFI_UDP_DROP_ADDRESS);
    }
    const uint32_t protocol_offsets[] = {12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u, 20u, 21u};
    for (uint32_t i = 0u; i < sizeof(protocol_offsets) / sizeof(protocol_offsets[0]); ++i) {
        memcpy(f, base, 60u);
        f[protocol_offsets[i]] ^= 128u;
        expect_drop(&net, f, 60u, 0u, WIFI_UDP_DROP_PROTOCOL);
    }
    memcpy(f, base, 60u);
    memset(f, 255, 6u);
    expect_drop(&net, f, 60u, 0u, WIFI_UDP_DROP_ADDRESS);
    memcpy(f, base, 60u);
    memset(f + 22u, 0, 6u);
    expect_drop(&net, f, 60u, 0u, WIFI_UDP_DROP_ADDRESS);
    memcpy(f, base, 60u);
    memset(f + 28u, 0, 4u);
    expect_drop(&net, f, 60u, 0u, WIFI_UDP_DROP_ADDRESS);
    reference_arp(f, 1u, 0);
    memset(f + 32u, 255, 6u);
    expect_drop(&net, f, 60u, 0u, WIFI_UDP_DROP_ADDRESS);
    reference_arp(f, 1u, 0);
    f[0] = 1u;
    expect_drop(&net, f, 60u, 0u, WIFI_UDP_DROP_ADDRESS);
    assert(net.stage == WIFI_UDP_ARP && !net.arp_ready && net.arp_rx == 0u);
}

static void test_golden_and_rtt(void) {
    wifi_udp_t net, peer;
    uint8_t f[WIFI_UDP_FRAME_MAX], out[WIFI_UDP_FRAME_MAX];
    uint32_t n = UINT32_MAX;
    learn(&net, 0u, 0u);
    assert(wifi_udp_step(&net, 0u, f, &n) == WIFI_UDP_SEND_FRAME);
    assert(n == sizeof(golden_ping) && memcmp(f, golden_ping, n) == 0);
    assert(net.sent == 1u && net.pending && net.received == 0u);
    learn(&peer, 1u, 0u);
    assert(wifi_udp_receive(&peer, 4u, golden_ping, sizeof(golden_ping), out, &n) ==
           WIFI_UDP_SEND_FRAME);
    check_wire(out, n, 1u);
    assert(peer.echoed == 1u && peer.sent == 0u && !peer.pending);
    assert(out[46] == 2u && out[47] == 0u);
    assert(wifi_udp_receive(&net, 9u, out, n, f, &n) == WIFI_UDP_NO_FRAME);
    assert(n == 0u && net.received == 1u && !net.pending);
    assert(net.samples[0] == 9u && net.rtt_sum == 9u && net.last_rtt == 9u);
    assert(net.rtt_min == 9u && net.rtt_max == 9u);
    expect_drop(&net, out, 74u, 10u, WIFI_UDP_DROP_STALE);
    assert(net.duplicates == 1u);
    expect_drop(&peer, golden_ping, 74u, 10u, WIFI_UDP_DROP_STALE);
    assert(peer.duplicates == 1u && peer.echoed == 1u);
    assert(wifi_udp_step(&net, 9999u, f, &n) == WIFI_UDP_NO_FRAME && n == 0u);
    assert(wifi_udp_step(&net, 10000u, f, &n) == WIFI_UDP_SEND_FRAME);
    reference_udp(out, 1u, 2u, 0u, 2u, 10000u);
    assert(wifi_udp_receive(&net, 10000u, out, 74u, f, &n) == WIFI_UDP_NO_FRAME);
    assert(net.samples[1] == 1u && net.rtt_sum == 10u && net.rtt_min == 1u);
    assert(net.rtt_max == 9u);

    uint32_t start = UINT32_MAX - 5u;
    learn(&net, 0u, start);
    assert(wifi_udp_step(&net, start, f, &n) == WIFI_UDP_SEND_FRAME);
    reference_udp(out, 1u, 2u, 0u, 1u, start);
    assert(wifi_udp_receive(&net, 9u, out, 74u, f, &n) == WIFI_UDP_NO_FRAME);
    assert(net.samples[0] == 15u && net.received == 1u);
    assert(wifi_udp_step(&net, start + 9999u, f, &n) == WIFI_UDP_NO_FRAME);
    assert(wifi_udp_step(&net, start + 10000u, f, &n) == WIFI_UDP_SEND_FRAME);
    reference_udp(out, 1u, 2u, 0u, 2u, start + 10000u);
    assert(wifi_udp_receive(&net, start + 109999u, out, 74u, f, &n) == WIFI_UDP_NO_FRAME);
    assert(net.samples[1] == 99999u && net.lost == 0u);
}

static void test_bad_udp(void) {
    wifi_udp_t net;
    uint8_t base[WIFI_UDP_FRAME_MAX], f[WIFI_UDP_FRAME_MAX], reply[WIFI_UDP_FRAME_MAX];
    uint32_t n;
    learn(&net, 0u, 0u);
    reference_udp(base, 1u, 1u, 1u, 1u, 123u);
    for (uint32_t length = 0u; length < 74u; ++length) {
        uint8_t *short_frame = malloc(length != 0u ? length : 1u);
        assert(short_frame != NULL);
        memcpy(short_frame, base, length);
        expect_drop(&net, short_frame, length, 0u, WIFI_UDP_DROP_LENGTH);
        free(short_frame);
    }
    expect_drop(&net, base, UINT32_MAX, 0u, WIFI_UDP_DROP_LENGTH);

    const uint32_t addresses[] = {0u, 6u, 26u, 29u, 30u, 33u};
    for (uint32_t i = 0u; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        memcpy(f, base, 74u);
        f[addresses[i]] ^= 4u;
        fix_ip(f);
        fix_udp(f);
        expect_drop(&net, f, 74u, 0u, WIFI_UDP_DROP_ADDRESS);
    }
    memcpy(f, base, 74u);
    memset(f, 255, 6u);
    expect_drop(&net, f, 74u, 0u, WIFI_UDP_DROP_ADDRESS);
    memcpy(f, base, 74u);
    memset(f + 26u, 0, 8u);
    fix_ip(f);
    fix_udp(f);
    expect_drop(&net, f, 74u, 0u, WIFI_UDP_DROP_ADDRESS);

    const struct { uint32_t offset; uint8_t value; } protocols[] = {
        {12u, 0x81u}, {12u, 0x86u}, {13u, 0xddu}, {14u, 0x44u},
        {14u, 0x46u}, {14u, 0x65u}, {20u, 0x80u}, {20u, 0x20u},
        {21u, 1u}, {22u, 0u}, {23u, 1u}, {23u, 6u},
        {34u, 0u}, {36u, 0u}
    };
    for (uint32_t i = 0u; i < sizeof(protocols) / sizeof(protocols[0]); ++i) {
        memcpy(f, base, 74u);
        f[protocols[i].offset] = protocols[i].value;
        fix_ip(f);
        fix_udp(f);
        expect_drop(&net, f, 74u, 0u, WIFI_UDP_DROP_PROTOCOL);
    }
    const uint32_t ip_lengths[] = {0u, 19u, 20u, 27u, 61u, 65535u};
    for (uint32_t i = 0u; i < sizeof(ip_lengths) / sizeof(ip_lengths[0]); ++i) {
        memcpy(f, base, 74u);
        set16(f + 16u, ip_lengths[i]);
        fix_ip(f);
        expect_drop(&net, f, 74u, 0u, WIFI_UDP_DROP_LENGTH);
    }
    const uint32_t udp_lengths[] = {0u, 7u, 8u, 39u, 41u, 65535u};
    for (uint32_t i = 0u; i < sizeof(udp_lengths) / sizeof(udp_lengths[0]); ++i) {
        memcpy(f, base, 74u);
        set16(f + 38u, udp_lengths[i]);
        expect_drop(&net, f, 74u, 0u, WIFI_UDP_DROP_LENGTH);
    }
    const uint32_t checksum_offsets[] = {15u, 18u, 24u, 25u, 40u, 41u, 73u};
    for (uint32_t i = 0u; i < sizeof(checksum_offsets) / sizeof(checksum_offsets[0]); ++i) {
        memcpy(f, base, 74u);
        f[checksum_offsets[i]] ^= 1u;
        expect_drop(&net, f, 74u, 0u, WIFI_UDP_DROP_CHECKSUM);
    }
    memcpy(f, base, 74u);
    memset(f + 40u, 0, 2u);
    expect_drop(&net, f, 74u, 0u, WIFI_UDP_DROP_CHECKSUM);
    for (uint32_t i = 42u; i < 74u; ++i) {
        if (i >= 54u && i < 58u) continue; /* Peer clocks are independent. */
        memcpy(f, base, 74u);
        f[i] ^= 128u;
        fix_udp(f);
        expect_drop(&net, f, 74u, 0u, WIFI_UDP_DROP_PAYLOAD);
    }
    const uint32_t bad_sequences[] = {0u, 101u, UINT32_MAX};
    for (uint32_t i = 0u; i < sizeof(bad_sequences) / sizeof(bad_sequences[0]); ++i) {
        reference_udp(f, 1u, 1u, 1u, bad_sequences[i], 0u);
        expect_drop(&net, f, 74u, 0u, WIFI_UDP_DROP_PAYLOAD);
    }
    reference_udp(f, 1u, 1u, 0u, 1u, 0u);
    expect_drop(&net, f, 74u, 0u, WIFI_UDP_DROP_PAYLOAD);
    reference_udp(f, 1u, 2u, 1u, 1u, 0u);
    expect_drop(&net, f, 74u, 0u, WIFI_UDP_DROP_PAYLOAD);
    memcpy(f, base, 74u);
    memset(f + 74u, 0xea, sizeof(f) - 74u);
    /* Ethernet padding is never included in either checksum. */
    assert(wifi_udp_receive(&net, 0u, f, sizeof(f), reply, &n) == WIFI_UDP_SEND_FRAME);
    check_wire(reply, n, 0u);
    reference_udp(f, 1u, 1u, 1u, 2u, 123u);
    f[20] = 0u; /* DF is optional. */
    f[15] = 0xfcu; /* DSCP/ECN are not an unsupported IP option. */
    f[22] = 1u;
    fix_ip(f);
    assert(wifi_udp_receive(&net, 0u, f, 74u, f, &n) == WIFI_UDP_SEND_FRAME);
    check_wire(f, n, 0u);
}

static void test_checksum_edges(void) {
    wifi_udp_t net;
    uint8_t f[WIFI_UDP_FRAME_MAX], out[WIFI_UDP_FRAME_MAX];
    uint32_t n;
    learn(&net, 0u, 0u);
    const uint32_t payload_lengths[] = {0u, 1u, 2u, 31u, 33u, 1471u, 1472u};
    for (uint32_t i = 0u; i < sizeof(payload_lengths) / sizeof(payload_lengths[0]); ++i) {
        uint32_t payload = payload_lengths[i];
        memset(f, 0xa7, sizeof(f));
        reference_udp(f, 1u, 1u, 1u, 1u, 7u);
        set16(f + 16u, 28u + payload);
        set16(f + 38u, 8u + payload);
        fix_ip(f);
        fix_udp(f);
        assert(reference_udp_checksum(f) == 0u);
        expect_drop(&net, f, 42u + payload, 0u, WIFI_UDP_DROP_PAYLOAD);
        if (payload != 0u) {
            f[41u + payload] ^= 1u;
            expect_drop(&net, f, 42u + payload, 0u, WIFI_UDP_DROP_CHECKSUM);
        }
    }
    /* A computed zero checksum must be transmitted as 0xffff, not disabled. */
    uint32_t stamp = 0u;
    for (; stamp <= 65535u; ++stamp) {
        reference_udp(f, 0u, 1u, 0u, 1u, stamp);
        f[40] = 0u;
        f[41] = 0u;
        if (reference_udp_checksum(f) == 0u) break;
    }
    assert(stamp <= 65535u);
    learn(&net, 0u, stamp);
    assert(wifi_udp_step(&net, stamp, f, &n) == WIFI_UDP_SEND_FRAME);
    assert(get16(f + 40u) == 65535u);
    check_wire(f, n, 0u);
    learn(&net, 1u, stamp);
    assert(wifi_udp_receive(&net, stamp, f, 74u, out, &n) == WIFI_UDP_SEND_FRAME);
    check_wire(out, n, 1u);
    f[40] = 0u;
    f[41] = 0u;
    expect_drop(&net, f, 74u, stamp, WIFI_UDP_DROP_CHECKSUM);
}

static void test_pending_losses_and_done(void) {
    wifi_udp_t net;
    uint8_t f[WIFI_UDP_FRAME_MAX], out[WIFI_UDP_FRAME_MAX];
    uint32_t n;
    uint32_t start = UINT32_MAX - 50000u;
    learn(&net, 0u, start);
    assert(wifi_udp_step(&net, start, out, &n) == WIFI_UDP_SEND_FRAME);
    reference_udp(f, 1u, 2u, 0u, 1u, start + 1u);
    expect_drop(&net, f, 74u, start + 1u, WIFI_UDP_DROP_STALE);
    reference_udp(f, 1u, 2u, 0u, 2u, start);
    expect_drop(&net, f, 74u, start + 1u, WIFI_UDP_DROP_STALE);
    assert(wifi_udp_step(&net, start + 99999u, out, &n) == WIFI_UDP_NO_FRAME);
    assert(net.sent == 1u && net.pending && n == 0u);
    reference_udp(f, 1u, 2u, 0u, 1u, start);
    /* Receive also expires a pending ping, even without another step call. */
    assert(wifi_udp_receive(&net, start + 100000u, f, 74u, out, &n) == WIFI_UDP_DROPPED);
    assert(net.last_drop == WIFI_UDP_DROP_STALE && !net.pending);
    assert(net.lost == 1u && net.received == 0u && net.samples[0] == 0u);
    assert(net.duplicates == 3u && net.sent == 1u && n == 0u);
    assert(wifi_udp_step(&net, start + 100000u, out, &n) == WIFI_UDP_SEND_FRAME);
    assert(net.sent == 2u && get_le32(out + 50u) == 2u);
    expect_drop(&net, f, 74u, start + 100001u, WIFI_UDP_DROP_STALE);
    reference_udp(f, 1u, 2u, 0u, 2u, start + 100000u);
    assert(wifi_udp_receive(&net, start + 100003u, f, 74u, out, &n) == WIFI_UDP_NO_FRAME);
    assert(net.samples[1] == 3u && net.samples[0] == 0u && net.received == 1u);

    learn(&net, 0u, 0u);
    for (uint32_t sequence = 1u; sequence <= 100u; ++sequence) {
        uint32_t now = (sequence - 1u) * 10000u;
        assert(wifi_udp_step(&net, now, out, &n) == WIFI_UDP_SEND_FRAME);
        assert(net.sent == sequence && net.stage == WIFI_UDP_ECHO && net.pending);
        reference_udp(f, 1u, 2u, 0u, sequence, now);
        if (sequence == 100u) break;
        assert(wifi_udp_receive(&net, now + 5u, f, 74u, out, &n) == WIFI_UDP_NO_FRAME);
    }
    assert(net.sent == 100u && net.received == 99u && net.stage != WIFI_UDP_DONE);
    assert(wifi_udp_step(&net, 1089999u, out, &n) == WIFI_UDP_NO_FRAME);
    assert(net.stage != WIFI_UDP_DONE && n == 0u);
    assert(wifi_udp_step(&net, 1090000u, out, &n) == WIFI_UDP_NO_FRAME);
    assert(net.stage == WIFI_UDP_DONE && net.lost == 1u && net.samples[99] == 0u);
    expect_drop(&net, f, 74u, 1090001u, WIFI_UDP_DROP_STALE);
    assert(wifi_udp_step(&net, 20000000u, out, &n) == WIFI_UDP_NO_FRAME);
    assert(net.stage == WIFI_UDP_DONE && net.error == WIFI_UDP_OK && n == 0u);
    reference_udp(f, 1u, 1u, 1u, 99u, 20000000u);
    assert(wifi_udp_receive(&net, 20000001u, f, 74u, out, &n) == WIFI_UDP_SEND_FRAME);
    check_wire(out, n, 0u);
    assert(net.stage == WIFI_UDP_DONE && net.echoed == 1u);
    reference_arp(f, 1u, 0);
    assert(wifi_udp_receive(&net, 20000002u, f, 60u, out, &n) == WIFI_UDP_SEND_FRAME);
    check_wire(out, n, 0u);
    assert(net.stage == WIFI_UDP_DONE);
    reference_udp(f, 1u, 1u, 1u, 100u, 20000000u);
    assert(wifi_udp_receive(&net, 20000003u, f, 74u, out, &n) == WIFI_UDP_SEND_FRAME);
    assert(net.echoed == 2u && net.sent == 100u && net.received == 99u);

    learn(&net, 0u, start);
    assert(wifi_udp_step(&net, start, out, &n) == WIFI_UDP_SEND_FRAME);
    assert(wifi_udp_step(&net, net.deadline, out, &n) == WIFI_UDP_ERROR);
    assert(net.stage == WIFI_UDP_FAILED && net.error == WIFI_UDP_DEADLINE && n == 0u);
}

static void test_all_pings_lost(void) {
    wifi_udp_t net;
    uint8_t f[WIFI_UDP_FRAME_MAX], reply[WIFI_UDP_FRAME_MAX];
    uint32_t length;
    uint32_t start = UINT32_MAX - 1000000u;
    learn(&net, 0u, start);
    uint32_t elapsed;
    for (elapsed = 0u; elapsed < 11000000u; elapsed += 10000u) {
        wifi_udp_result_t result = wifi_udp_step(&net, start + elapsed, f, &length);
        assert(result != WIFI_UDP_ERROR && result != WIFI_UDP_DROPPED);
        if (result == WIFI_UDP_SEND_FRAME) {
            check_wire(f, length, 0u);
            if (get16(f + 12u) == 0x806u) {
                reference_arp(f, 1u, 1);
                assert(wifi_udp_receive(&net, start + elapsed, f, 60u, reply, &length) ==
                       WIFI_UDP_NO_FRAME);
            }
        } else {
            assert(length == 0u);
        }
        if (net.stage == WIFI_UDP_DONE) break;
    }
    assert(elapsed < 11000000u && net.stage == WIFI_UDP_DONE);
    assert(net.sent == 100u && net.lost == 100u && net.received == 0u && !net.pending);
    assert(net.rtt_sum == 0u && net.rtt_min == 0u && net.rtt_max == 0u);
    assert(net.dropped == 0u && net.duplicates == 0u);
    for (uint32_t i = 0u; i < 100u; ++i) assert(net.samples[i] == 0u);
    assert(net.arp_rx >= 5u);

    /* A peer disappearing during cache refresh fails within another 250k ticks. */
    learn(&net, 0u, start);
    uint32_t expiry = net.arp_expires;
    for (uint32_t i = 0u; i < 5u; ++i) {
        assert(wifi_udp_step(&net, expiry + i * 50000u, f, &length) == WIFI_UDP_SEND_FRAME);
        check_wire(f, length, 0u);
    }
    assert(wifi_udp_step(&net, expiry + 250000u, f, &length) == WIFI_UDP_ERROR);
    assert(net.stage == WIFI_UDP_FAILED && net.error == WIFI_UDP_ARP_TIMEOUT);
    assert(net.sent == 0u && length == 0u);
}

typedef struct {
    uint8_t used;
    uint8_t source;
    uint32_t delivery;
    uint32_t length;
    uint8_t frame[74];
} event_t;

typedef struct {
    wifi_udp_t net[2];
    event_t events[64];
    uint32_t first_arp[2][2];
    uint32_t post_done_echoes;
    uint32_t start;
    int faults;
} simulation_t;

static int request_lost(uint8_t source, uint32_t sequence) {
    return sequence % 11u == 0u || sequence % 19u == 0u ||
           (source == 1u && sequence % 23u == 0u);
}

static void enqueue(simulation_t *s, uint8_t source, const uint8_t *frame,
                      uint32_t length, uint32_t delivery, int corrupt) {
    for (uint32_t i = 0u; i < sizeof(s->events) / sizeof(s->events[0]); ++i) {
        event_t *event = &s->events[i];
        if (event->used) continue;
        assert(length <= sizeof(event->frame));
        event->used = 1u;
        event->source = source;
        event->delivery = delivery;
        event->length = length;
        memcpy(event->frame, frame, length);
        if (corrupt) event->frame[73] ^= 1u;
        return;
    }
    assert(0 && "simulation queue overflow");
}

static void transmit(simulation_t *s, uint8_t source, const uint8_t *frame,
                       uint32_t length, uint32_t now) {
    check_wire(frame, length, source);
    if (get16(frame + 12u) == 0x806u) {
        uint32_t kind = get16(frame + 20u) - 1u;
        if (s->faults && s->first_arp[source][kind]++ == 0u) return;
        enqueue(s, source, frame, length, now + 130u, 0);
        return;
    }
    uint32_t sequence = get_le32(frame + 50u);
    int request = frame[46] == 1u;
    if (request)
        assert(get_le32(frame + 54u) == s->start + now);
    if (!s->faults) {
        enqueue(s, source, frame, length, now + 130u, 0);
        return;
    }
    if (request) {
        if (sequence % 19u == 0u) {
            enqueue(s, source, frame, length, now + 130u, 1);
            return;
        }
        if (request_lost(source, sequence)) return;
        enqueue(s, source, frame, length, now + 130u, 0);
        if (sequence % 7u == 0u) enqueue(s, source, frame, length, now + 330u, 0);
    } else {
        if (sequence % 13u == 0u) return;
        if (sequence % 17u == 0u) {
            enqueue(s, source, frame, length, now + 101000u, 0);
            return;
        }
        enqueue(s, source, frame, length, now + 130u, 0);
        if (sequence % 5u == 0u) enqueue(s, source, frame, length, now + 330u, 0);
    }
}

static void deliver(simulation_t *s, uint32_t elapsed) {
    for (uint32_t i = 0u; i < sizeof(s->events) / sizeof(s->events[0]); ++i) {
        event_t event = s->events[i];
        if (!event.used || event.delivery > elapsed) continue;
        s->events[i].used = 0u;
        uint8_t target = (uint8_t)(event.source ^ 1u);
        wifi_udp_t *net = &s->net[target];
        wifi_udp_t before = *net;
        uint8_t reply[WIFI_UDP_FRAME_MAX];
        uint32_t length = UINT32_MAX;
        wifi_udp_result_t result = wifi_udp_receive(
            net, s->start + elapsed, event.frame, event.length, reply, &length);
        assert(result != WIFI_UDP_ERROR);
        for (uint32_t j = 0u; j < 100u; ++j)
            if (before.samples[j] != 0u) assert(net->samples[j] == before.samples[j]);
        if (net->received != before.received) {
            uint32_t sequence = get_le32(event.frame + 50u);
            assert(net->received == before.received + 1u);
            assert(event.frame[46] == 2u);
            assert(before.pending && before.pending_sequence == sequence);
            assert(net->samples[sequence - 1u] ==
                   s->start + elapsed - get_le32(event.frame + 54u));
        }
        if (result == WIFI_UDP_SEND_FRAME) {
            if (before.stage == WIFI_UDP_DONE && get16(reply + 12u) == 0x800u)
                ++s->post_done_echoes;
            transmit(s, target, reply, length, elapsed);
        } else {
            assert(length == 0u);
        }
    }
}

static void test_simulation(int faults, uint32_t start) {
    simulation_t simulation;
    memset(&simulation, 0, sizeof(simulation));
    simulation.start = start;
    simulation.faults = faults;
    init(&simulation.net[0], 0u, start);
    init(&simulation.net[1], 1u, start);
    uint32_t elapsed;
    uint32_t settled_at = UINT32_MAX;
    for (elapsed = 0u; elapsed < 14000000u; elapsed += 100u) {
        for (uint8_t role = 0u; role < 2u; ++role) {
            uint8_t frame[WIFI_UDP_FRAME_MAX];
            uint32_t length = UINT32_MAX;
            wifi_udp_t *net = &simulation.net[role];
            wifi_udp_result_t result = wifi_udp_step(net, start + elapsed, frame, &length);
            assert(result != WIFI_UDP_ERROR && result != WIFI_UDP_DROPPED);
            assert(net->sent == net->received + net->lost + net->pending);
            if (result == WIFI_UDP_SEND_FRAME)
                transmit(&simulation, role, frame, length, elapsed);
            else assert(length == 0u);
        }
        deliver(&simulation, elapsed);
        if (simulation.net[0].stage == WIFI_UDP_DONE &&
            simulation.net[1].stage == WIFI_UDP_DONE) {
            if (settled_at == UINT32_MAX) settled_at = elapsed;
            if (elapsed - settled_at >= 110000u) break;
        }
    }
    assert(elapsed < 14000000u);
    for (uint8_t role = 0u; role < 2u; ++role) {
        const wifi_udp_t *net = &simulation.net[role];
        uint32_t received = 0u, sum = 0u, min = UINT32_MAX, max = 0u;
        assert(net->stage == WIFI_UDP_DONE && net->error == WIFI_UDP_OK);
        assert(net->sent == 100u && !net->pending && net->received + net->lost == 100u);
        for (uint32_t sequence = 1u; sequence <= 100u; ++sequence) {
            uint32_t sample = net->samples[sequence - 1u];
            if (faults && (request_lost(role, sequence) ||
                           sequence % 13u == 0u || sequence % 17u == 0u))
                assert(sample == 0u);
            if (sample == 0u) continue;
            ++received;
            sum += sample;
            if (sample < min) min = sample;
            if (sample > max) max = sample;
            assert(sample < 100000u);
        }
        assert(net->received == received && net->rtt_sum == sum);
        assert(net->rtt_min == min && net->rtt_max == max);
        if (faults) {
            assert(net->lost >= 26u && net->received > 60u && net->duplicates > 15u);
            assert(net->dropped > net->duplicates && net->arp_tx >= 3u);
        } else {
            assert(net->received == 100u && net->lost == 0u && net->echoed == 100u);
            assert(net->dropped == 0u && net->duplicates == 0u && net->arp_rx == 2u);
        }
    }
    if (faults) assert(simulation.post_done_echoes != 0u);
}

static void test_bounded_malformed_corpus(void) {
    wifi_udp_t net;
    uint8_t f[WIFI_UDP_FRAME_MAX], reply[WIFI_UDP_FRAME_MAX];
    uint32_t state = 0x12345678u;
    learn(&net, 0u, 0u);
    for (uint32_t iteration = 0u; iteration < 5000u; ++iteration) {
        for (uint32_t j = 0u; j < sizeof(f); ++j) {
            state = state * 1664525u + 1013904223u;
            f[j] = (uint8_t)(state >> 24u);
        }
        if (iteration & 1u) {
            memcpy(f, macs[0], 6u);
            memcpy(f + 6u, macs[1], 6u);
            set16(f + 12u, iteration & 2u ? 0x800u : 0x806u);
        }
        uint32_t length = iteration % (WIFI_UDP_FRAME_MAX + 1u), output = UINT32_MAX;
        uint32_t dropped = net.dropped;
        assert(wifi_udp_receive(&net, 0u, f, length, reply, &output) == WIFI_UDP_DROPPED);
        assert(output == 0u && net.dropped == dropped + 1u);
        assert(net.last_drop != WIFI_UDP_DROP_NONE);
        assert(net.sent == 0u && net.received == 0u && net.echoed == 0u);
    }
}

int main(void) {
    test_arp_refresh(0u);
    test_arp_refresh(0xffff0000u);
    test_config();
    test_arp();
    test_bad_arp();
    test_golden_and_rtt();
    test_bad_udp();
    test_checksum_edges();
    test_pending_losses_and_done();
    test_all_pings_lost();
    test_simulation(0, 0u);
    test_simulation(0, UINT32_MAX - 40000u);
    test_simulation(1, 0u);
    test_simulation(1, UINT32_MAX - 40000u);
    test_bounded_malformed_corpus();
    puts("wifi_udp_test: PASS");
    return 0;
}

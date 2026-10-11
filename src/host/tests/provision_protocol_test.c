#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../sdk/wifi_handover/provision_protocol.h"

static handover_credentials_t example(void) {
    handover_credentials_t c = {
        .session_id = 0x12345678u,
        .token = UINT64_C(0xfedcba9876543210),
        .slot = 2u,
        .channel = 6u,
        .ssid = "U26W-TEST",
        .password = "0123456789abcdef0123456789ABCDEF"
    };
    return c;
}

static void assert_equal(const handover_credentials_t *a,
                         const handover_credentials_t *b) {
    assert(a->session_id == b->session_id && a->token == b->token);
    assert(a->slot == b->slot && a->channel == b->channel);
    assert(strcmp(a->ssid, b->ssid) == 0);
    assert(strcmp(a->password, b->password) == 0);
}

static void reject_unchanged(const uint8_t *wire, size_t length) {
    handover_credentials_t out, before;
    memset(&out, 0xa5, sizeof(out));
    memcpy(&before, &out, sizeof(out));
    assert(!provision_decode(wire, length, &out));
    assert(memcmp(&out, &before, sizeof(out)) == 0);
}

static void round_trip(void) {
    handover_credentials_t c = example(), out;
    uint8_t wire[PROVISION_WIRE_SIZE + 1u];
    memset(wire, 0xaa, sizeof(wire));
    assert(provision_encode(&c, wire, sizeof(wire)));
    assert(wire[PROVISION_WIRE_SIZE] == 0xaau);
    assert(wire[0] == 1u && wire[1] == 73u && wire[2] == 2u && wire[3] == 6u);
    assert(wire[4] == 0x78u && wire[7] == 0x12u);
    assert(wire[8] == 0x10u && wire[15] == 0xfeu);
    assert(provision_decode(wire, PROVISION_WIRE_SIZE, &out));
    assert_equal(&c, &out);
    for (unsigned slot = 1u; slot <= 3u; ++slot) {
        for (unsigned channel = 1u; channel <= 11u; ++channel) {
            c.slot = (uint8_t)slot;
            c.channel = (uint8_t)channel;
            memset(c.ssid, '!', sizeof(c.ssid) - 1u);
            c.ssid[sizeof(c.ssid) - 1u] = '\0';
            assert(provision_encode(&c, wire, sizeof(wire)));
            assert(provision_decode(wire, PROVISION_WIRE_SIZE, &out));
            assert_equal(&c, &out);
        }
    }
}

static void malformed_wire(void) {
    handover_credentials_t c = example();
    uint8_t good[PROVISION_WIRE_SIZE], bad[PROVISION_WIRE_SIZE + 1u];
    assert(provision_encode(&c, good, sizeof(good)));
    for (size_t n = 0u; n < sizeof(good); ++n) reject_unchanged(good, n);
    memcpy(bad, good, sizeof(good));
    reject_unchanged(bad, sizeof(bad));
    reject_unchanged(NULL, sizeof(good));
    assert(!provision_decode(good, sizeof(good), NULL));
    static const struct { unsigned offset; uint8_t value; } invalid[] = {
        {0, 0}, {0, 2}, {0, 255}, {1, 0}, {1, 72}, {1, 74},
        {2, 0}, {2, 4}, {2, 255}, {3, 0}, {3, 12}, {3, 255},
        {16, 0}, {16, 0x1f}, {16, 0x7f}, {16, 0x80}, {39, 'x'},
        {40, 0}, {40, 'g'}, {40, '/'}, {40, ':'}, {40, 'G'}, {72, 'x'}
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        memcpy(bad, good, sizeof(good));
        bad[invalid[i].offset] = invalid[i].value;
        reject_unchanged(bad, sizeof(good));
    }
    memcpy(bad, good, sizeof(good));
    memset(bad + 4u, 0, 4u);
    reject_unchanged(bad, sizeof(good));
    memcpy(bad, good, sizeof(good));
    memset(bad + 8u, 0, 8u);
    reject_unchanged(bad, sizeof(good));
    memcpy(bad, good, sizeof(good));
    memset(bad + 16u, 'a', 24u);
    reject_unchanged(bad, sizeof(good));
}

static void malformed_dto(void) {
    handover_credentials_t good = example(), c;
    uint8_t wire[PROVISION_WIRE_SIZE], before[PROVISION_WIRE_SIZE];
    memset(wire, 0xa5, sizeof(wire));
    memcpy(before, wire, sizeof(wire));
    assert(!provision_encode(NULL, wire, sizeof(wire)));
    assert(!provision_encode(&good, NULL, sizeof(wire)));
    assert(!provision_encode(&good, wire, sizeof(wire) - 1u));
    for (unsigned test = 0; test < 12u; ++test) {
        c = good;
        switch (test) {
        case 0: c.session_id = 0; break;
        case 1: c.token = 0; break;
        case 2: c.slot = 0; break;
        case 3: c.slot = 4; break;
        case 4: c.channel = 0; break;
        case 5: c.channel = 12; break;
        case 6: c.ssid[0] = '\0'; break;
        case 7: memset(c.ssid, 's', sizeof(c.ssid)); break;
        case 8: c.ssid[0] = '\n'; break;
        case 9: c.password[0] = 'g'; break;
        case 10: c.password[31] = '\0'; break;
        case 11: c.password[32] = 'x'; break;
        }
        assert(!provision_encode(&c, wire, sizeof(wire)));
        assert(memcmp(wire, before, sizeof(wire)) == 0);
    }
    c = good;
    memset(c.ssid + 10u, 'x', sizeof(c.ssid) - 10u);
    assert(provision_encode(&c, wire, sizeof(wire)));
    for (size_t i = 16u + strlen(good.ssid); i < 40u; ++i) assert(wire[i] == 0u);
}

static void fragments(void) {
    handover_credentials_t c = example(), out;
    uint8_t wire[PROVISION_WIRE_SIZE];
    assert(provision_encode(&c, wire, sizeof(wire)));
    provision_rx_t rx;
    /* Includes default ATT MTU 23 => read/blob payload 22 bytes. */
    for (size_t chunk = 1u; chunk <= sizeof(wire); ++chunk) {
        provision_rx_init(&rx);
        for (size_t offset = 0u; offset < sizeof(wire); offset += chunk) {
            assert(!provision_rx_finish(&rx, true, c.session_id, &out));
            size_t n = sizeof(wire) - offset;
            if (n > chunk) n = chunk;
            assert(provision_rx_append(&rx, offset, wire + offset, n));
        }
        assert(!provision_rx_finish(&rx, false, c.session_id, &out));
        assert(!provision_rx_finish(&rx, true, c.session_id + 1u, &out));
        assert(!provision_rx_finish(&rx, true, 0u, &out));
        assert(provision_rx_finish(&rx, true, c.session_id, &out));
        assert_equal(&c, &out);
        assert(provision_rx_append(&rx, sizeof(wire), NULL, 0u));
        assert(!provision_rx_append(&rx, sizeof(wire), wire, 1u));
        assert(!provision_rx_finish(&rx, true, c.session_id, &out));
    }
    provision_rx_init(&rx);
    assert(!provision_rx_append(&rx, 0u, wire, 0u));
    provision_rx_init(&rx);
    assert(!provision_rx_append(&rx, 1u, wire, 1u));
    assert(!provision_rx_append(&rx, 0u, wire, 1u));
    provision_rx_init(&rx);
    assert(!provision_rx_append(&rx, 0u, wire, SIZE_MAX));
    provision_rx_init(&rx);
    assert(!provision_rx_append(&rx, SIZE_MAX, wire, 1u));
    provision_rx_init(&rx);
    assert(provision_rx_append(&rx, 0u, wire, 22u));
    assert(!provision_rx_append(&rx, 0u, wire, 22u));
    provision_rx_init(&rx);
    assert(!provision_rx_append(&rx, 0u, NULL, 1u));
    assert(!provision_rx_append(NULL, 0u, wire, 1u));
    assert(!provision_rx_finish(NULL, true, c.session_id, &out));
    provision_rx_init(NULL);
}

int main(void) {
    round_trip();
    malformed_wire();
    malformed_dto();
    fragments();
    puts("provision_protocol_test: PASS");
    return 0;
}

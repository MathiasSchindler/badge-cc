#include <assert.h>
#include <stdio.h>
#include "protocol.h"

static const uint8_t ids[3][8] = {{1u}, {2u}, {3u}};

static void deliver(pr_link_t *receiver, const pr_message_t *m, int expected, uint32_t now) {
    uint8_t frame[PR_MPDU];
    assert(pr_encode(frame, m));
    assert(pr_receive(receiver, frame, sizeof(frame), now) == expected);
}

static void roundtrip(uint8_t source, uint8_t destination, uint32_t now) {
    pr_link_t a, b, outsider;
    pr_init(&a, source, ids[source - 1u], 42u);
    pr_init(&b, destination, ids[destination - 1u], 51u);
    pr_init(&outsider, (uint8_t)(6u - source - destination),
            ids[6u - source - destination - 1u], 61u);
    uint8_t data[PR_DATA];
    for (uint32_t i = 0u; i < PR_DATA; ++i) data[i] = (uint8_t)(now + i * 19u);
    assert(!pr_begin(&a, source, data, now));
    assert(!pr_begin(&a, 0u, data, now));
    assert(!pr_begin(&a, 4u, data, now));
    assert(pr_begin(&a, destination, data, now));
    assert(!pr_begin(&a, destination, data, now));
    pr_message_t request, reply;
    assert(pr_next(&a, now, &request));
    deliver(&outsider, &request, 0, now);
    deliver(&b, &request, 1, now);
    assert(!pr_next(&b, now + 2999u, &reply));
    assert(pr_next(&b, now + 3000u, &reply));
    pr_message_t wrong = reply;
    wrong.data[0] ^= 1u;
    deliver(&a, &wrong, 0, now + 3001u);
    wrong = reply; ++wrong.session;
    deliver(&a, &wrong, 0, now + 3001u);
    wrong = reply; ++wrong.sequence;
    deliver(&a, &wrong, 0, now + 3001u);
    /* A lost reply must elicit another reply without another data delivery. */
    deliver(&b, &request, 3, now + 4000u);
    assert(pr_next(&b, now + 7000u, &reply));
    deliver(&a, &reply, 2, now + 7001u);
    deliver(&a, &reply, 0, now + 7002u);
    assert(a.confirmed == 1u && !a.active && !a.failed);
    assert(a.first_tx_at==now && a.completed_at-a.began_at==7001u);
    assert(a.completed_at-a.first_tx_at==7001u);
    assert(b.requests == 1u && b.duplicates == 1u);
    uint8_t frame[PR_MPDU];
    assert(pr_encode(frame, &request));
    for (uint32_t i = 0u; i < sizeof(frame); ++i) {
        uint8_t broken[PR_MPDU];
        badger_bytes_copy(broken, frame, sizeof(frame));
        broken[i] ^= 1u;
        pr_message_t decoded;
        assert(!pr_decode(&decoded, broken, sizeof(broken)));
        if (i < 76u) {
            badger_bytes_store_u32(broken + 76u, diag_crc32(broken, 76u));
            if (i < 37u || (i >= 70u && i < 76u))
                assert(!pr_decode(&decoded, broken, sizeof(broken)));
        }
    }
    pr_message_t decoded;
    assert(!pr_decode(&decoded, frame, sizeof(frame) - 1u));
    uint8_t packet[PR_PACKET] = {0};
    packet[15] = clean_ofdm_rates[0].signal;
    packet[16] = CLEAN_RX_OFDM | PR_CHANNEL;
    assert(pr_metadata(packet));
    packet[16] ^= 1u; assert(!pr_metadata(packet));
    packet[16] = PR_CHANNEL; assert(!pr_metadata(packet));
}

int main(void) {
    for (uint8_t a = 1u; a <= 3u; ++a)
        for (uint8_t b = 1u; b <= 3u; ++b) if (a != b) {
            roundtrip(a, b, 100u);
            roundtrip(a, b, 0xfffff000u);
        }
    pr_link_t pair[2];
    pr_message_t messages[2], replies[2];
    uint8_t simultaneous[PR_DATA] = {0xabu};
    for (uint32_t i = 0u; i < 2u; ++i) {
        pr_init(pair + i, (uint8_t)(i + 1u), ids[i], i + 1u);
        assert(pr_begin(pair + i, (uint8_t)(2u - i), simultaneous, 0u));
        assert(pr_next(pair + i, 0u, messages + i));
    }
    for (uint32_t i = 0u; i < 2u; ++i) {
        deliver(pair + i, messages + (1u - i), 1, 10u);
        /* A full reply slot is explicit backpressure, not a false data delivery. */
        deliver(pair + i, messages + (1u - i), 0, 11u);
        assert(pair[i].reply_drops == 1u);
        assert(pr_next(pair + i, 3010u, replies + i));
    }
    for (uint32_t i = 0u; i < 2u; ++i) {
        deliver(pair + i, replies + (1u - i), 2, 4000u);
        assert(pair[i].confirmed == 1u && pair[i].requests == 1u);
        pr_message_t stale = messages[1u - i];
        stale.sequence = 0xffffffffu;
        deliver(pair + i, &stale, 0, 4001u);
        ++stale.session; stale.sequence = 1u;
        deliver(pair + i, &stale, 1, 4002u);
    }
    pr_link_t link;
    pr_init(&link, 1u, ids[0], 1u);
    uint8_t data[PR_DATA] = {0};
    assert(pr_begin(&link, 2u, data, 0u));
    pr_message_t message;
    for (uint32_t i = 0u; i < PR_MAX_ATTEMPTS; ++i)
        assert(pr_next(&link, i * 267000u, &message));
    assert(!pr_next(&link, PR_MAX_ATTEMPTS * 267000u, &message));
    assert(link.failed == 1u && !link.active && !link.confirmed);
    link.sequence = 0xffffffffu;
    assert(!pr_begin(&link, 2u, data, 0u));
    puts("PASS: six directed links, timer wrap, exact frames/FCS, metadata, stale replies, "
         "duplicate re-ACK, retry exhaustion and sequence exhaustion");
    return 0;
}

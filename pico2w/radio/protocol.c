#include "protocol.h"

static int valid(const pr_message_t *m) {
    uint8_t any = 0u;
    for (uint32_t i = 0u; i < 8u; ++i) any |= m->sender[i];
    return (m->kind == PR_REQUEST || m->kind == PR_REPLY) &&
        m->source >= 1u && m->source <= 3u &&
        m->destination >= 1u && m->destination <= 3u &&
        m->source != m->destination && m->session && m->sequence && any;
}

int pr_encode(uint8_t mpdu[PR_MPDU], const pr_message_t *m) {
    if (!valid(m)) return 0;
    badger_bytes_zero(mpdu, PR_MPDU);
    mpdu[0] = 0x08u;
    for (uint32_t i = 0u; i < 6u; ++i) mpdu[4u + i] = 255u;
    mpdu[10] = 2u;
    badger_bytes_copy(mpdu + 11u, m->sender, 5u);
    static const uint8_t network[6] = {2u, 'P', '2', 'W', 'R', 1u};
    static const uint8_t llc[8] = {0xaau, 0xaau, 3u, 0u, 0u, 0u, 0x88u, 0xb5u};
    badger_bytes_copy(mpdu + 16u, network, 6u);
    mpdu[22] = (uint8_t)(m->sequence << 4u);
    mpdu[23] = (uint8_t)(m->sequence >> 4u);
    badger_bytes_copy(mpdu + 24u, llc, 8u);
    badger_bytes_store_u32(mpdu + 32u, PR_TRACE_MAGIC);
    mpdu[36] = 1u;
    mpdu[37] = m->kind;
    mpdu[38] = m->source;
    mpdu[39] = m->destination;
    badger_bytes_copy(mpdu + 40u, m->sender, 8u);
    badger_bytes_store_u32(mpdu + 48u, m->session);
    badger_bytes_store_u32(mpdu + 52u, m->sequence);
    badger_bytes_copy(mpdu + 56u, m->data, PR_DATA);
    badger_bytes_store_u32(mpdu + 76u, diag_crc32(mpdu, 76u));
    return 1;
}

int pr_decode(pr_message_t *m, const uint8_t *mpdu, uint32_t size) {
    if (size != PR_MPDU || !clean_frame_valid(mpdu, size)) return 0;
    pr_message_t candidate;
    candidate.kind = mpdu[37];
    candidate.source = mpdu[38];
    candidate.destination = mpdu[39];
    badger_bytes_copy(candidate.sender, mpdu + 40u, 8u);
    candidate.session = badger_bytes_load_u32(mpdu + 48u);
    candidate.sequence = badger_bytes_load_u32(mpdu + 52u);
    badger_bytes_copy(candidate.data, mpdu + 56u, PR_DATA);
    uint8_t expected[PR_MPDU];
    if (!pr_encode(expected, &candidate) ||
        !badger_bytes_equal(expected, mpdu, PR_MPDU)) return 0;
    badger_bytes_copy((uint8_t *)m, (const uint8_t *)&candidate, sizeof(candidate));
    return 1;
}

int pr_metadata(const uint8_t packet[PR_PACKET]) {
    return clean_ofdm_rate_index(packet[15]) == 0 &&
        packet[16] == (CLEAN_RX_OFDM | PR_CHANNEL);
}

void pr_init(pr_link_t *l, uint8_t node, const uint8_t id[8], uint32_t session) {
    badger_bytes_zero((uint8_t *)l, sizeof(*l));
    l->node = node;
    l->session = session;
    badger_bytes_copy(l->id, id, 8u);
}

int pr_begin(pr_link_t *l, uint8_t destination, const uint8_t data[PR_DATA], uint32_t now) {
    if (l->active || destination < 1u || destination > 3u ||
        destination == l->node || l->sequence == 0xffffffffu) return 0;
    pr_message_t *m = &l->pending;
    m->kind = PR_REQUEST;
    m->source = l->node;
    m->destination = destination;
    m->session = l->session;
    m->sequence = ++l->sequence;
    badger_bytes_copy(m->sender, l->id, 8u);
    badger_bytes_copy(m->data, data, PR_DATA);
    l->active = 1u;
    l->attempts = 0u;
    l->retry_at = now;
    l->began_at = now; l->first_tx_at = l->completed_at = 0u;
    return 1;
}

int pr_receive(pr_link_t *l, const uint8_t *mpdu, uint32_t size, uint32_t now) {
    pr_message_t m;
    if (!pr_decode(&m, mpdu, size) || m.destination != l->node ||
        badger_bytes_equal(m.sender, l->id, 8u)) return 0;
    if (m.kind == PR_REPLY) {
        if (!l->active || m.source != l->pending.destination ||
            m.session != l->pending.session || m.sequence != l->pending.sequence ||
            !badger_bytes_equal(m.data, l->pending.data, PR_DATA)) return 0;
        l->active = 0u;
        l->completed_at = now;
        ++l->confirmed;
        return 2;
    }
    uint8_t peer = m.source;
    int duplicate = l->seen_session[peer] == m.session;
    if (duplicate && m.sequence != l->seen_sequence[peer]) {
        if (m.sequence - l->seen_sequence[peer] >= 0x80000000u) return 0;
        duplicate = 0;
    }
    if (l->reply_pending) {
        ++l->reply_drops;
        return 0;
    }
    l->seen_session[peer] = m.session;
    l->seen_sequence[peer] = m.sequence;
    if (duplicate) ++l->duplicates;
    else ++l->requests;
    badger_bytes_copy((uint8_t *)&l->reply, (const uint8_t *)&m, sizeof(m));
    l->reply.kind = PR_REPLY;
    l->reply.destination = m.source;
    l->reply.source = l->node;
    badger_bytes_copy(l->reply.sender, l->id, 8u);
    l->reply_pending = 1u;
    l->reply_at = now + 3000u;
    return duplicate ? 3 : 1;
}

int pr_next(pr_link_t *l, uint32_t now, pr_message_t *m) {
    if (l->reply_pending && now - l->reply_at < 0x80000000u) {
        badger_bytes_copy((uint8_t *)m, (const uint8_t *)&l->reply, sizeof(*m));
        l->reply_pending = 0u;
        ++l->replies;
        return 1;
    }
    if (!l->active || now - l->retry_at >= 0x80000000u) return 0;
    if (l->attempts == PR_MAX_ATTEMPTS) {
        l->active = 0u;
        ++l->failed;
        return 0;
    }
    badger_bytes_copy((uint8_t *)m, (const uint8_t *)&l->pending, sizeof(*m));
    if (!l->attempts) l->first_tx_at = now;
    ++l->attempts;
    l->retry_at = now + 250000u + 17000u * l->node;
    return 1;
}

#include "link.h"
#include "../src/ht_payload.h"

static int due(uint32_t now, uint32_t deadline) {
    return now - deadline < 0x80000000u;
}

int msg_encode(uint8_t frame[MSG_MPDU], const msg_frame_t *m) {
    if ((m->role != 1u && m->role != 2u) || !m->session || !m->sequence ||
        (m->kind != MSG_STATE && m->kind != MSG_ACK) || m->buttons > 31u ||
        m->origin > MSG_USB || (m->kind == MSG_STATE && m->target) ||
        (m->kind == MSG_ACK && !m->target) || (m->origin != MSG_USB && m->token))
        return 0;
    badger_bytes_zero(frame, MSG_MPDU);
    frame[0] = 8u;
    for (uint32_t i = 0u; i < 6u; ++i) frame[4u+i] = frame[16u+i] = 255u;
    static const uint8_t source[6] = {2u, 0x55u, 0x32u, 0x36u, 0u, 0u};
    badger_bytes_copy(frame+10u, source, sizeof(source));
    frame[15] = m->role;
    uint16_t seq = (uint16_t)((m->sequence & 4095u) << 4u);
    frame[22] = (uint8_t)seq; frame[23] = (uint8_t)(seq >> 8u);
    static const uint8_t llc[8] = {0xaau,0xaau,3u,0u,0u,0u,0x88u,0xb5u};
    badger_bytes_copy(frame+24u, llc, sizeof(llc));
    badger_bytes_store_u32(frame+32u, MSG_TRACE_MAGIC);
    frame[36] = 1u; frame[37] = m->role;
    frame[38] = m->kind; frame[39] = m->buttons;
    badger_bytes_store_u32(frame+40u, m->session);
    badger_bytes_store_u32(frame+44u, m->sequence);
    badger_bytes_store_u32(frame+48u, m->target);
    frame[52] = m->origin;
    badger_bytes_store_u32(frame+56u, m->token);
    badger_bytes_store_u32(frame+76u, clean_mac_fcs(frame, 76u));
    return 1;
}

int msg_decode(msg_frame_t *m, const uint8_t *frame, uint32_t size) {
    if (size != MSG_MPDU || !clean_mac_valid(frame, size) ||
        badger_bytes_load_u32(frame+32u) != MSG_TRACE_MAGIC || frame[36] != 1u)
        return 0;
    msg_frame_t decoded = {
        .role=frame[37], .kind=frame[38], .buttons=frame[39],
        .session=badger_bytes_load_u32(frame+40u),
        .sequence=badger_bytes_load_u32(frame+44u),
        .target=badger_bytes_load_u32(frame+48u),
        .origin=frame[52], .token=badger_bytes_load_u32(frame+56u),
    };
    uint8_t expected[MSG_MPDU];
    if (!msg_encode(expected, &decoded) || !badger_bytes_equal(expected, frame, size))
        return 0;
    *m = decoded;
    return 1;
}

int msg_template(uint8_t packet[MSG_STORAGE], const uint8_t frame[MSG_MPDU]) {
    msg_frame_t decoded;
    if (!msg_decode(&decoded, frame, MSG_MPDU)) return 0;
    badger_bytes_zero(packet, MSG_STORAGE);
    packet[0] = CLEAN_HT_DATA_VERSION;
    packet[2] = MSG_MCS;
    packet[4] = MSG_MPDU;
    if (!clean_ht20_sig(packet+8u, MSG_MPDU, MSG_MCS)) return 0;
    badger_bytes_copy(packet+14u, frame, MSG_MPDU);
    return clean_ht_payload_command_valid(
        CLEAN_HT_DATA_CMD | clean_tx_length_bits(MSG_MPDU+6u) | (1u<<19u),
        CLEAN_RX_HT_DATA, 0u, packet);
}

void msg_init(msg_link_t *l, uint8_t role, uint32_t session) {
    badger_bytes_zero((uint8_t *)l, sizeof(*l));
    l->role = role; l->session = session;
}

int msg_enqueue(msg_link_t *l, uint8_t buttons, uint8_t origin, uint32_t token,
                uint32_t now) {
    if (buttons > 31u || origin > MSG_USB || (origin != MSG_USB && token) ||
        l->count == MSG_QUEUE) {
        ++l->dropped;
        return 0;
    }
    msg_event_t *event = &l->queue[(l->head+l->count) % MSG_QUEUE];
    event->buttons=buttons; event->origin=origin; event->token=token;
    event->detected_at=event->queued_at=now;
    ++l->count;
    return 1;
}

void msg_buttons(msg_link_t *l, uint8_t raw, uint32_t now) {
    (void)diag_buttons_update(&l->debounce, raw & 31u, now);
    if (l->buttons != l->debounce.stable) {
        uint8_t changed=(uint8_t)(l->buttons ^ l->debounce.stable);
        uint32_t age=0u;
        for (uint32_t i=0u; i<5u; ++i)
            if ((changed & (1u<<i)) && now-l->debounce.changed_at[i]>age)
                age=now-l->debounce.changed_at[i];
        l->buttons = l->debounce.stable;
        if (msg_enqueue(l, l->buttons, MSG_BUTTON, 0u, now))
            l->queue[(l->head+l->count-1u)%MSG_QUEUE].detected_at=now-age;
    }
}

int msg_next(msg_link_t *l, uint32_t now, uint8_t frame[MSG_MPDU]) {
    if (l->ack_pending && due(now, l->ack_at)) {
        l->ack_pending=0u; ++l->tx_ack;
        if (l->reply.origin!=MSG_HEARTBEAT && l->receipt.sequence==l->reply.sequence &&
            l->receipt.session==l->reply.target && !(l->receipt.flags & 2u)) {
            l->receipt.ack_selected_at=now; l->receipt.flags|=2u;
        }
        return msg_encode(frame, &l->reply);
    }
    if (l->active && due(now, l->retry_at) && l->attempts == MSG_ATTEMPTS) {
        l->timing.completed_at=now; l->timing.outcome=2u;
        if (l->pending.origin!=MSG_HEARTBEAT)
            badger_bytes_copy((uint8_t *)&l->last_timing,(const uint8_t *)&l->timing,
                              sizeof(l->timing));
        ++l->failed; l->active=0u;
    }
    if (!l->active && l->count) {
        msg_event_t *event = &l->queue[l->head];
        if (++l->sequence == 0u) ++l->sequence;
        l->pending = (msg_frame_t){
            .role=l->role, .kind=MSG_STATE, .buttons=event->buttons,
            .origin=event->origin, .token=event->token,
            .session=l->session, .sequence=l->sequence,
        };
        l->head=(uint8_t)((l->head+1u)%MSG_QUEUE); --l->count;
        l->attempts=0u; l->active=1u; l->retry_at=now;
        l->timing=(msg_tx_time_t){
            .token=event->token, .sequence=l->sequence,
            .detected_at=event->detected_at, .queued_at=event->queued_at,
        };
    }
    if (!l->active || !due(now, l->retry_at)) return 0;
    ++l->attempts; ++l->tx_data;
    if (l->attempts==1u) l->timing.first_tx_at=now;
    l->timing.last_tx_at=now; l->timing.attempts=l->attempts;
    l->retry_at = now + MSG_RETRY_BASE + MSG_RETRY_ROLE*l->role +
        MSG_RETRY_SEQUENCE*(l->sequence%3u);
    return msg_encode(frame, &l->pending);
}

int msg_receive(msg_link_t *l, const uint8_t *frame, uint32_t size, uint32_t now) {
    msg_frame_t m;
    if (!msg_decode(&m, frame, size) || m.role == l->role) return 0;
    if (m.kind == MSG_ACK) {
        if (!l->active || m.target != l->session || m.sequence != l->pending.sequence ||
            m.buttons != l->pending.buttons || m.origin != l->pending.origin ||
            m.token != l->pending.token) return 0;
        ++l->rx_ack; l->active=0u; l->peer_at=now;
        l->timing.completed_at=now; l->timing.outcome=1u;
        if (l->pending.origin != MSG_HEARTBEAT) {
            l->last_acked=l->pending;
            badger_bytes_copy((uint8_t *)&l->last_timing,(const uint8_t *)&l->timing,
                              sizeof(l->timing));
        }
        return 3;
    }
    l->peer_at=now;
    l->reply=m; l->reply.kind=MSG_ACK; l->reply.role=l->role;
    l->reply.session=l->session; l->reply.target=m.session;
    if (!l->ack_pending) l->ack_at=now+MSG_ACK_DELAY_US;
    l->ack_pending=1u;
    if (l->peer_session == m.session &&
        (m.sequence == l->peer_sequence ||
         m.sequence-l->peer_sequence >= 0x80000000u)) {
        ++l->duplicates;
        if (m.origin!=MSG_HEARTBEAT && l->receipt.session==m.session &&
            l->receipt.sequence==m.sequence) ++l->receipt.duplicates;
        return 2;
    }
    uint8_t previous=l->peer_session==m.session?l->peer_buttons:0u;
    uint8_t pressed=(uint8_t)(m.buttons & ~previous);
    if (pressed) { l->last_press=pressed; ++l->peer_presses; }
    l->peer_session=m.session; l->peer_sequence=m.sequence;
    l->peer_buttons=m.buttons; ++l->rx_data;
    if (m.origin != MSG_HEARTBEAT) {
        l->last_event=m;
        l->receipt=(msg_rx_time_t){
            .token=m.token, .session=m.session, .sequence=m.sequence,
            .received_at=now, .flags=1u,
        };
    }
    return 1;
}

void msg_transmitted(msg_link_t *l, const uint8_t frame[MSG_MPDU], uint32_t now) {
    uint32_t sequence=badger_bytes_load_u32(frame+44u);
    if (frame[38]==MSG_STATE && l->active && sequence==l->pending.sequence) {
        l->timing.last_done_at=now;
        if (l->attempts==1u) { l->timing.first_done_at=now; l->timing.flags|=1u; }
    } else if (frame[38]==MSG_ACK && sequence==l->receipt.sequence &&
               badger_bytes_load_u32(frame+48u)==l->receipt.session &&
               (l->receipt.flags & 2u) && !(l->receipt.flags & 4u)) {
        l->receipt.ack_done_at=now; l->receipt.flags|=4u;
    }
}

void msg_displayed(msg_link_t *l, uint32_t now) {
    if ((l->receipt.flags & 1u) && !(l->receipt.flags & 8u)) {
        l->receipt.displayed_at=now; l->receipt.flags|=8u;
    }
}

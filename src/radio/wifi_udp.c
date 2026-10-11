#include "wifi_udp.h"
#include "../shared/bytes.h"

enum {
    ETH_HEADER = 14u, ARP_SIZE = 28u, IP_HEADER = 20u, UDP_HEADER = 8u,
    PAYLOAD_SIZE = 32u, ARP_FRAME_SIZE = 60u, UDP_FRAME_SIZE = 74u,
    ARP_INTERVAL = 50000u, ARP_ATTEMPTS = 5u, ARP_LIFETIME = 2000000u,
    ARP_REFRESH = 4u * ARP_INTERVAL,
    RUN_DEADLINE = 15000000u
};

static void copy_bytes(uint8_t *to, const uint8_t *from, uint32_t size) {
    badger_bytes_copy(to, from, size);
}

static int equal_bytes(const uint8_t *a, const uint8_t *b, uint32_t size) {
    return badger_bytes_equal(a, b, size);
}

static int zero_mac(const uint8_t *mac) {
    return (badger_bytes_load_u32(mac) | mac[4] | mac[5]) == 0u;
}

static int broadcast_mac(const uint8_t *mac) {
    return badger_bytes_load_u32(mac) == 0xffffffffu &&
           mac[4] == 255u && mac[5] == 255u;
}

static uint32_t be16(const uint8_t *p) {
    return (uint32_t)p[0] << 8u | (uint32_t)p[1];
}

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24u | (uint32_t)p[1] << 16u |
           (uint32_t)p[2] << 8u | (uint32_t)p[3];
}

static uint32_t le32(const uint8_t *p) {
    return badger_bytes_load_u32(p);
}

static void put_be16(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 8u);
    p[1] = (uint8_t)value;
}

static void put_be32(uint8_t *p, uint32_t value) {
    for (uint32_t i = 0u; i < 4u; ++i)
        p[i] = (uint8_t)(value >> (24u - 8u * i));
}

static void put_le32(uint8_t *p, uint32_t value) {
    badger_bytes_store_u32(p, value);
}

static uint32_t checksum_add(uint32_t sum, const uint8_t *p, uint32_t size) {
    while (size >= 4u) {
        uint32_t word = badger_bytes_load_u32(p);
        uint32_t pairs = ((word & 0x00ff00ffu) << 8u) |
                         ((word >> 8u) & 0x00ff00ffu);
        sum += (pairs & 65535u) + (pairs >> 16u);
        p += 4u;
        size -= 4u;
    }
    while (size >= 2u) {
        sum += be16(p);
        p += 2u;
        size -= 2u;
    }
    if (size != 0u) sum += (uint32_t)p[0] << 8u;
    return sum;
}

static uint32_t checksum_finish(uint32_t sum) {
    while (sum >> 16u) sum = (sum & 65535u) + (sum >> 16u);
    return (~sum) & 65535u;
}

static uint32_t udp_checksum(const uint8_t *ip, const uint8_t *udp,
                              uint32_t length) {
    uint32_t sum = checksum_add(17u + length, ip + 12u, 8u);
    return checksum_finish(checksum_add(sum, udp, length));
}

/* All scheduled intervals are below 2^31 ticks, including across timer wrap. */
static int due(uint32_t now, uint32_t when) {
    return (uint32_t)(now - when) < 0x80000000u;
}

static wifi_udp_result_t fail(wifi_udp_t *net, wifi_udp_error_t error) {
    if (net != 0) {
        net->error = error;
        net->stage = WIFI_UDP_FAILED;
    }
    return WIFI_UDP_ERROR;
}

static wifi_udp_result_t drop(wifi_udp_t *net, wifi_udp_drop_t reason) {
    net->last_drop = reason;
    ++net->dropped;
    return WIFI_UDP_DROPPED;
}

static int valid_state(const wifi_udp_t *net) {
    if (net->stage != WIFI_UDP_ARP && net->stage != WIFI_UDP_ECHO &&
        net->stage != WIFI_UDP_DONE) return 0;
    if (net->error != WIFI_UDP_OK || net->role > 1u ||
        zero_mac(net->mac) || zero_mac(net->peer_mac) ||
        (net->mac[0] & 1u) || (net->peer_mac[0] & 1u) ||
        equal_bytes(net->mac, net->peer_mac, 6u) ||
        net->ip != 0xc0a84d01u + net->role ||
        net->peer_ip != 0xc0a84d02u - net->role ||
        net->arp_ready > 1u || net->pending > 1u ||
        net->sent > WIFI_UDP_SAMPLES || net->received > net->sent ||
        net->lost > net->sent || net->arp_attempts > ARP_ATTEMPTS ||
        net->peer_sequence > WIFI_UDP_SAMPLES ||
        net->received + net->lost + net->pending != net->sent) return 0;
    if (net->pending && (net->pending_sequence != net->sent ||
                        net->pending_sequence == 0u)) return 0;
    if (net->stage == WIFI_UDP_ARP && (net->arp_ready || net->pending)) return 0;
    if (net->stage == WIFI_UDP_ECHO && !net->arp_ready) return 0;
    if (net->stage == WIFI_UDP_DONE &&
        (net->sent != WIFI_UDP_SAMPLES || net->pending)) return 0;
    return 1;
}

static wifi_udp_result_t check_call(wifi_udp_t *net) {
    if (net == 0) return WIFI_UDP_ERROR;
    if (net->stage == WIFI_UDP_FAILED && net->error != WIFI_UDP_OK)
        return WIFI_UDP_ERROR;
    if (!valid_state(net)) return fail(net, WIFI_UDP_BAD_CONFIG);
    return WIFI_UDP_NO_FRAME;
}

static void finish_if_settled(wifi_udp_t *net) {
    if (net->sent == WIFI_UDP_SAMPLES && !net->pending)
        net->stage = WIFI_UDP_DONE;
}

static wifi_udp_result_t advance_time(wifi_udp_t *net, uint32_t now) {
    if (net->stage == WIFI_UDP_DONE) return WIFI_UDP_NO_FRAME;
    if (due(now, net->deadline)) return fail(net, WIFI_UDP_DEADLINE);
    if (net->stage == WIFI_UDP_ARP && due(now, net->arp_expires))
        return fail(net, WIFI_UDP_ARP_TIMEOUT);
    if (net->pending &&
        (uint32_t)(now - net->pending_ticks) >= WIFI_UDP_TIMEOUT_TICKS) {
        net->pending = 0u;
        ++net->lost;
        net->last_rtt = 0u;
        finish_if_settled(net);
    }
    return WIFI_UDP_NO_FRAME;
}

static void ethernet(wifi_udp_t *net, uint8_t *frame, uint32_t type,
                      int broadcast) {
    for (uint32_t i = 0u; i < 6u; ++i)
        frame[i] = broadcast ? 255u : net->peer_mac[i];
    copy_bytes(frame + 6u, net->mac, 6u);
    put_be16(frame + 12u, type);
}

static void make_arp(wifi_udp_t *net, uint8_t *frame, int response) {
    badger_bytes_zero(frame, ARP_FRAME_SIZE);
    ethernet(net, frame, 0x0806u, !response);
    uint8_t *arp = frame + ETH_HEADER;
    put_be16(arp, 1u);
    put_be16(arp + 2u, 0x0800u);
    arp[4] = 6u;
    arp[5] = 4u;
    put_be16(arp + 6u, response ? 2u : 1u);
    copy_bytes(arp + 8u, net->mac, 6u);
    put_be32(arp + 14u, net->ip);
    if (response) copy_bytes(arp + 18u, net->peer_mac, 6u);
    put_be32(arp + 24u, net->peer_ip);
    ++net->arp_tx;
}

/* Byte 16+i = (17*sequence + 53*original_sender_role + 29*i) mod 256. */
static uint8_t pattern_start(uint32_t sequence, uint8_t role) {
    return (uint8_t)(17u * sequence + 53u * role);
}

static void make_udp(wifi_udp_t *net, uint8_t *frame, uint8_t type,
                      uint8_t origin, uint32_t sequence, uint32_t ticks) {
    badger_bytes_zero(frame, UDP_FRAME_SIZE);
    ethernet(net, frame, 0x0800u, 0);
    uint8_t *ip = frame + ETH_HEADER;
    uint8_t *udp = ip + IP_HEADER;
    uint8_t *p = udp + UDP_HEADER;
    ip[0] = 0x45u;
    put_be16(ip + 2u, IP_HEADER + UDP_HEADER + PAYLOAD_SIZE);
    put_be16(ip + 4u, sequence);
    put_be16(ip + 6u, 0x4000u);
    ip[8] = 64u;
    ip[9] = 17u;
    put_be32(ip + 12u, net->ip);
    put_be32(ip + 16u, net->peer_ip);
    put_be16(ip + 10u, checksum_finish(checksum_add(0u, ip, IP_HEADER)));
    put_be16(udp, WIFI_UDP_PORT);
    put_be16(udp + 2u, WIFI_UDP_PORT);
    put_be16(udp + 4u, UDP_HEADER + PAYLOAD_SIZE);
    p[0] = 'U';
    p[1] = '2';
    p[2] = '6';
    p[3] = 'E';
    p[4] = type;
    p[5] = origin;
    p[6] = PAYLOAD_SIZE;
    p[7] = 0u;
    put_le32(p + 8u, sequence);
    put_le32(p + 12u, ticks);
    uint8_t value = pattern_start(sequence, origin);
    for (uint32_t i = 0u; i < 16u; ++i) {
        p[16u + i] = value;
        value = (uint8_t)(value + 29u);
    }
    uint32_t sum = udp_checksum(ip, udp, UDP_HEADER + PAYLOAD_SIZE);
    put_be16(udp + 6u, sum == 0u ? 65535u : sum);
}

void wifi_udp_init(wifi_udp_t *net, uint8_t role, const uint8_t mac[6],
                    const uint8_t peer_mac[6], uint32_t now) {
    if (net == 0) return;
    uint8_t local[6], peer[6];
    if (mac != 0) copy_bytes(local, mac, 6u);
    if (peer_mac != 0) copy_bytes(peer, peer_mac, 6u);
    /* Volatile prevents freestanding compilers replacing this with memset. */
    volatile uint8_t *bytes = (volatile uint8_t *)net;
    for (uint32_t i = 0u; i < sizeof(*net); ++i) bytes[i] = 0u;
    if (mac == 0 || peer_mac == 0 || role > 1u ||
        zero_mac(local) || zero_mac(peer) ||
        (local[0] & 1u) || (peer[0] & 1u) || equal_bytes(local, peer, 6u)) {
        (void)fail(net, WIFI_UDP_BAD_CONFIG);
        return;
    }
    net->role = role;
    copy_bytes(net->mac, local, 6u);
    copy_bytes(net->peer_mac, peer, 6u);
    net->ip = 0xc0a84d01u + role;
    net->peer_ip = 0xc0a84d02u - role;
    net->stage = WIFI_UDP_ARP;
    net->started = now;
    net->next_due = now;
    net->deadline = now + RUN_DEADLINE;
    net->arp_expires = now + ARP_ATTEMPTS * ARP_INTERVAL;
}

wifi_udp_result_t wifi_udp_step(wifi_udp_t *net, uint32_t now,
                                uint8_t frame[WIFI_UDP_FRAME_MAX], uint32_t *length) {
    if (length != 0) *length = 0u;
    if (frame == 0 || length == 0) return fail(net, WIFI_UDP_BAD_CONFIG);
    if (check_call(net) == WIFI_UDP_ERROR) return WIFI_UDP_ERROR;
    if (advance_time(net, now) == WIFI_UDP_ERROR) return WIFI_UDP_ERROR;
    if (net->stage == WIFI_UDP_DONE || net->pending) return WIFI_UDP_NO_FRAME;
    if (net->arp_ready && due(now, net->arp_expires)) {
        net->arp_ready = 0u;
        net->arp_attempts = 0u;
        net->stage = WIFI_UDP_ARP;
        net->next_due = now;
        /* In ARP stage this field bounds discovery, not cache freshness. */
        net->arp_expires = now + ARP_ATTEMPTS * ARP_INTERVAL;
    }
    if (!due(now, net->next_due)) return WIFI_UDP_NO_FRAME;
    if (net->arp_ready && due(now, net->arp_expires - ARP_REFRESH) &&
        net->arp_attempts < ARP_ATTEMPTS) {
        make_arp(net, frame, 0);
        ++net->arp_attempts;
        net->next_due = now + ARP_INTERVAL;
        *length = ARP_FRAME_SIZE;
        return WIFI_UDP_SEND_FRAME;
    }
    if (net->stage == WIFI_UDP_ARP) {
        if (net->arp_attempts == ARP_ATTEMPTS) return fail(net, WIFI_UDP_ARP_TIMEOUT);
        make_arp(net, frame, 0);
        ++net->arp_attempts;
        net->next_due = now + ARP_INTERVAL;
        *length = ARP_FRAME_SIZE;
        return WIFI_UDP_SEND_FRAME;
    }
    if (net->sent >= WIFI_UDP_SAMPLES) {
        finish_if_settled(net);
        return WIFI_UDP_NO_FRAME;
    }
    ++net->sent;
    net->pending = 1u;
    net->pending_sequence = net->sent;
    net->pending_ticks = now;
    net->next_due = now + WIFI_UDP_INTERVAL_TICKS;
    make_udp(net, frame, 1u, net->role, net->pending_sequence, now);
    *length = UDP_FRAME_SIZE;
    return WIFI_UDP_SEND_FRAME;
}

static wifi_udp_result_t receive_arp(wifi_udp_t *net, uint32_t now,
                                     const uint8_t *frame, uint32_t length,
                                     uint8_t *reply, uint32_t *reply_length) {
    if (length < ETH_HEADER + ARP_SIZE) return drop(net, WIFI_UDP_DROP_LENGTH);
    const uint8_t *arp = frame + ETH_HEADER;
    uint32_t operation = be16(arp + 6u);
    if (be16(arp) != 1u || be16(arp + 2u) != 0x0800u ||
        arp[4] != 6u || arp[5] != 4u || (operation != 1u && operation != 2u))
        return drop(net, WIFI_UDP_DROP_PROTOCOL);
    if (!equal_bytes(arp + 8u, net->peer_mac, 6u) ||
        be32(arp + 14u) != net->peer_ip || be32(arp + 24u) != net->ip)
        return drop(net, WIFI_UDP_DROP_ADDRESS);
    if (operation == 1u) {
        /* Requests may be broadcast or unicast; THA must be zero or local. */
        if (!zero_mac(arp + 18u) && !equal_bytes(arp + 18u, net->mac, 6u))
            return drop(net, WIFI_UDP_DROP_ADDRESS);
    } else if (!equal_bytes(frame, net->mac, 6u) ||
               !equal_bytes(arp + 18u, net->mac, 6u)) {
        return drop(net, WIFI_UDP_DROP_ADDRESS);
    }
    ++net->arp_rx;
    net->arp_ready = 1u;
    net->arp_attempts = 0u;
    net->arp_expires = now + ARP_LIFETIME;
    if (net->stage == WIFI_UDP_ARP) {
        net->stage = WIFI_UDP_ECHO;
        net->next_due = now;
    }
    if (operation == 2u) return WIFI_UDP_NO_FRAME;
    make_arp(net, reply, 1);
    *reply_length = ARP_FRAME_SIZE;
    return WIFI_UDP_SEND_FRAME;
}

static wifi_udp_result_t receive_udp(wifi_udp_t *net, uint32_t now,
                                     const uint8_t *frame, uint32_t length,
                                     uint8_t *reply, uint32_t *reply_length) {
    if (length < ETH_HEADER + IP_HEADER) return drop(net, WIFI_UDP_DROP_LENGTH);
    const uint8_t *ip = frame + ETH_HEADER;
    if (ip[0] != 0x45u || ip[8] == 0u || ip[9] != 17u ||
        (be16(ip + 6u) & 0xbfffu) != 0u)
        return drop(net, WIFI_UDP_DROP_PROTOCOL);
    uint32_t ip_length = be16(ip + 2u);
    if (ip_length < IP_HEADER + UDP_HEADER || ip_length > length - ETH_HEADER)
        return drop(net, WIFI_UDP_DROP_LENGTH);
    if (checksum_finish(checksum_add(0u, ip, IP_HEADER)) != 0u)
        return drop(net, WIFI_UDP_DROP_CHECKSUM);
    if (!net->arp_ready || be32(ip + 12u) != net->peer_ip ||
        be32(ip + 16u) != net->ip)
        return drop(net, WIFI_UDP_DROP_ADDRESS);
    const uint8_t *udp = ip + IP_HEADER;
    uint32_t udp_length = be16(udp + 4u);
    if (udp_length < UDP_HEADER || udp_length != ip_length - IP_HEADER)
        return drop(net, WIFI_UDP_DROP_LENGTH);
    if (be16(udp) != WIFI_UDP_PORT || be16(udp + 2u) != WIFI_UDP_PORT)
        return drop(net, WIFI_UDP_DROP_PROTOCOL);
    /* Deliberately reject RFC-legal zero checksums for this narrow lab link. */
    if (be16(udp + 6u) == 0u || udp_checksum(ip, udp, udp_length) != 0u)
        return drop(net, WIFI_UDP_DROP_CHECKSUM);
    if (udp_length != UDP_HEADER + PAYLOAD_SIZE) return drop(net, WIFI_UDP_DROP_PAYLOAD);
    const uint8_t *p = udp + UDP_HEADER;
    uint32_t sequence = le32(p + 8u);
    uint32_t ticks = le32(p + 12u);
    uint8_t type = p[4], origin = p[5];
    if (p[0] != 'U' || p[1] != '2' || p[2] != '6' || p[3] != 'E' ||
        (type != 1u && type != 2u) ||
        origin != (type == 1u ? (uint8_t)(net->role ^ 1u) : net->role) ||
        p[6] != PAYLOAD_SIZE || p[7] != 0u ||
        sequence == 0u || sequence > WIFI_UDP_SAMPLES)
        return drop(net, WIFI_UDP_DROP_PAYLOAD);
    uint8_t value = pattern_start(sequence, origin);
    for (uint32_t i = 0u; i < 16u; ++i) {
        if (p[16u + i] != value)
            return drop(net, WIFI_UDP_DROP_PAYLOAD);
        value = (uint8_t)(value + 29u);
    }
    if (type == 1u) {
        if (sequence <= net->peer_sequence) {
            ++net->duplicates;
            return drop(net, WIFI_UDP_DROP_STALE);
        }
        net->peer_sequence = sequence;
        ++net->echoed;
        make_udp(net, reply, 2u, origin, sequence, ticks);
        *reply_length = UDP_FRAME_SIZE;
        return WIFI_UDP_SEND_FRAME;
    }
    if (!net->pending || sequence != net->pending_sequence || ticks != net->pending_ticks) {
        ++net->duplicates;
        return drop(net, WIFI_UDP_DROP_STALE);
    }
    uint32_t rtt = now - net->pending_ticks;
    if (rtt == 0u) rtt = 1u; /* Zero is reserved for loss in host reports. */
    net->samples[sequence - 1u] = rtt;
    net->last_rtt = rtt;
    net->rtt_sum += rtt;
    if (net->received == 0u || rtt < net->rtt_min) net->rtt_min = rtt;
    if (rtt > net->rtt_max) net->rtt_max = rtt;
    ++net->received;
    net->pending = 0u;
    finish_if_settled(net);
    return WIFI_UDP_NO_FRAME;
}

wifi_udp_result_t wifi_udp_receive(wifi_udp_t *net, uint32_t now,
                                   const uint8_t *frame, uint32_t length,
                                   uint8_t reply[WIFI_UDP_FRAME_MAX],
                                   uint32_t *reply_length) {
    if (reply_length != 0) *reply_length = 0u;
    if (frame == 0 || reply == 0 || reply_length == 0)
        return fail(net, WIFI_UDP_BAD_CONFIG);
    if (check_call(net) == WIFI_UDP_ERROR) return WIFI_UDP_ERROR;
    if (advance_time(net, now) == WIFI_UDP_ERROR) return WIFI_UDP_ERROR;
    if (length < ETH_HEADER || length > WIFI_UDP_FRAME_MAX)
        return drop(net, WIFI_UDP_DROP_LENGTH);
    uint32_t type = be16(frame + 12u);
    if (type != 0x0806u && type != 0x0800u) return drop(net, WIFI_UDP_DROP_PROTOCOL);
    if (!equal_bytes(frame + 6u, net->peer_mac, 6u) ||
        (!equal_bytes(frame, net->mac, 6u) &&
         (type != 0x0806u || !broadcast_mac(frame))))
        return drop(net, WIFI_UDP_DROP_ADDRESS);
    if (type == 0x0806u)
        return receive_arp(net, now, frame, length, reply, reply_length);
    return receive_udp(net, now, frame, length, reply, reply_length);
}

const char *wifi_udp_stage_name(wifi_udp_stage_t stage) {
    switch (stage) {
        case WIFI_UDP_IDLE: return "IDLE";
        case WIFI_UDP_ARP: return "ARP";
        case WIFI_UDP_ECHO: return "ECHO";
        case WIFI_UDP_DONE: return "DONE";
        case WIFI_UDP_FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

const char *wifi_udp_error_name(wifi_udp_error_t error) {
    switch (error) {
        case WIFI_UDP_OK: return "OK";
        case WIFI_UDP_BAD_CONFIG: return "BAD_CONFIG";
        case WIFI_UDP_ARP_TIMEOUT: return "ARP_TIMEOUT";
        case WIFI_UDP_TX_ERROR: return "TX_ERROR";
        case WIFI_UDP_DEADLINE: return "DEADLINE";
        case WIFI_UDP_RADIO_ERROR: return "RADIO_ERROR";
        default: return "UNKNOWN";
    }
}

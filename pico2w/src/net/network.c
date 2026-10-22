#include "network.h"

static uint16_t be16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] << 8u | p[1]);
}

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24u | (uint32_t)p[1] << 16u |
           (uint32_t)p[2] << 8u | p[3];
}

static void put16(uint8_t *p, uint16_t n) {
    p[0] = (uint8_t)(n >> 8u);
    p[1] = (uint8_t)n;
}

static void put32(uint8_t *p, uint32_t n) {
    put16(p, (uint16_t)(n >> 16u));
    put16(p + 2u, (uint16_t)n);
}

static void copy(uint8_t *to, const uint8_t *from, uint32_t n) {
    for (uint32_t i = 0u; i < n; ++i) to[i] = from[i];
}

static int equal(const uint8_t *a, const uint8_t *b, uint32_t n) {
    for (uint32_t i = 0u; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

static uint32_t sum(const uint8_t *p, uint32_t n, uint32_t total) {
    while (n >= 2u) {
        total += be16(p);
        p += 2u;
        n -= 2u;
    }
    if (n) total += (uint32_t)p[0] << 8u;
    return total;
}

static uint16_t finish(uint32_t total) {
    while (total >> 16u) total = (total & 65535u) + (total >> 16u);
    return (uint16_t)~total;
}

static uint16_t transport_sum(const uint8_t *ip, uint32_t length) {
    return finish(sum(ip + 20u, length, sum(ip + 12u, 8u, ip[9] + length)));
}

static void transmit(network_t *net, uint32_t length) {
    while (length < 60u) net->tx[length++] = 0u;
    if (net->ops.send(net->ops.context, net->tx, length) == 1) ++net->transmitted;
    else net->error = 2u;
}

static void ethernet(network_t *net, const uint8_t *mac, uint16_t type) {
    copy(net->tx, mac, 6u);
    copy(net->tx + 6u, net->mac, 6u);
    put16(net->tx + 12u, type);
}

static uint8_t *ipv4(network_t *net, const uint8_t *mac, uint32_t destination,
                       uint8_t protocol, uint16_t size) {
    ethernet(net, mac, 0x0800u);
    uint8_t *ip = net->tx + 14u;
    for (uint32_t i = 0u; i < 20u; ++i) ip[i] = 0u;
    ip[0] = 0x45u;
    put16(ip + 2u, (uint16_t)(20u + size));
    put16(ip + 4u, ++net->packet_id);
    put16(ip + 6u, 0x4000u);
    ip[8] = 64u;
    ip[9] = protocol;
    put32(ip + 12u, net->ip);
    put32(ip + 16u, destination);
    put16(ip + 10u, finish(sum(ip, 20u, 0u)));
    return ip + 20u;
}

static void address_text(network_t *net) {
    uint32_t position = 0u;
    for (uint32_t octet = 0u; octet < 4u; ++octet) {
        uint32_t value = (net->ip >> ((3u - octet) * 8u)) & 255u;
        if (value >= 100u) net->address[position++] = (char)('0' + value / 100u);
        if (value >= 10u) net->address[position++] = (char)('0' + value / 10u % 10u);
        net->address[position++] = (char)('0' + value % 10u);
        if (octet != 3u) net->address[position++] = '.';
    }
    net->address[position] = '\0';
}

static void clear_clients(network_t *net) {
    for (uint32_t i = 0u; i < NET_CLIENTS; ++i) net->clients[i].state = 0u;
}

static void dhcp_send(network_t *net) {
    static const uint8_t broadcast[6] = {255u, 255u, 255u, 255u, 255u, 255u};
    uint8_t *udp = ipv4(net, broadcast, 0xffffffffu, 17u, 308u);
    put16(udp, 68u);
    put16(udp + 2u, 67u);
    put16(udp + 4u, 308u);
    put16(udp + 6u, 0u);
    uint8_t *boot = udp + 8u;
    for (uint32_t i = 0u; i < 300u; ++i) boot[i] = 0u;
    boot[0] = 1u;
    boot[1] = 1u;
    boot[2] = 6u;
    put32(boot + 4u, net->xid);
    put16(boot + 10u, 0x8000u);
    if (net->dhcp_state == 4u) put32(boot + 12u, net->ip);
    copy(boot + 28u, net->mac, 6u);
    put32(boot + 236u, 0x63825363u);
    uint32_t n = 240u;
    boot[n++] = 53u;
    boot[n++] = 1u;
    boot[n++] = net->dhcp_state == 1u ? 1u : 3u;
    boot[n++] = 61u;
    boot[n++] = 7u;
    boot[n++] = 1u;
    copy(boot + n, net->mac, 6u);
    n += 6u;
    if (net->dhcp_state == 2u) {
        boot[n++] = 50u;
        boot[n++] = 4u;
        put32(boot + n, net->offered);
        n += 4u;
        boot[n++] = 54u;
        boot[n++] = 4u;
        put32(boot + n, net->server);
        n += 4u;
    }
    boot[n++] = 55u;
    boot[n++] = 4u;
    boot[n++] = 1u;
    boot[n++] = 3u;
    boot[n++] = 51u;
    boot[n++] = 54u;
    boot[n] = 255u;
    transmit(net, 342u);
    net->dhcp_at = net->now;
    ++net->dhcp_attempts;
}

static void dhcp_restart(network_t *net) {
    net->ip = 0u;
    net->offered = 0u;
    net->server = 0u;
    address_text(net);
    clear_clients(net);
    net->dhcp_state = 1u;
    net->dhcp_attempts = 0u;
    ++net->xid;
    dhcp_send(net);
}

static int unicast_ip(uint32_t ip) {
    return ip != 0u && (ip >> 24u) != 0u && (ip >> 24u) != 127u &&
           (ip >> 24u) < 224u;
}

static void dhcp_receive(network_t *net, const uint8_t *data, uint32_t length) {
    if (length < 240u || data[0] != 2u || data[1] != 1u || data[2] != 6u ||
        be32(data + 4u) != net->xid || !equal(data + 28u, net->mac, 6u) ||
        be32(data + 236u) != 0x63825363u) {
        ++net->dropped;
        return;
    }
    uint8_t type = 0u, ended = 0u;
    uint32_t server = 0u, lease = 0u;
    for (uint32_t n = 240u; n < length;) {
        uint8_t tag = data[n++];
        if (tag == 255u) {
            ended = 1u;
            break;
        }
        if (tag == 0u) continue;
        if (n == length) break;
        uint32_t size = data[n++];
        if (size > length - n) break;
        if (tag == 53u) {
            if (size != 1u || type != 0u) break;
            type = data[n];
        } else if (tag == 54u) {
            if (size != 4u || server != 0u) break;
            server = be32(data + n);
        } else if (tag == 51u) {
            if (size != 4u || lease != 0u) break;
            lease = be32(data + n);
        } else if (tag == 52u) break;
        n += size;
    }
    if (!ended || !unicast_ip(server)) {
        ++net->dropped;
        return;
    }
    uint32_t offered = be32(data + 16u);
    if (type == 2u && net->dhcp_state == 1u && unicast_ip(offered)) {
        net->server = server;
        net->offered = offered;
        net->dhcp_state = 2u;
        net->dhcp_attempts = 0u;
        dhcp_send(net);
    } else if (type == 5u && (net->dhcp_state == 2u || net->dhcp_state == 4u) &&
               server == net->server && lease >= 60u &&
               (offered == net->offered || (net->dhcp_state == 4u && offered == net->ip))) {
        net->ip = offered;
        net->lease_ms = (lease > 86400u ? 86400u : lease) * 1000u;
        net->lease_at = net->now;
        net->dhcp_state = 3u;
        net->error = 0u;
        address_text(net);
    } else if (type == 6u && server == net->server &&
               (net->dhcp_state == 2u || net->dhcp_state == 4u)) dhcp_restart(net);
    else ++net->dropped;
}

static void arp(network_t *net, const uint8_t *frame, uint32_t length) {
    if (!net->ip || length < 42u) return;
    const uint8_t *p = frame + 14u;
    if (be16(p) != 1u || be16(p + 2u) != 0x0800u || p[4] != 6u || p[5] != 4u ||
        !equal(p + 8u, frame + 6u, 6u)) {
        ++net->dropped;
        return;
    }
    if (be32(p + 14u) == net->ip && !equal(p + 8u, net->mac, 6u)) {
        net->error = 3u;
        net->ip = 0u;
        net->dhcp_state = 5u;
        clear_clients(net);
        address_text(net);
        return;
    }
    if (be16(p + 6u) != 1u || be32(p + 24u) != net->ip) return;
    ethernet(net, frame + 6u, 0x0806u);
    uint8_t *out = net->tx + 14u;
    copy(out, p, 8u);
    put16(out + 6u, 2u);
    copy(out + 8u, net->mac, 6u);
    put32(out + 14u, net->ip);
    copy(out + 18u, p + 8u, 6u);
    copy(out + 24u, p + 14u, 4u);
    transmit(net, 42u);
}

static void tcp_send(network_t *net, net_client_t *client, uint8_t flags,
                       uint16_t length, uint32_t sequence) {
    uint16_t header = flags & 2u ? 24u : 20u;
    uint8_t *tcp = ipv4(net, client->mac, client->ip, 6u, (uint16_t)(header + length));
    for (uint32_t i = 0u; i < header; ++i) tcp[i] = 0u;
    put16(tcp, 80u);
    put16(tcp + 2u, client->port);
    put32(tcp + 4u, sequence);
    put32(tcp + 8u, client->receive_next);
    tcp[12] = (uint8_t)(header << 2u);
    tcp[13] = flags;
    put16(tcp + 14u, (uint16_t)(HTTP_REQUEST_MAX - client->request_size));
    if (flags & 2u) {
        tcp[20] = 2u;
        tcp[21] = 4u;
        put16(tcp + 22u, 1024u);
    }
    copy(tcp + header, client->response + client->response_offset, length);
    put16(tcp + 16u, transport_sum(net->tx + 14u, header + length));
    transmit(net, 34u + header + length);
}

static void tcp_outstanding(network_t *net, net_client_t *client,
                             uint8_t flags, uint16_t length) {
    client->flags = flags;
    client->outstanding = length;
    client->send_next = client->send_acked + length + ((flags & 3u) != 0u ? 1u : 0u);
    client->sent_at = net->now;
    client->attempts = 1u;
    tcp_send(net, client, flags, length, client->send_acked);
}

static void tcp_next(network_t *net, net_client_t *client) {
    if (client->state != 2u || client->send_next != client->send_acked ||
        !client->response_size) return;
    if (client->response_offset < client->response_size) {
        uint32_t length = client->response_size - client->response_offset;
        if (length > client->mss) length = client->mss;
        if (length > client->window) length = client->window;
        if (length) tcp_outstanding(net, client, 0x18u, (uint16_t)length);
    } else {
        client->state = 3u;
        tcp_outstanding(net, client, 0x11u, 0u);
    }
}

static void tcp_receive(network_t *net, const uint8_t *frame, uint32_t ip_length) {
    const uint8_t *ip = frame + 14u, *tcp = ip + 20u;
    uint32_t length = ip_length - 20u;
    if (length < 20u || be16(tcp + 2u) != 80u ||
        transport_sum(ip, length) != 0u) {
        ++net->dropped;
        return;
    }
    uint32_t header = (uint32_t)(tcp[12] >> 4u) * 4u;
    if (header < 20u || header > length || (tcp[12] & 15u) || (tcp[13] & 0x20u)) {
        ++net->dropped;
        return;
    }
    uint32_t source = be32(ip + 12u), sequence = be32(tcp + 4u);
    uint32_t ack = be32(tcp + 8u);
    uint16_t port = be16(tcp), window = be16(tcp + 14u);
    uint8_t flags = tcp[13];
    net_client_t *client = 0, *free_client = 0;
    for (uint32_t i = 0u; i < NET_CLIENTS; ++i) {
        net_client_t *candidate = net->clients + i;
        if (candidate->state && candidate->ip == source && candidate->port == port)
            client = candidate;
        if (!candidate->state) free_client = candidate;
    }
    if ((flags & 0x17u) == 2u && length == header) {
        if (client) {
            if (client->state == 1u && sequence + 1u == client->receive_next)
                tcp_send(net, client, 0x12u, 0u, client->send_acked);
            return;
        }
        if (!free_client || port == 0u || !unicast_ip(source)) {
            ++net->dropped;
            return;
        }
        client = free_client;
        client->mss = 536u;
        for (uint32_t n = 20u; n < header;) {
            uint8_t kind = tcp[n++];
            if (kind == 0u) break;
            if (kind == 1u) continue;
            if (n == header) {
                ++net->dropped;
                return;
            }
            uint8_t size = tcp[n++];
            if (size < 2u || size - 2u > header - n) {
                ++net->dropped;
                return;
            }
            if (kind == 2u && size == 4u) {
                uint16_t mss = be16(tcp + n);
                if (mss < 64u) {
                    ++net->dropped;
                    return;
                }
                client->mss = mss < 1024u ? mss : 1024u;
            }
            n += size - 2u;
        }
        client->state = 1u;
        client->ip = source;
        client->port = port;
        client->window = window;
        client->receive_next = sequence + 1u;
        net->isn += 64001u;
        client->send_acked = net->isn;
        client->request_size = client->response_size = client->response_offset = 0u;
        client->active_at = net->now;
        copy(client->mac, frame + 6u, 6u);
        tcp_outstanding(net, client, 0x12u, 0u);
        return;
    }
    if (!client) {
        ++net->dropped;
        return;
    }
    if (flags & 4u) {
        if (sequence == client->receive_next) client->state = 0u;
        return;
    }
    if (flags & 2u || !(flags & 16u)) {
        ++net->dropped;
        return;
    }
    if (sequence != client->receive_next) {
        tcp_send(net, client, 16u, 0u, client->send_next);
        return;
    }
    if (ack == client->send_next) {
        client->response_offset += client->outstanding;
        client->outstanding = 0u;
        client->send_acked = ack;
        if (client->state == 1u) client->state = 2u;
        else if (client->state == 3u) {
            client->state = 4u;
            client->active_at = net->now;
        }
    } else if (ack != client->send_acked) {
        ++net->dropped;
        return;
    }
    client->window = window;
    uint32_t payload = length - header;
    if (payload && client->state == 2u && !client->response_size) {
        if (payload > HTTP_REQUEST_MAX - client->request_size) {
            client->state = 0u;
            tcp_send(net, client, 0x14u, 0u, client->send_next);
            ++net->dropped;
            return;
        }
        copy(client->request + client->request_size, tcp + header, payload);
        client->request_size += payload;
        client->receive_next += payload;
        client->active_at = net->now;
        http_request_t request;
        int parsed = http_parse(client->request, client->request_size, net->address, &request);
        if (parsed != 0) {
            if (parsed < 0) {
                static const uint8_t bad[] = "Malformed or unsupported HTTP request";
                client->response_size = http_response(client->response, HTTP_RESPONSE_MAX,
                                                       400u, "text/plain", bad, sizeof(bad) - 1u);
            } else {
                ++net->http_requests;
                client->response_size = net->ops.http(net->ops.context, &request,
                                                       client->response, HTTP_RESPONSE_MAX);
            }
            if (!client->response_size || client->response_size > HTTP_RESPONSE_MAX) {
                static const uint8_t failure[] = "Response generation failed";
                client->response_size = http_response(client->response, HTTP_RESPONSE_MAX,
                                                       500u, "text/plain", failure, sizeof(failure) - 1u);
            }
        }
    } else if (payload) {
        tcp_send(net, client, 16u, 0u, client->send_next);
        return;
    }
    if (flags & 1u) {
        ++client->receive_next;
        client->active_at = net->now;
        tcp_send(net, client, 16u, 0u, client->send_next);
        if (!client->response_size && client->state == 2u) {
            client->state = 3u;
            tcp_outstanding(net, client, 0x11u, 0u);
        }
    } else if (payload && (!client->response_size || client->send_next != client->send_acked ||
                            client->window == 0u))
        tcp_send(net, client, 16u, 0u, client->send_next);
    tcp_next(net, client);
}

void network_init(network_t *net, const net_ops_t *ops, const uint8_t mac[6],
                    uint32_t seed, uint32_t now) {
    net->ops = *ops;
    copy(net->mac, mac, 6u);
    net->now = now;
    net->isn = seed;
    net->xid = seed ^ 0x5032574cu;
    net->error = 0u;
    net->received = net->dropped = net->transmitted = net->retransmits = net->http_requests = 0u;
    net->packet_id = 0u;
    dhcp_restart(net);
}

void network_input(network_t *net, const uint8_t *frame, uint32_t length) {
    static const uint8_t broadcast[6] = {255u, 255u, 255u, 255u, 255u, 255u};
    ++net->received;
    if (length < 14u || length > NET_FRAME_MAX ||
        (!equal(frame, net->mac, 6u) && !equal(frame, broadcast, 6u)) || (frame[6] & 1u)) {
        ++net->dropped;
        return;
    }
    uint16_t type = be16(frame + 12u);
    if (type == 0x0806u) {
        arp(net, frame, length);
        return;
    }
    if (type != 0x0800u || length < 34u) {
        ++net->dropped;
        return;
    }
    const uint8_t *ip = frame + 14u;
    uint32_t ip_length = be16(ip + 2u);
    if (ip[0] != 0x45u || ip_length < 20u || ip_length > length - 14u ||
        (be16(ip + 6u) & 0xbfffu) || !ip[8] ||
        finish(sum(ip, 20u, 0u)) != 0u) {
        ++net->dropped;
        return;
    }
    uint32_t destination = be32(ip + 16u);
    uint8_t initial_dhcp = ip[9] == 17u && !net->ip &&
                           (net->dhcp_state == 1u || net->dhcp_state == 2u);
    if (destination != 0xffffffffu && (!net->ip || destination != net->ip) && !initial_dhcp) {
        ++net->dropped;
        return;
    }
    if (ip[9] == 17u && ip_length >= 28u) {
        const uint8_t *udp = ip + 20u;
        uint32_t size = be16(udp + 4u);
        if (size != ip_length - 20u || size < 8u || be16(udp) != 67u ||
            be16(udp + 2u) != 68u ||
            (be16(udp + 6u) && transport_sum(ip, size) != 0u)) ++net->dropped;
        else dhcp_receive(net, udp + 8u, size - 8u);
    } else if (net->ip && destination == net->ip && ip[9] == 6u) {
        tcp_receive(net, frame, ip_length);
    } else if (net->ip && destination == net->ip && ip[9] == 1u && ip_length >= 28u) {
        const uint8_t *icmp = ip + 20u;
        uint32_t n = ip_length - 20u;
        if (icmp[0] != 8u || icmp[1] != 0u || finish(sum(icmp, n, 0u)) != 0u) {
            ++net->dropped;
            return;
        }
        uint8_t *reply = ipv4(net, frame + 6u, be32(ip + 12u), 1u, (uint16_t)n);
        copy(reply, icmp, n);
        reply[0] = reply[2] = reply[3] = 0u;
        put16(reply + 2u, finish(sum(reply, n, 0u)));
        transmit(net, ip_length + 14u);
    } else ++net->dropped;
}

void network_tick(network_t *net, uint32_t now) {
    net->now = now;
    if (net->ip && now - net->lease_at >= net->lease_ms) dhcp_restart(net);
    if (net->dhcp_state == 3u && now - net->lease_at >= net->lease_ms / 2u) {
        net->dhcp_state = 4u;
        net->dhcp_attempts = 0u;
        ++net->xid;
        dhcp_send(net);
    }
    if ((net->dhcp_state == 1u || net->dhcp_state == 2u || net->dhcp_state == 4u) &&
        now - net->dhcp_at >= 4000u) {
        if (net->dhcp_attempts < 8u || net->dhcp_state == 4u) dhcp_send(net);
        else {
            net->error = 1u;
            net->dhcp_state = 5u;
        }
    }
    for (uint32_t i = 0u; i < NET_CLIENTS; ++i) {
        net_client_t *client = net->clients + i;
        if (!client->state) continue;
        if (now - client->active_at >= (client->state == 4u ? 2000u : 15000u)) {
            client->state = 0u;
            continue;
        }
        if (client->send_next != client->send_acked && now - client->sent_at >= 1000u) {
            if (client->attempts >= 5u) {
                client->state = 0u;
                ++net->dropped;
            } else {
                ++client->attempts;
                ++net->retransmits;
                client->sent_at = now;
                tcp_send(net, client, client->flags, client->outstanding, client->send_acked);
            }
        } else tcp_next(net, client);
    }
}

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "net/network.h"

static network_t net;
static uint8_t last[1514];
static uint32_t last_size, sends, calls;
static const uint8_t ours[6] = {2, 1, 2, 3, 4, 5};
static const uint8_t peer[6] = {2, 6, 7, 8, 9, 10};
static uint32_t local_seq, remote_seq;
static uint16_t peer_window = 4096;
static uint16_t peer_port = 45678;

static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] << 8u | p[1]); }
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)get16(p) << 16u | get16(p + 2);
}
static void p16(uint8_t *p, uint32_t n) { p[0] = (uint8_t)(n >> 8u); p[1] = (uint8_t)n; }
static void p32(uint8_t *p, uint32_t n) { p16(p, n >> 16u); p16(p + 2, n); }
static uint32_t add(const uint8_t *p, uint32_t n, uint32_t s) {
    for (uint32_t i = 0; i < n; i += 2) s += (uint32_t)p[i] << 8u | (i + 1 < n ? p[i + 1] : 0);
    return s;
}
static uint16_t checksum(uint32_t s) {
    while (s >> 16u) s = (s & 65535) + (s >> 16u);
    return (uint16_t)~s;
}
static uint16_t l4sum(const uint8_t *ip) {
    uint32_t n = get16(ip + 2) - 20u;
    return checksum(add(ip + 20, n, add(ip + 12, 8, ip[9] + n)));
}

static int send_frame(void *context, const uint8_t *frame, uint32_t size) {
    (void)context;
    assert(size >= 60 && size <= 1514);
    assert(memcmp(frame + 6, ours, 6) == 0);
    if (get16(frame + 12) == 0x0800) {
        assert(checksum(add(frame + 14, 20, 0)) == 0);
        if (frame[23] == 6) assert(l4sum(frame + 14) == 0);
    }
    memcpy(last, frame, size);
    last_size = size;
    ++sends;
    return 1;
}

static uint32_t respond(void *context, const http_request_t *request,
                         uint8_t *out, uint32_t capacity) {
    (void)context;
    ++calls;
    assert(!request->post && strcmp(request->path, "/") == 0);
    static uint8_t body[15000];
    memset(body, 'x', sizeof(body));
    return http_response(out, capacity, 200, "text/plain", body, sizeof(body));
}

static void init(void) {
    const net_ops_t ops = {NULL, send_frame, respond};
    memset(&net, 0, sizeof(net));
    sends = calls = 0;
    peer_port = 45678;
    network_init(&net, &ops, ours, 0x12345678, 0);
    assert(sends == 1 && net.dhcp_state == 1);
    assert(get16(last + 34) == 68 && get16(last + 36) == 67);
    assert(last[14 + 20 + 8 + 242] == 1);
}

static uint8_t *ip_header(uint8_t *frame, uint32_t source, uint32_t dest,
                           uint8_t protocol, uint16_t size) {
    memset(frame, 0, 1514);
    memcpy(frame, ours, 6);
    memcpy(frame + 6, peer, 6);
    p16(frame + 12, 0x0800);
    uint8_t *ip = frame + 14;
    ip[0] = 0x45;
    ip[8] = 64;
    ip[9] = protocol;
    p16(ip + 2, 20 + size);
    p32(ip + 12, source);
    p32(ip + 16, dest);
    p16(ip + 10, checksum(add(ip, 20, 0)));
    return ip + 20;
}

static void dhcp(uint8_t type, uint32_t offer, uint32_t lease, uint8_t corrupt) {
    uint8_t frame[1514];
    uint8_t *udp = ip_header(frame, 0xc0a80101, type == 2 ? offer : 0xffffffff, 17, 308);
    p16(udp, 67); p16(udp + 2, 68); p16(udp + 4, 308);
    uint8_t *boot = udp + 8;
    boot[0] = 2; boot[1] = 1; boot[2] = 6;
    p32(boot + 4, net.xid);
    p32(boot + 16, offer);
    memcpy(boot + 28, ours, 6);
    p32(boot + 236, 0x63825363);
    boot[240] = 53; boot[241] = 1; boot[242] = type;
    boot[243] = 54; boot[244] = 4; p32(boot + 245, 0xc0a80101);
    boot[249] = 51; boot[250] = 4; p32(boot + 251, lease);
    boot[255] = corrupt ? 52 : 255;
    if (corrupt) { boot[256] = 1; boot[257] = 3; }
    network_input(&net, frame, 342);
}

static void acquire(void) {
    dhcp(2, 0xc0a80150, 3600, 0);
    assert(net.dhcp_state == 2 && !net.ip && sends == 2);
    dhcp(5, 0xc0a80150, 3600, 0);
    assert(net.dhcp_state == 3 && net.ip == 0xc0a80150);
    assert(strcmp(net.address, "192.168.1.80") == 0);
}

static void tcp(uint8_t flags, const uint8_t *body, uint32_t length,
                  uint32_t sequence, uint32_t ack) {
    uint8_t frame[1514];
    uint8_t *t = ip_header(frame, 0xc0a80122, net.ip, 6, (uint16_t)(20 + length));
    p16(t, peer_port); p16(t + 2, 80);
    p32(t + 4, sequence); p32(t + 8, ack);
    t[12] = 0x50; t[13] = flags;
    p16(t + 14, peer_window);
    if (length) memcpy(t + 20, body, length);
    p16(t + 16, l4sum(frame + 14));
    network_input(&net, frame, 54 + length);
}

static void connect_client(void) {
    peer_window = 4096;
    remote_seq = 0xfffffff0;
    tcp(2, NULL, 0, remote_seq, 0);
    assert(last[47] == 0x12 && get32(last + 42) == remote_seq + 1u);
    local_seq = get32(last + 38) + 1u;
    ++remote_seq;
    tcp(0x10, NULL, 0, remote_seq, local_seq);
}

static void http_tests(void) {
    http_request_t request;
    static const char good[] = "POST /api/config HTTP/1.1\r\nHost: 192.168.1.80:80\r\n"
        "Content-Length: 4\r\nContent-Type: application/x-www-form-urlencoded\r\n"
        "X-Pico-Request: 1\r\n\r\nsf=7";
    for (uint32_t n = 0; n < sizeof(good) - 1; ++n)
        assert(http_parse((const uint8_t *)good, n, "192.168.1.80", &request) == 0);
    assert(http_parse((const uint8_t *)good, sizeof(good) - 1, "192.168.1.80", &request) == 1);
    assert(request.post && request.write_allowed && request.body_size == 4);
    assert(memcmp(request.body, "sf=7", 4) == 0);
    static const char *bad[] = {
        "GET / HTTP/1.1\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: evil.example\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: 192.168.1.80\r\nTransfer-Encoding: chunked\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: 192.168.1.80\r\nHost: 192.168.1.80\r\n\r\n",
        "POST / HTTP/1.1\r\nHost: 192.168.1.80\r\nContent-Length: 0\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: 192.168.1.80\r\nContent-Length: 1\r\n\r\nx",
        "GET / HTTP/1.1\r\nHost: 192.168.1.80\r\nExpect: 100-continue\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: 192.168.1.80\r\n\r\nGET / HTTP/1.1\r\n\r\n",
        "GET / HTTP/1.0\r\nHost: 192.168.1.80\r\n\r\n",
        "GET / HTTP/1.1\r\n Host: 192.168.1.80\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: 192.168.1.80\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n"
    };
    for (uint32_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        assert(http_parse((const uint8_t *)bad[i], (uint32_t)strlen(bad[i]), "192.168.1.80", &request) == -1);
    uint8_t out[512];
    uint32_t size = http_response(out, sizeof(out), 200, "text/plain", (const uint8_t *)"abc", 3);
    assert(size && memcmp(out + size - 3, "abc", 3) == 0);
    assert(http_response(out, size - 1, 200, "text/plain", (const uint8_t *)"abc", 3) == 0);
}

static void tcp_tests(void) {
    init(); acquire(); connect_client();
    const uint8_t first[] = "GET / HTTP/1.1\r\nHost: 192.";
    const uint8_t second[] = "168.1.80\r\n\r\n";
    tcp(0x18, first, sizeof(first) - 1, remote_seq, local_seq);
    assert(calls == 0 && last[47] == 0x10);
    uint32_t first_seq = remote_seq;
    remote_seq += sizeof(first) - 1;
    tcp(0x18, first, sizeof(first) - 1, first_seq, local_seq);
    assert(calls == 0 && get32(last + 42) == remote_seq);
    tcp(0x18, second, sizeof(second) - 1, remote_seq + 5, local_seq);
    assert(calls == 0 && get32(last + 42) == remote_seq);
    peer_window = 0;
    tcp(0x18, second, sizeof(second) - 1, remote_seq, local_seq);
    remote_seq += sizeof(second) - 1;
    assert(calls == 1 && last[47] == 0x10 && get32(last + 42) == remote_seq);
    peer_window = 4096;
    tcp(0x10, NULL, 0, remote_seq, local_seq);
    assert(calls == 1 && last[47] == 0x18 && get32(last + 38) == local_seq);
    assert(memcmp(last + 54, "HTTP/1.1 200 OK", 15) == 0);
    uint8_t saved[1514];
    memcpy(saved, last, last_size);
    uint32_t saved_size = last_size;
    network_tick(&net, 1000);
    assert(net.retransmits == 1 && last_size == saved_size);
    assert(memcmp(last + 34, saved + 34, last_size - 34) == 0);
    uint32_t total = 0;
    for (uint32_t segments = 0; segments < 40; ++segments) {
        if (last[47] == 0x11) break;
        assert(last[47] == 0x18);
        uint32_t size = get16(last + 16) - 40u;
        assert(size > 0 && size <= 536);
        local_seq += size;
        total += size;
        tcp(0x10, NULL, 0, remote_seq, local_seq);
    }
    assert(total > 15000 && calls == 1 && last[47] == 0x11);
    tcp(0x11, NULL, 0, remote_seq, local_seq + 1u);
    assert(last[47] == 0x10 && get32(last + 42) == remote_seq + 1u);
    network_tick(&net, 3100);
    for (uint32_t i = 0; i < NET_CLIENTS; ++i) assert(net.clients[i].state == 0);
    connect_client();
    network_tick(&net, 20000);
    for (uint32_t i = 0; i < NET_CLIENTS; ++i) assert(net.clients[i].state == 0);
}

static void concurrent_clients(void) {
    init(); acquire();
    for (uint32_t i = 0; i < NET_CLIENTS; ++i) {
        peer_port = (uint16_t)(45678u + i);
        connect_client();
        uint32_t active = 0;
        for (uint32_t j = 0; j < NET_CLIENTS; ++j) active += net.clients[j].state != 0;
        assert(active == i + 1);
    }
    uint32_t before = sends, dropped = net.dropped;
    peer_port = 50000;
    tcp(2, NULL, 0, 1234, 0);
    assert(sends == before && net.dropped == dropped + 1);
    network_tick(&net, 16000);
    for (uint32_t i = 0; i < NET_CLIENTS; ++i) assert(net.clients[i].state == 0);
    connect_client();
    assert(last[47] == 0x12);
}

static void network_tests(void) {
    init();
    dhcp(2, 0xc0a80150, 3600, 1);
    assert(net.dhcp_state == 1 && net.dropped == 1);
    acquire();
    network_tick(&net, 1800000);
    assert(net.dhcp_state == 4 && get32(last + 54) == net.ip);
    dhcp(5, net.ip, 3600, 0);
    assert(net.dhcp_state == 3 && net.lease_at == 1800000);
    uint8_t frame[1514];
    memset(frame, 0, sizeof(frame));
    memcpy(frame, ours, 6); memcpy(frame + 6, peer, 6);
    p16(frame + 12, 0x0806);
    uint8_t *a = frame + 14;
    p16(a, 1); p16(a + 2, 0x0800); a[4] = 6; a[5] = 4; p16(a + 6, 1);
    memcpy(a + 8, peer, 6); p32(a + 14, 0xc0a80122); p32(a + 24, net.ip);
    network_input(&net, frame, 42);
    assert(get16(last + 20) == 2 && get32(last + 28) == net.ip);
    p32(a + 14, net.ip);
    network_input(&net, frame, 42);
    assert(net.error == 3 && net.ip == 0 && net.dhcp_state == 5);
    init(); acquire();
    uint8_t *icmp = ip_header(frame, 0xc0a80122, net.ip, 1, 8);
    icmp[0] = 8; p16(icmp + 4, 7); p16(icmp + 2, checksum(add(icmp, 8, 0)));
    network_input(&net, frame, 42);
    assert(last[23] == 1 && last[34] == 0 && checksum(add(last + 34, 8, 0)) == 0);
    uint32_t before = sends;
    frame[24] ^= 1;
    network_input(&net, frame, 42);
    assert(sends == before);
    for (uint32_t n = 0; n < 42; ++n) network_input(&net, frame, n);
    assert(sends == before);
    network_tick(&net, 5400000);
    assert(!net.ip && net.dhcp_state == 1);
    init();
    for (uint32_t n = 1; n <= 8; ++n) network_tick(&net, n * 4000);
    assert(net.error == 1 && net.dhcp_state == 5 && sends == 8);
}

static void malformed_inputs(void) {
    init(); acquire();
    uint8_t frame[1514];
    uint32_t random = 0x1234abcd;
    for (uint32_t iteration = 0; iteration < 10000; ++iteration) {
        uint32_t length = iteration % sizeof(frame);
        for (uint32_t i = 0; i < length; ++i) {
            random ^= random << 13u; random ^= random >> 17u; random ^= random << 5u;
            frame[i] = (uint8_t)random;
        }
        if (length >= 14) memcpy(frame, ours, 6);
        if (length >= 34 && iteration % 2 == 0) {
            p16(frame + 12, 0x0800);
            frame[14] = 0x45;
            p16(frame + 16, length - 14);
            p16(frame + 20, 0);
            p32(frame + 30, net.ip);
            p16(frame + 24, 0);
            p16(frame + 24, checksum(add(frame + 14, 20, 0)));
        }
        network_input(&net, frame, length);
        http_request_t request;
        if (length <= HTTP_REQUEST_MAX) (void)http_parse(frame, length, net.address, &request);
    }
    assert(net.ip == 0xc0a80150 && net.dropped >= 10000);
}

int main(void) {
    http_tests(); network_tests(); tcp_tests(); concurrent_clients(); malformed_inputs();
    puts("PASS: strict HTTP, DHCP lease/renewal, ARP/ICMP, TCP segmentation/retransmit/close, bounds");
    return 0;
}

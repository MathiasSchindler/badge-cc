/* Platform fakes exercise the production transport and pbuf-chain ownership. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../sdk/wifi_handover/udp_protocol.h"

typedef int err_t;
typedef uint16_t u16_t;
typedef struct { uint32_t addr; } ip4_addr_t;
typedef ip4_addr_t ip_addr_t;
struct netif { ip4_addr_t address, mask, gateway; bool up, link; };
struct pbuf { struct pbuf *next; uint16_t len, tot_len; uint8_t bytes[64]; };
struct udp_pcb {
    void (*receive)(void *, struct udp_pcb *, struct pbuf *, const ip_addr_t *, u16_t);
    void *arg;
};
static struct { struct netif netif[2]; unsigned itf_state; } cyw43_state;
static uint32_t mock_ms, mock_us, mock_pm, mock_channel, mock_join_channel;
static unsigned allocated, freed, sends, joins, removes;
static unsigned state_sends[EXP_MAX_PLAYERS], input_sends;
static int pm_error, join_error;
static bool alloc_error;
static err_t send_error;
static udp_message_t last_sent;
static uint8_t last_physical_slot;
static struct udp_pcb mock_pcb;

#define CYW43_ITF_STA 0
#define CYW43_ITF_AP 1
#define CYW43_AUTH_WPA2_AES_PSK 4u
#define CYW43_NONE_PM 0u
#define CYW43_DEFAULT_PM 1u
#define LWIP_DHCP 0
#define PBUF_TRANSPORT 0
#define PBUF_RAM 0
#define IPADDR_TYPE_V4 0
#define ERR_OK 0
#define ERR_MEM -1
#define ERR_ARG -16
#define IP4_ADDR(p, a, b, c, d) \
    ((p)->addr = ((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
                 ((uint32_t)(c) << 8) | (uint32_t)(d))
#define IP_ADDR4 IP4_ADDR
#define ip4_addr_set_zero(p) ((p)->addr = 0)
#define ip4_addr_get_u32(p) ((p)->addr)
#define ip4_addr_isany_val(p) ((p).addr == 0)
#define ip_2_ip4(p) (p)
#define IP_IS_V4(p) ((p) != NULL)
#define lwip_ntohl(n) (n)
#define netif_is_up(n) ((n)->up)
#define netif_is_link_up(n) ((n)->link)
#define netif_ip4_addr(n) (&(n)->address)
#define netif_ip4_netmask(n) (&(n)->mask)
#define netif_ip4_gw(n) (&(n)->gateway)

static uint32_t get_absolute_time(void) { return mock_ms; }
static uint32_t to_ms_since_boot(uint32_t t) { return t; }
static uint32_t time_us_32(void) { return mock_us; }
static uint32_t get_rand_32(void) { return 777; }
static void netif_set_addr(struct netif *n, const ip4_addr_t *a,
                           const ip4_addr_t *m, const ip4_addr_t *g) {
    n->address = *a; n->mask = *m; n->gateway = *g;
}
static void netif_set_default(struct netif *n) { (void)n; }
static void cyw43_wifi_ap_set_channel(void *s, uint32_t channel) {
    (void)s; mock_channel = channel;
}
static void cyw43_arch_enable_ap_mode(const char *ssid, const char *pw, uint32_t auth) {
    assert(ssid[0] && strlen(pw) >= 8 && auth == CYW43_AUTH_WPA2_AES_PSK);
    cyw43_state.itf_state |= 2u;
    cyw43_state.netif[1].up = true;
}
static void cyw43_arch_enable_sta_mode(void) {
    cyw43_state.itf_state |= 1u;
    cyw43_state.netif[0].up = true;
}
static void cyw43_arch_disable_ap_mode(void) {
    cyw43_state.itf_state &= ~2u;
    cyw43_state.netif[1] = (struct netif){0};
}
static void cyw43_arch_disable_sta_mode(void) {
    cyw43_state.itf_state &= ~1u;
    cyw43_state.netif[0] = (struct netif){0};
}
static int cyw43_wifi_pm(void *s, uint32_t pm) {
    (void)s; mock_pm = pm; return pm_error;
}
static int cyw43_wifi_link_status(void *s, int itf) {
    (void)s; return cyw43_state.netif[itf].link ? 1 : 0;
}
static int cyw43_wifi_join(void *s, size_t ssid_len, const uint8_t *ssid,
                           size_t key_len, const uint8_t *key, uint32_t auth,
                           const uint8_t *bssid, uint32_t channel) {
    (void)s; (void)ssid; (void)key;
    assert(ssid_len && key_len >= 8 && auth == CYW43_AUTH_WPA2_AES_PSK && !bssid);
    ++joins; mock_join_channel = channel; return join_error;
}
static struct pbuf *pbuf_alloc(int layer, uint16_t size, int type) {
    (void)layer; (void)type;
    if (alloc_error) return NULL;
    struct pbuf *p = calloc(1, sizeof(*p));
    assert(p && size <= sizeof(p->bytes));
    p->len = p->tot_len = size;
    ++allocated;
    return p;
}
static void pbuf_free(struct pbuf *p) {
    while (p) {
        struct pbuf *next = p->next;
        free(p);
        ++freed;
        p = next;
    }
}
static err_t pbuf_take(struct pbuf *p, const void *bytes, size_t size) {
    assert(size <= p->len);
    memcpy(p->bytes, bytes, size);
    return ERR_OK;
}
static uint16_t pbuf_copy_partial(struct pbuf *p, void *bytes, uint16_t size,
                                  uint16_t offset) {
    assert(offset == 0);
    uint16_t copied = 0;
    for (; p && copied < size; p = p->next) {
        uint16_t count = p->len < size - copied ? p->len : size - copied;
        memcpy((uint8_t *)bytes + copied, p->bytes, count);
        copied += count;
    }
    return copied;
}
static struct udp_pcb *udp_new_ip_type(int type) {
    (void)type;
    return &mock_pcb;
}
static err_t udp_bind(struct udp_pcb *pcb, const ip_addr_t *address, u16_t port) {
    assert(pcb == &mock_pcb && address->addr >= udp_protocol_ip(0) &&
           address->addr <= udp_protocol_ip(3) && port == UDP_PROTOCOL_PORT);
    return ERR_OK;
}
static void udp_recv(struct udp_pcb *pcb,
                      void (*receive)(void *, struct udp_pcb *, struct pbuf *,
                                      const ip_addr_t *, u16_t), void *arg) {
    pcb->receive = receive; pcb->arg = arg;
}
static void udp_remove(struct udp_pcb *pcb) {
    assert(pcb == &mock_pcb);
    memset(pcb, 0, sizeof(*pcb));
    ++removes;
}
static err_t udp_sendto_if(struct udp_pcb *pcb, struct pbuf *p,
                           const ip_addr_t *address, u16_t port, struct netif *n) {
    assert(pcb == &mock_pcb && n->up && n->link && port == UDP_PROTOCOL_PORT);
    assert(udp_protocol_decode(&last_sent, p->bytes, p->tot_len));
    last_physical_slot = (uint8_t)(address->addr - udp_protocol_ip(0));
    if (last_sent.type == UDP_STATE) ++state_sends[last_physical_slot];
    if (last_sent.type == UDP_INPUT) ++input_sends;
    ++sends;
    return send_error;
}

#define WIFI_TRANSPORT_HOST_TEST
#include "../../sdk/wifi_handover/wifi_transport.c"

static handover_credentials_t credentials(uint8_t slot) {
    return (handover_credentials_t){
        .session_id = 99, .token = slot ? 456 : 123, .slot = slot,
        .ssid = "handover-test", .password = "test-password"
    };
}

static void poll_at(uint32_t now) {
    mock_ms = now; mock_us = now * 1000u;
    wifi_transport_poll(now);
}

static void deliver(const udp_message_t *m, uint8_t physical_source, u16_t port) {
    uint8_t bytes[UDP_PROTOCOL_SIZE];
    assert(udp_protocol_encode(bytes, m));
    struct pbuf *p = pbuf_alloc(0, 7, 0);
    p->next = pbuf_alloc(0, UDP_PROTOCOL_SIZE - 7, 0);
    p->tot_len = UDP_PROTOCOL_SIZE;
    memcpy(p->bytes, bytes, 7);
    memcpy(p->next->bytes, bytes + 7, UDP_PROTOCOL_SIZE - 7);
    ip_addr_t address = {.addr = udp_protocol_ip(physical_source)};
    assert(mock_pcb.receive);
    mock_pcb.receive(mock_pcb.arg, &mock_pcb, p, &address, port);
}

static uint32_t admit(uint8_t slot, uint64_t token, uint32_t nonce) {
    udp_message_t hello = {
        .type = UDP_HELLO, .session = 99, .token = token,
        .sender = slot, .origin = slot, .run = nonce
    };
    deliver(&hello, slot, UDP_PROTOCOL_PORT);
    assert(last_sent.type == UDP_WELCOME && last_sent.run == nonce);
    uint32_t epoch = last_sent.epoch;
    deliver(&hello, slot, UDP_PROTOCOL_PORT);
    assert(last_sent.epoch == epoch);
    udp_message_t input = {
        .type = UDP_INPUT, .session = 99, .token = token, .epoch = epoch,
        .sender = slot, .origin = slot, .sequence = UINT32_MAX
    };
    input.positions[slot] = 650;
    deliver(&input, slot, UDP_PROTOCOL_PORT);
    assert(wifi_transport_view()->online_mask & (1u << slot));
    assert(wifi_transport_view()->positions[slot] == 650);
    return epoch;
}

static void test_host(void) {
    handover_credentials_t c = credentials(0);
    assert(wifi_transport_host(&c));
    assert(mock_channel == 6 && mock_pm == CYW43_NONE_PM);
    assert(wifi_transport_view()->phase == WIFI_STARTING_AP);
    assert(cyw43_state.netif[1].address.addr == udp_protocol_ip(0));
    poll_at(100);
    assert(wifi_transport_view()->phase == WIFI_STARTING_AP);
    cyw43_state.netif[1].link = true;
    poll_at(101);
    assert(wifi_transport_view()->phase == WIFI_READY && transport.pcb);
    assert(wifi_transport_view()->online_mask == 1);
    wifi_transport_member(1, 456);
    wifi_transport_member(2, 789);
    wifi_transport_member(3, 456);
    assert(wifi_transport_view()->error_code == ERROR_MEMBER && !transport.tokens[3]);
    wifi_transport_member(1, 999);
    assert(transport.tokens[1] == 456);
    uint32_t epoch1 = admit(1, 456, 100);
    uint32_t epoch2 = admit(2, 789, 200);
    udp_message_t input = {
        .type = UDP_INPUT, .session = 99, .token = 456, .epoch = epoch1,
        .sender = 1, .origin = 1, .sequence = 0, .positions = {0, 700, 0, 0}
    };
    unsigned rejected = transport.view.rejected_packets;
    deliver(&input, 2, UDP_PROTOCOL_PORT);
    deliver(&input, 1, UDP_PROTOCOL_PORT + 1);
    assert(transport.view.rejected_packets == rejected + 2);
    deliver(&input, 1, UDP_PROTOCOL_PORT);
    assert(transport.view.positions[1] == 700);
    input.positions[1] = 900;
    deliver(&input, 1, UDP_PROTOCOL_PORT);
    assert(transport.view.positions[1] == 700);

    udp_message_t ping = {
        .type = UDP_PING, .session = 99, .token = 456, .epoch = epoch1,
        .sender = 1, .target = 2, .origin = 1, .sequence = 1,
        .run = 1000, .stamp_us = mock_us
    };
    deliver(&ping, 1, UDP_PROTOCOL_PORT);
    assert(last_physical_slot == 2 && last_sent.token == 789 && last_sent.epoch == epoch2);
    assert(last_sent.sender == 1 && last_sent.origin == 1 && last_sent.target == 2);
    udp_message_t pong = last_sent;
    pong.type = UDP_PONG; pong.sender = 2; pong.target = 1;
    deliver(&pong, 2, UDP_PROTOCOL_PORT);
    assert(last_physical_slot == 1 && last_sent.token == 456 && last_sent.epoch == epoch1);
    assert(last_sent.sender == 2 && last_sent.origin == 1);

    wifi_transport_input(1);
    poll_at(135);
    assert(transport.view.positions[0] > 500);
    assert(last_sent.type == UDP_STATE && last_sent.online_mask == 7);
    assert(wifi_transport_benchmark(1));
    poll_at(136);
    assert(last_sent.type == UDP_PING && last_sent.origin == 0 && last_physical_slot == 1);
    assert(transport.view.metrics.sent == 1 && transport.view.metrics.received == 0);
    pong = last_sent;
    pong.type = UDP_PONG; pong.sender = 1; pong.target = 0;
    mock_us += 1234;
    deliver(&pong, 1, UDP_PROTOCOL_PORT);
    assert(transport.view.metrics.received == 1 && transport.view.metrics.latest_us == 1234);
    wifi_transport_cancel_benchmark();
    assert(wifi_transport_benchmark(1));
    alloc_error = true;
    poll_at(138);
    alloc_error = false;
    assert(!transport.view.metrics.running && transport.view.metrics.local_errors == 1);
    assert(transport.view.metrics.sent == 0 && transport.view.metrics.lost == 0);
    poll_at(3101);
    assert(transport.view.online_mask == 1 && transport.tokens[1] == 456);
    assert(admit(1, 456, 101) != epoch1);
    cyw43_state.netif[1].link = false;
    poll_at(3102);
    assert(transport.view.phase == WIFI_ERROR);
    assert(allocated == freed);
}

static void join_client(void) {
    handover_credentials_t c = credentials(1);
    assert(wifi_transport_join(&c));
    assert(mock_join_channel == 6 && mock_pm == CYW43_NONE_PM);
    assert(transport.view.phase == WIFI_JOINING);
    assert(cyw43_state.netif[0].address.addr == udp_protocol_ip(1));
    poll_at(mock_ms + 1);
    assert(transport.view.phase == WIFI_JOINING);
    cyw43_state.netif[0].link = true;
    poll_at(mock_ms + 1);
    assert(transport.view.phase == WIFI_HANDSHAKE);
    assert(last_sent.type == UDP_HELLO && last_physical_slot == 0);
    uint32_t nonce = last_sent.run;
    poll_at(mock_ms + 250);
    assert(last_sent.type == UDP_HELLO && last_sent.run == nonce);
    udp_message_t welcome = {
        .type = UDP_WELCOME, .session = 99, .token = 456, .epoch = 900,
        .target = 1, .origin = 1, .run = nonce
    };
    deliver(&welcome, 0, UDP_PROTOCOL_PORT);
    assert(transport.view.phase == WIFI_READY && last_sent.type == UDP_INPUT);
    uint32_t sequence = transport.input_sequence;
    deliver(&welcome, 0, UDP_PROTOCOL_PORT);
    assert(transport.input_sequence == sequence);
}

static void test_client(void) {
    join_client();
    udp_message_t state = {
        .type = UDP_STATE, .session = 99, .token = 456, .epoch = 900,
        .target = 1, .online_mask = 7, .sequence = 8, .positions = {100, 200, 300, 400}
    };
    deliver(&state, 0, UDP_PROTOCOL_PORT);
    assert(transport.view.positions[2] == 300);
    state.sequence--;
    state.positions[2] = 900;
    deliver(&state, 0, UDP_PROTOCOL_PORT);
    assert(transport.view.positions[2] == 300);
    wifi_transport_input(-1);
    poll_at(mock_ms + 34);
    assert(last_sent.type == UDP_INPUT && last_sent.positions[1] < 500);
    assert(wifi_transport_benchmark(2));
    poll_at(mock_ms + 1);
    assert(last_sent.type == UDP_PING && last_sent.target == 2 && last_physical_slot == 0);
    udp_message_t pong = last_sent;
    pong.type = UDP_PONG; pong.sender = 2; pong.target = 1;
    mock_us += 4000;
    deliver(&pong, 0, UDP_PROTOCOL_PORT);
    assert(transport.view.metrics.received == 1 && transport.view.metrics.latest_us == 4000);
    wifi_transport_cancel_benchmark();

    udp_message_t ping = {
        .type = UDP_PING, .session = 99, .token = 456, .epoch = 900,
        .sender = 2, .target = 1, .origin = 2, .sequence = 1,
        .run = 1001, .stamp_us = 333
    };
    deliver(&ping, 0, UDP_PROTOCOL_PORT);
    assert(last_sent.type == UDP_PONG && last_sent.sender == 1 && last_sent.target == 2);
    assert(last_sent.origin == 2 && last_physical_slot == 0 && last_sent.token == 456);
    pm_error = -88;
    assert(!wifi_transport_set_power_save(true));
    assert(transport.view.error_code == -88 && !transport.view.power_save);
    pm_error = 0;
    assert(wifi_transport_set_power_save(true) && transport.view.power_save);
    poll_at(transport.last_host_ms + UDP_PEER_STALE_MS);
    assert(transport.view.phase == WIFI_ERROR && transport.view.error_code == ERROR_HEARTBEAT);
    unsigned old_joins = joins;
    poll_at(mock_ms + 20000);
    assert(joins == old_joins); /* No automatic retry loop. */
    uint32_t old_nonce = transport.client_nonce;
    join_client();
    assert(transport.client_nonce != old_nonce && joins == old_joins + 1);

    unsigned rejected = transport.view.rejected_packets;
    struct pbuf *short_p = pbuf_alloc(0, 43, 0);
    ip_addr_t address = {.addr = udp_protocol_ip(0)};
    mock_pcb.receive(NULL, &mock_pcb, short_p, &address, UDP_PROTOCOL_PORT);
    struct pbuf *broken_chain = pbuf_alloc(0, 10, 0);
    broken_chain->tot_len = UDP_PROTOCOL_SIZE;
    mock_pcb.receive(NULL, &mock_pcb, broken_chain, &address, UDP_PROTOCOL_PORT);
    assert(transport.view.rejected_packets == rejected + 2 && allocated == freed);
}

static void test_deadlines(void) {
    handover_credentials_t c = credentials(0);
    assert(wifi_transport_host(&c));
    poll_at(mock_ms + WIFI_START_TIMEOUT_MS);
    assert(transport.view.phase == WIFI_ERROR && transport.view.error_code == ERROR_START_TIMEOUT);
    c = credentials(1);
    assert(wifi_transport_join(&c));
    cyw43_state.netif[0].link = true;
    poll_at(mock_ms + 1);
    assert(transport.view.phase == WIFI_HANDSHAKE);
    poll_at(mock_ms + UDP_HANDSHAKE_MS);
    assert(transport.view.phase == WIFI_ERROR && transport.view.error_code == ERROR_HANDSHAKE);
    join_error = -99;
    assert(!wifi_transport_join(&c));
    assert(transport.view.error_code == -99);
    join_error = 0;
    c.slot = 4;
    assert(!wifi_transport_join(&c) && transport.view.error_code == ERROR_CREDENTIALS);
    wifi_transport_stop();
    assert(transport.view.phase == WIFI_IDLE && !transport.pcb && !cyw43_state.itf_state);
}

static void test_host_progress_during_benchmark(void) {
    handover_credentials_t c = credentials(0);
    assert(wifi_transport_host(&c));
    cyw43_state.netif[1].link = true;
    poll_at(mock_ms + 1);
    wifi_transport_member(1, 456);
    wifi_transport_member(2, 789);
    uint32_t epoch = admit(1, 456, 301);
    (void)admit(2, 789, 302);
    assert(wifi_transport_benchmark(1));
    uint32_t started = mock_ms;
    unsigned frames_before = state_sends[1];
    udp_message_t input = {
        .type = UDP_INPUT, .session = 99, .token = 456, .epoch = epoch,
        .sender = 1, .origin = 1
    };
    for (uint32_t i = 1; i <= EXP_BENCH_SAMPLES; ++i) {
        assert(transport.view.metrics.running);
        input.sequence = i - 1u;
        input.positions[1] = (uint16_t)(i % (EXP_POSITION_MAX + 1u));
        deliver(&input, 1, UDP_PROTOCOL_PORT);
        wifi_transport_input((i / 20u) % 2u ? -1 : 1);
        uint16_t position_before = transport.view.positions[0];
        unsigned previous_frames = state_sends[1];
        poll_at(started + i * 21u);
        assert(transport.view.phase == WIFI_READY);
        assert(transport.view.positions[1] == input.positions[1]);
        if (state_sends[1] != previous_frames)
            assert(transport.view.positions[0] != position_before);
        if (i * 21u >= UDP_PEER_STALE_MS)
            assert(transport.view.online_mask == 3u);
        assert(last_sent.type == UDP_PING && last_sent.sequence == i);
        assert(transport.view.metrics.sent == i);
        udp_message_t pong = last_sent;
        pong.type = UDP_PONG;
        pong.sender = 1;
        pong.target = 0;
        if (i == 500) {
            pm_error = -88;
            assert(!wifi_transport_set_power_save(true));
            pm_error = 0;
        }
        mock_us += 1000;
        deliver(&pong, 1, UDP_PROTOCOL_PORT);
        assert(transport.view.metrics.received == i);
        if (i >= 500) {
            assert(transport.view.error_code == -88);
            assert(strcmp(transport.view.status, "Power-save change failed") == 0);
        }
    }
    assert(!transport.view.metrics.running && transport.view.metrics.lost == 0);
    assert(state_sends[1] - frames_before == 630u); /* Exactly 30 Hz across 21 seconds. */
    assert(transport.view.online_mask == 3u && transport.tokens[2] == 789);

    assert(wifi_transport_benchmark(1));
    poll_at(mock_ms + 21);
    assert(transport.bench.outstanding);
    /* Fast-forward the accepted-sample count to exercise final timeout reporting. */
    transport.bench.metrics.sent = EXP_BENCH_SAMPLES;
    transport.bench.metrics.lost = EXP_BENCH_SAMPLES - 1u;
    wifi_transport_member(1, 999);
    assert(transport.view.error_code == ERROR_MEMBER);
    poll_at(mock_ms + 1000);
    assert(!transport.view.metrics.running && transport.view.metrics.lost == 1000);
    assert(transport.view.phase == WIFI_READY && transport.view.error_code == ERROR_MEMBER);
    assert(strcmp(transport.view.status, "Member slot already reserved") == 0);

    assert(!wifi_transport_benchmark(0));
    assert(transport.view.error_code == ERROR_BENCH_TARGET);
    assert(strcmp(transport.view.status, "Benchmark target not online") == 0);
    poll_at(mock_ms + 34);
    assert(transport.view.phase == WIFI_READY && transport.view.error_code == ERROR_BENCH_TARGET);
    assert(wifi_transport_benchmark(1));
    send_error = -77;
    poll_at(mock_ms + 34);
    send_error = 0;
    assert(transport.view.phase == WIFI_READY && transport.view.error_code == -77);
    assert(strcmp(transport.view.status, "UDP local send failure") == 0);
    assert(!transport.view.metrics.running && transport.view.metrics.local_errors == 1);
    assert(transport.view.metrics.lost == 0);
    poll_at(mock_ms + 34);
    assert(transport.view.error_code == -77 && allocated == freed);
}

static void test_client_progress_during_benchmark(void) {
    join_client();
    udp_message_t state = {
        .type = UDP_STATE, .session = 99, .token = 456, .epoch = 900,
        .target = 1, .online_mask = 7, .sequence = 1, .positions = {500, 500, 500, 500}
    };
    deliver(&state, 0, UDP_PROTOCOL_PORT);
    assert(wifi_transport_benchmark(2));
    poll_at(mock_ms + 1);
    assert(transport.bench.outstanding);
    unsigned before = input_sends;
    uint16_t position = transport.own_position;
    wifi_transport_input(-1);
    for (uint32_t i = 0; i < 10; ++i) {
        ++state.sequence;
        state.positions[1] = transport.own_position;
        state.online_mask = i > 4 ? 3 : 7;
        deliver(&state, 0, UDP_PROTOCOL_PORT);
        poll_at(mock_ms + 34);
        assert(transport.view.phase == WIFI_READY && transport.view.metrics.running);
        assert(transport.view.metrics.sent == 1 && transport.bench.outstanding);
        assert(transport.own_position < position);
        assert(transport.view.positions[1] == state.positions[1]);
        assert(transport.view.online_mask == state.online_mask);
        position = transport.own_position;
    }
    assert(input_sends - before == 10u);
    wifi_transport_cancel_benchmark();
}

int main(void) {
    wifi_transport_init();
    test_host();
    test_client();
    test_deadlines();
    test_host_progress_during_benchmark();
    test_client_progress_during_benchmark();
    wifi_transport_stop();
    assert(allocated == freed && removes && sends);
    puts("wifi_transport_test: all tests passed");
    return 0;
}

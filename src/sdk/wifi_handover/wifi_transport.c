#include "wifi_transport.h"
#include "udp_protocol.h"

#include <stdio.h>
#include <string.h>
#ifndef WIFI_TRANSPORT_HOST_TEST
#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "pico/time.h"
#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#endif

#define WIFI_START_TIMEOUT_MS 15000u
#define HELLO_INTERVAL_MS 250u

enum {
    ERROR_CREDENTIALS = -1001, ERROR_START_TIMEOUT = -1002,
    ERROR_HEARTBEAT = -1003, ERROR_LINK_DOWN = -1004,
    ERROR_HANDSHAKE = -1005, ERROR_MEMBER = -1006,
    ERROR_BENCH_TARGET = -1007, ERROR_NOT_READY = -1008,
    ERROR_REMOTE_SEND = -1009
};

static struct {
    experiment_wifi_view_t view;
    handover_credentials_t credentials;
    struct udp_pcb *pcb;
    udp_peer_t peers[EXP_MAX_PLAYERS];
    udp_relay_t relays[EXP_MAX_PLAYERS];
    uint64_t tokens[EXP_MAX_PLAYERS];
    udp_benchmark_t bench;
    uint32_t started_ms;
    uint32_t phase_ms;
    uint32_t hello_ms;
    uint32_t frame_ms;
    uint32_t frame_credit;
    uint32_t last_host_ms;
    uint32_t client_nonce;
    uint32_t client_epoch;
    uint32_t input_sequence;
    uint32_t state_sequence;
    uint16_t own_position;
    int direction;
    bool state_sequence_valid;
    bool interface_started;
    bool address_set;
} transport;

/* Not reset by stop/retry: late packets cannot match a later run on this boot. */
static uint32_t generation;

static uint32_t clock_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static uint32_t next_generation(void) {
    if (!generation) generation = get_rand_32();
    if (++generation == 0) ++generation;
    return generation;
}

static void status_text(const char *text, int code) {
    transport.view.error_code = code;
    snprintf(transport.view.status, sizeof(transport.view.status), "%s", text);
}

static void update_metrics(void) {
    transport.view.metrics = transport.bench.metrics;
}

static void fail(const char *text, int code) {
    udp_benchmark_cancel(&transport.bench);
    update_metrics();
    transport.view.phase = WIFI_ERROR;
    transport.view.online_mask = 0;
    status_text(text, code);
}

static struct netif *active_netif(void) {
    return &cyw43_state.netif[transport.view.host ? CYW43_ITF_AP : CYW43_ITF_STA];
}

static bool interface_exists(void) {
    unsigned itf = transport.view.host ? CYW43_ITF_AP : CYW43_ITF_STA;
    return (cyw43_state.itf_state & (1u << itf)) != 0;
}

static void provision_address(void) {
    if (!interface_exists()) return;
    struct netif *n = active_netif();
    ip4_addr_t address, mask, gateway;
    IP4_ADDR(&address, 192, 168, 4, transport.view.slot + 1u);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    ip4_addr_set_zero(&gateway);
#if LWIP_DHCP
    /* Also safe if a different integration enables the driver's automatic DHCP. */
    dhcp_stop(n);
#endif
    netif_set_addr(n, &address, &mask, &gateway);
    netif_set_default(n);
    transport.address_set = true;
}

static bool interface_ready(void) {
    if (!interface_exists() || !transport.address_set) return false;
    struct netif *n = active_netif();
    return netif_is_up(n) && netif_is_link_up(n) &&
           lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(n))) ==
               udp_protocol_ip(transport.view.slot) &&
           lwip_ntohl(ip4_addr_get_u32(netif_ip4_netmask(n))) == 0xffffff00u &&
           ip4_addr_isany_val(*netif_ip4_gw(n));
}

static err_t send_message(const udp_message_t *message, uint8_t physical_slot) {
    uint8_t bytes[UDP_PROTOCOL_SIZE];
    err_t error = ERR_ARG;
    if (transport.pcb && physical_slot < EXP_MAX_PLAYERS &&
        udp_protocol_encode(bytes, message)) {
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, UDP_PROTOCOL_SIZE, PBUF_RAM);
        if (!p) {
            error = ERR_MEM;
        } else {
            error = pbuf_take(p, bytes, sizeof(bytes));
            if (error == ERR_OK) {
                ip_addr_t destination;
                IP_ADDR4(&destination, 192, 168, 4, physical_slot + 1u);
                error = udp_sendto_if(transport.pcb, p, &destination,
                                      UDP_PROTOCOL_PORT, active_netif());
            }
            pbuf_free(p);
        }
    }
    if (error == ERR_OK) {
        ++transport.view.tx_packets;
    } else {
        ++transport.view.send_errors;
        status_text("UDP local send failure", error);
        if (transport.bench.metrics.running) {
            udp_benchmark_local_error(&transport.bench);
            update_metrics();
        }
    }
    return error;
}

static udp_message_t message_base(uint8_t type, uint8_t target) {
    udp_message_t m = {
        .type = type, .session = transport.credentials.session_id,
        .sender = transport.view.slot, .target = target,
        .origin = transport.view.slot,
        .token = transport.tokens[transport.view.host ? target : transport.view.slot],
        .epoch = transport.view.host ? transport.peers[target].epoch :
                                      transport.client_epoch
    };
    return m;
}

static void report_echo_error(const udp_message_t *request) {
    udp_message_t m = message_base(UDP_BENCH_ERROR, request->origin);
    m.origin = request->origin;
    m.sequence = request->sequence;
    m.run = request->run;
    m.stamp_us = request->stamp_us;
    (void)send_message(&m, transport.view.host ? m.target : 0);
}

static void echo_ping(const udp_message_t *request) {
    udp_message_t reply = message_base(UDP_PONG, request->origin);
    reply.origin = request->origin;
    reply.sequence = request->sequence;
    reply.run = request->run;
    reply.stamp_us = request->stamp_us;
    if (send_message(&reply, transport.view.host ? reply.target : 0) != ERR_OK)
        report_echo_error(request);
}

static void receive_benchmark(const udp_message_t *m, uint32_t now_us) {
    bool accepted;
    if (m->type == UDP_BENCH_ERROR) {
        accepted = udp_benchmark_remote_error(&transport.bench, m,
                                              transport.view.slot, now_us);
        if (accepted) status_text("Benchmark remote local failure", ERROR_REMOTE_SEND);
    } else {
        accepted = udp_benchmark_echo(&transport.bench, m, transport.view.slot, now_us);
    }
    if (!accepted) ++transport.view.rejected_packets;
    else if (!transport.bench.metrics.running && !transport.bench.metrics.local_errors &&
             !transport.view.error_code)
        status_text("Benchmark complete", 0);
    update_metrics();
}

static void receive_host(udp_message_t *m, uint32_t now_ms, uint32_t now_us) {
    uint8_t slot = m->sender;
    udp_peer_t *peer = &transport.peers[slot];
    if (m->type == UDP_HELLO) {
        uint32_t epoch = udp_peer_hello(peer, m->run, next_generation(), now_ms);
        udp_message_t welcome = message_base(UDP_WELCOME, slot);
        welcome.origin = slot;
        welcome.epoch = epoch;
        welcome.run = m->run;
        (void)send_message(&welcome, slot);
        return;
    }
    if (m->type == UDP_INPUT) {
        uint32_t previous_epoch = peer->epoch;
        if (!udp_peer_input(peer, m->epoch, m->sequence, now_ms)) {
            ++transport.view.rejected_packets;
            return;
        }
        if (previous_epoch != peer->epoch)
            memset(&transport.relays[slot], 0, sizeof(transport.relays[slot]));
        transport.view.positions[slot] = m->positions[slot];
        transport.view.online_mask |= (uint8_t)(1u << slot);
        return;
    }
    if (!peer->online || m->epoch != peer->epoch ||
        now_ms - peer->last_seen_ms >= UDP_PEER_STALE_MS) {
        ++transport.view.rejected_packets;
        return;
    }
    if (m->target == 0) {
        if (m->type == UDP_PING) echo_ping(m);
        else receive_benchmark(m, now_us);
        return;
    }
    udp_peer_t *target = &transport.peers[m->target];
    if (!target->online || now_ms - target->last_seen_ms >= UDP_PEER_STALE_MS) {
        if (m->type == UDP_PING) report_echo_error(m);
        else ++transport.view.rejected_packets;
        return;
    }
    bool accepted = m->type == UDP_PING ?
        udp_relay_ping(&transport.relays[m->origin], m, now_ms) :
        udp_relay_reply(&transport.relays[m->origin], m, now_ms);
    if (!accepted) {
        ++transport.view.rejected_packets;
        return;
    }
    (void)udp_protocol_relay(m, transport.tokens[m->target], target->epoch);
    if (send_message(m, m->target) != ERR_OK) report_echo_error(m);
}

static void send_input(void) {
    udp_message_t m = message_base(UDP_INPUT, 0);
    m.sequence = ++transport.input_sequence;
    m.positions[transport.view.slot] = transport.own_position;
    (void)send_message(&m, 0);
}

static void receive_client(const udp_message_t *m, uint32_t now_ms, uint32_t now_us) {
    if (m->type == UDP_WELCOME) {
        if (m->run != transport.client_nonce ||
            (transport.view.phase != WIFI_HANDSHAKE &&
             !(transport.view.phase == WIFI_READY && m->epoch == transport.client_epoch))) {
            ++transport.view.rejected_packets;
            return;
        }
        if (transport.view.phase == WIFI_HANDSHAKE) {
            transport.client_epoch = m->epoch;
            transport.input_sequence = 0;
            transport.state_sequence_valid = false;
            transport.last_host_ms = now_ms;
            transport.view.phase = WIFI_READY;
            transport.view.join_ms = now_ms - transport.started_ms;
            transport.view.online_mask = (uint8_t)(1u | (1u << transport.view.slot));
            status_text("Joined; static IP ready", 0);
            send_input();
        }
        return;
    }
    if (transport.view.phase != WIFI_READY || m->epoch != transport.client_epoch) {
        ++transport.view.rejected_packets;
        return;
    }
    if (m->type == UDP_STATE) {
        if (transport.state_sequence_valid &&
            !udp_sequence_newer(m->sequence, transport.state_sequence)) {
            ++transport.view.rejected_packets;
            return;
        }
        transport.state_sequence_valid = true;
        transport.state_sequence = m->sequence;
        transport.last_host_ms = now_ms;
        memcpy(transport.view.positions, m->positions, sizeof(m->positions));
        transport.view.online_mask = m->online_mask;
    } else if (m->type == UDP_PING) {
        echo_ping(m);
    } else {
        receive_benchmark(m, now_us);
    }
}

static void receive_datagram(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                             const ip_addr_t *address, u16_t port) {
    (void)arg;
    (void)pcb;
    if (!p) return;
    uint8_t bytes[UDP_PROTOCOL_SIZE];
    udp_message_t m;
    uint32_t now_us = time_us_32();
    uint32_t now_ms = clock_ms();
    ++transport.view.rx_packets;
    bool valid = address && IP_IS_V4(address) && p->tot_len == sizeof(bytes) &&
                 pbuf_copy_partial(p, bytes, sizeof(bytes), 0) == sizeof(bytes) &&
                 udp_protocol_decode(&m, bytes, sizeof(bytes));
    uint32_t source = valid ? lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(address))) : 0;
    pbuf_free(p);
    if (!valid ||
        !udp_protocol_endpoint(&m, transport.credentials.session_id,
                               transport.view.slot, transport.tokens, source, port) ||
        (transport.view.phase != WIFI_READY &&
         !(transport.view.phase == WIFI_HANDSHAKE && m.type == UDP_WELCOME))) {
        ++transport.view.rejected_packets;
        return;
    }
    if (transport.view.host) receive_host(&m, now_ms, now_us);
    else receive_client(&m, now_ms, now_us);
}

static bool bind_udp(void) {
    transport.pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (!transport.pcb) {
        fail("UDP allocation failed", ERR_MEM);
        return false;
    }
    ip_addr_t address;
    IP_ADDR4(&address, 192, 168, 4, transport.view.slot + 1u);
    err_t error = udp_bind(transport.pcb, &address, UDP_PROTOCOL_PORT);
    if (error != ERR_OK) {
        udp_remove(transport.pcb);
        transport.pcb = NULL;
        fail("UDP bind failed", error);
        return false;
    }
    udp_recv(transport.pcb, receive_datagram, NULL);
    return true;
}

void wifi_transport_stop(void) {
    if (transport.pcb) udp_remove(transport.pcb);
    if (transport.interface_started) {
        if (transport.view.host) cyw43_arch_disable_ap_mode();
        else cyw43_arch_disable_sta_mode();
    }
    memset(&transport, 0, sizeof(transport));
    for (unsigned i = 0; i < EXP_MAX_PLAYERS; ++i)
        transport.view.positions[i] = EXP_POSITION_MAX / 2u;
    transport.own_position = EXP_POSITION_MAX / 2u;
    transport.view.phase = WIFI_IDLE;
    status_text("Wi-Fi idle", 0);
}

void wifi_transport_init(void) {
    wifi_transport_stop();
}

static size_t bounded_length(const char *text, size_t capacity) {
    const char *end = memchr(text, '\0', capacity);
    return end ? (size_t)(end - text) : capacity;
}

static bool start(const handover_credentials_t *credentials, bool host) {
    /* Copy before stop permits retry even when caller points into our old state. */
    handover_credentials_t copy = {0};
    if (credentials) copy = *credentials;
    wifi_transport_stop();
    size_t ssid_length = bounded_length(copy.ssid, sizeof(copy.ssid));
    size_t password_length = bounded_length(copy.password, sizeof(copy.password));
    if (!credentials || !copy.session_id ||
        (host ? copy.slot != 0 : (copy.slot == 0 || copy.slot >= EXP_MAX_PLAYERS)) ||
        (!host && !copy.token) || copy.channel > 11u ||
        !ssid_length || ssid_length >= sizeof(copy.ssid) ||
        password_length < 8u || password_length >= sizeof(copy.password)) {
        fail("Invalid Wi-Fi credentials", ERROR_CREDENTIALS);
        return false;
    }
    if (!copy.channel) copy.channel = 6;
    if (host && !copy.token)
        copy.token = ((uint64_t)next_generation() << 32) | next_generation();
    transport.credentials = copy;
    transport.view.host = host;
    transport.view.slot = copy.slot;
    transport.tokens[copy.slot] = copy.token;
    transport.started_ms = transport.phase_ms = transport.frame_ms = clock_ms();
    transport.view.phase = host ? WIFI_STARTING_AP : WIFI_JOINING;
    status_text(host ? "Starting WPA2 AP" : "Joining on provisioned channel", 0);
    transport.interface_started = true;
    if (host) {
        cyw43_wifi_ap_set_channel(&cyw43_state, copy.channel);
        cyw43_arch_enable_ap_mode(copy.ssid, copy.password, CYW43_AUTH_WPA2_AES_PSK);
    } else {
        cyw43_arch_enable_sta_mode();
    }
    provision_address();
    if (!wifi_transport_set_power_save(false)) {
        fail("Unable to disable Wi-Fi PM", transport.view.error_code);
        return false;
    }
    status_text(host ? "Starting WPA2 AP" : "Joining on provisioned channel", 0);
    if (!host) {
        transport.client_nonce = next_generation();
        int error = cyw43_wifi_join(&cyw43_state, ssid_length,
                                    (const uint8_t *)copy.ssid, password_length,
                                    (const uint8_t *)copy.password,
                                    CYW43_AUTH_WPA2_AES_PSK, NULL, copy.channel);
        if (error) {
            fail("Wi-Fi join request failed", error);
            return false;
        }
    }
    return true;
}

bool wifi_transport_host(const handover_credentials_t *session) {
    return start(session, true);
}

bool wifi_transport_join(const handover_credentials_t *credentials) {
    return start(credentials, false);
}

void wifi_transport_member(uint8_t slot, uint64_t token) {
    if (!transport.view.host || (transport.view.phase != WIFI_STARTING_AP &&
        transport.view.phase != WIFI_READY) || slot == 0 ||
        slot >= EXP_MAX_PLAYERS || !token) {
        status_text("Invalid member registration", ERROR_MEMBER);
        return;
    }
    for (uint8_t i = 0; i < EXP_MAX_PLAYERS; ++i) {
        if (i != slot && transport.tokens[i] == token) {
            status_text("Duplicate member token", ERROR_MEMBER);
            return;
        }
    }
    if (transport.tokens[slot] && transport.tokens[slot] != token) {
        status_text("Member slot already reserved", ERROR_MEMBER);
        return;
    }
    transport.tokens[slot] = token;
}

void wifi_transport_input(int direction) {
    transport.direction = direction < 0 ? -1 : direction > 0 ? 1 : 0;
}

bool wifi_transport_set_power_save(bool enabled) {
    if (!transport.interface_started || !interface_exists()) {
        status_text("Wi-Fi interface not ready", ERROR_NOT_READY);
        return false;
    }
    int error = cyw43_wifi_pm(&cyw43_state, enabled ? CYW43_DEFAULT_PM : CYW43_NONE_PM);
    if (error) {
        status_text("Power-save change failed", error);
        return false;
    }
    transport.view.power_save = enabled;
    status_text(enabled ? "Power save enabled" : "Power save disabled", 0);
    return true;
}

bool wifi_transport_benchmark(uint8_t target) {
    if (transport.view.phase != WIFI_READY || target >= EXP_MAX_PLAYERS ||
        target == transport.view.slot || !(transport.view.online_mask & (1u << target))) {
        status_text("Benchmark target not online", ERROR_BENCH_TARGET);
        return false;
    }
    udp_benchmark_start(&transport.bench, target, next_generation(), time_us_32());
    update_metrics();
    status_text("Benchmark running", 0);
    return true;
}

void wifi_transport_cancel_benchmark(void) {
    if (transport.bench.metrics.running) {
        udp_benchmark_cancel(&transport.bench);
        update_metrics();
        status_text("Benchmark cancelled", 0);
    }
}

static void broadcast_state(void) {
    ++transport.state_sequence;
    for (uint8_t slot = 1; slot < EXP_MAX_PLAYERS; ++slot) {
        if (!transport.peers[slot].online) continue;
        udp_message_t m = message_base(UDP_STATE, slot);
        m.sequence = transport.state_sequence;
        m.online_mask = transport.view.online_mask;
        memcpy(m.positions, transport.view.positions, sizeof(m.positions));
        (void)send_message(&m, slot);
    }
}

static void poll_ready(uint32_t now_ms) {
    if (!interface_ready()) {
        fail("Wi-Fi link lost; retry", ERROR_LINK_DOWN);
        return;
    }
    if (transport.view.host) {
        transport.view.online_mask = 1;
        for (uint8_t slot = 1; slot < EXP_MAX_PLAYERS; ++slot) {
            udp_peer_t *p = &transport.peers[slot];
            (void)udp_peer_expire(p, now_ms);
            transport.view.peer_age_ms[slot] = p->epoch ? now_ms - p->last_seen_ms : 0;
            if (p->online) transport.view.online_mask |= (uint8_t)(1u << slot);
        }
    } else {
        uint32_t age = now_ms - transport.last_host_ms;
        transport.view.peer_age_ms[0] = age;
        for (uint8_t slot = 1; slot < EXP_MAX_PLAYERS; ++slot) {
            if (slot == transport.view.slot) continue;
            transport.view.peer_age_ms[slot] =
                transport.view.online_mask & (1u << slot) ? age : UDP_PEER_STALE_MS;
        }
        if (age >= UDP_PEER_STALE_MS) {
            fail("Host heartbeat lost; retry", ERROR_HEARTBEAT);
            return;
        }
        if (transport.state_sequence_valid &&
            !(transport.view.online_mask & (1u << transport.view.slot))) {
            fail("Host marked us offline; retry", ERROR_HEARTBEAT);
            return;
        }
    }

    uint32_t elapsed = now_ms - transport.frame_ms;
    if (elapsed > 100u) elapsed = 100u;
    transport.frame_ms = now_ms;
    transport.frame_credit += elapsed * 30u;
    if (transport.frame_credit >= 1000u) {
        /* One newest-value frame, never a catch-up queue after a slow poll. */
        uint32_t movement_ms = transport.frame_credit / 30u;
        transport.frame_credit %= 1000u;
        int position = transport.own_position +
                       transport.direction * (int)(movement_ms * 400u / 1000u);
        if (position < 0) position = 0;
        if (position > (int)EXP_POSITION_MAX) position = EXP_POSITION_MAX;
        transport.own_position = (uint16_t)position;
        if (transport.view.host) {
            transport.view.positions[0] = transport.own_position;
            broadcast_state();
        } else {
            send_input();
        }
    }

    uint32_t now_us = time_us_32();
    bool was_running = transport.bench.metrics.running;
    udp_benchmark_tick(&transport.bench, now_us);
    if (was_running && !transport.bench.metrics.running && !transport.view.error_code)
        status_text("Benchmark complete", 0);
    if (udp_benchmark_due(&transport.bench, now_us)) {
        udp_message_t m = message_base(UDP_PING, transport.bench.metrics.target);
        m.sequence = transport.bench.metrics.sent + 1u;
        m.run = transport.bench.generation;
        m.stamp_us = now_us;
        if (send_message(&m, transport.view.host ? m.target : 0) == ERR_OK)
            udp_benchmark_sent(&transport.bench, now_us);
    }
    update_metrics();
}

void wifi_transport_poll(uint32_t now_ms) {
    if (transport.view.phase == WIFI_IDLE || transport.view.phase == WIFI_ERROR) return;
    if (transport.view.phase == WIFI_READY) {
        poll_ready(now_ms);
        return;
    }
    if (!transport.address_set) provision_address();
    if (!transport.view.host) {
        int link = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
        if (link < 0) {
            fail("Wi-Fi association failed", link);
            return;
        }
    }
    if (transport.view.phase == WIFI_STARTING_AP || transport.view.phase == WIFI_JOINING) {
        if (interface_ready()) {
            if (!bind_udp()) return;
            transport.phase_ms = transport.frame_ms = now_ms;
            if (transport.view.host) {
                transport.view.phase = WIFI_READY;
                transport.view.online_mask = 1;
                transport.view.join_ms = now_ms - transport.started_ms;
                status_text("AP ready; static 192.168.4.1", 0);
                return;
            }
            transport.view.phase = WIFI_HANDSHAKE;
            transport.hello_ms = now_ms - HELLO_INTERVAL_MS;
            status_text("Static IP; waiting for WELCOME", 0);
        } else if (now_ms - transport.started_ms >= WIFI_START_TIMEOUT_MS) {
            fail(transport.view.host ? "AP readiness timed out" : "Wi-Fi join timed out",
                 ERROR_START_TIMEOUT);
            return;
        }
    }
    if (transport.view.phase == WIFI_HANDSHAKE) {
        if (!interface_ready()) {
            fail("Link lost during handshake", ERROR_LINK_DOWN);
            return;
        }
        if (now_ms - transport.phase_ms >= UDP_HANDSHAKE_MS) {
            fail("WELCOME timed out; retry", ERROR_HANDSHAKE);
            return;
        }
        if (now_ms - transport.hello_ms >= HELLO_INTERVAL_MS) {
            transport.hello_ms = now_ms;
            udp_message_t m = message_base(UDP_HELLO, 0);
            m.epoch = 0;
            m.run = transport.client_nonce;
            (void)send_message(&m, 0);
        }
    }
}

const experiment_wifi_view_t *wifi_transport_view(void) {
    return &transport.view;
}

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../radio/wifi_link.h"

typedef struct {
    uint32_t command, size, capacity, response_size, status;
    uint8_t set, iface, error;
    wifi_link_stage_t stage;
    uint8_t request[512], response[512];
} call_t;

typedef struct {
    wifi_link_t link;
    call_t calls[48];
    uint32_t count, used, fail_at, failure_status;
    wifi_link_stage_t stage;
    uint8_t radio_up;
} mock_t;

static const uint8_t nonce[16] = {
    0u, 1u, 0xa2u, 0xffu, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u
};
static const uint8_t ap_mac[6] = {2u, 0x12u, 0x34u, 0x56u, 0x78u, 1u};
static const uint8_t sta_mac[6] = {2u, 0x12u, 0x34u, 0x56u, 0x78u, 2u};
static const char ssid[] = "U26LAB-0001a2ff";
static const char password[] = "0001a2ff0405060708090a0b0c0d0e0f";

static void store(uint8_t *bytes, uint32_t value) {
    for (unsigned i = 0u; i < 4u; ++i) bytes[i] = (uint8_t)(value >> (8u * i));
}

static int control(void *context, uint32_t command, uint8_t set, uint8_t iface,
                    const uint8_t *request, uint32_t size, uint8_t *reply,
                    uint32_t capacity, uint32_t *reply_size) {
    mock_t *mock = context;
    assert(mock->used < mock->count);
    call_t *call = &mock->calls[mock->used++];
    assert(command == call->command && set == call->set && iface == call->iface);
    assert(size == call->size && capacity == call->capacity);
    assert(size <= sizeof(mock->link.buffer) && capacity <= sizeof(mock->link.reply));
    for (uint32_t i = 0u; i < size; ++i) {
        if (request[i] != call->request[i]) {
            fprintf(stderr, "control request mismatch: call %u command %u byte %u\n",
                    mock->used, command, i);
            assert(request[i] == call->request[i]);
        }
    }
    assert(mock->link.report.last_command == command);
    assert(mock->link.report.controller_status == 0u);
    assert(mock->link.report.stage == call->stage);
    assert(mock->link.report.authorized == 0u);
    assert(reply != 0 && reply_size != 0);
    for (uint32_t i = 0u; i < capacity; ++i) assert(reply[i] == 0u);
    if (mock->used == mock->fail_at || call->error) {
        mock->link.report.controller_status =
            call->error ? call->status : mock->failure_status;
        return 0;
    }
    if (command == 3u && set) mock->radio_up = 0u;
    if (command == 2u && set) mock->radio_up = 1u;
    if (command == 263u && set &&
        (strcmp((const char *)request, "apsta") == 0 ||
         strcmp((const char *)request, "country") == 0 ||
         (iface == 0u && strcmp((const char *)request, "cur_etheraddr") == 0)))
        assert(!mock->radio_up);
    uint32_t size_to_copy = call->response_size < capacity ? call->response_size : capacity;
    memcpy(reply, call->response, size_to_copy);
    *reply_size = call->response_size;
    return 1;
}

static void init(mock_t *mock, uint8_t role) {
    memset(mock, 0, sizeof(*mock));
    wifi_link_ops_t ops = {.context = mock, .control = control};
    wifi_link_init(&mock->link, &ops, role, nonce, role == WIFI_LINK_AP ? ap_mac : sta_mac);
    mock->stage = WIFI_LINK_CONFIGURE;
    mock->radio_up = 1u;
    assert(mock->link.report.stage == WIFI_LINK_IDLE);
}

static call_t *expect(mock_t *mock, uint32_t command, uint8_t set, uint8_t iface,
                       uint32_t size, uint32_t capacity) {
    assert(mock->count < sizeof(mock->calls) / sizeof(mock->calls[0]));
    call_t *call = &mock->calls[mock->count++];
    memset(call, 0, sizeof(*call));
    call->command = command;
    call->set = set;
    call->iface = iface;
    call->size = size;
    call->capacity = capacity;
    call->stage = mock->stage;
    return call;
}

static call_t *var(mock_t *mock, const char *name, uint8_t iface, uint32_t extra) {
    uint32_t length = (uint32_t)strlen(name) + 1u;
    call_t *call = expect(mock, 263u, 1u, iface, length + extra, 512u);
    memcpy(call->request, name, length);
    return call;
}

static void var32(mock_t *mock, const char *name, uint8_t iface, uint32_t value) {
    call_t *call = var(mock, name, iface, 4u);
    store(call->request + strlen(name) + 1u, value);
}

static void pair(mock_t *mock, const char *name, uint32_t index, uint32_t value) {
    call_t *call = var(mock, name, 0u, 8u);
    store(call->request + strlen(name) + 1u, index);
    store(call->request + strlen(name) + 5u, value);
}

static void ioctl32(mock_t *mock, uint32_t command, uint8_t iface, uint32_t value) {
    call_t *call = expect(mock, command, 1u, iface, 4u, 512u);
    store(call->request, value);
}

static void mac_set(mock_t *mock, uint8_t iface) {
    call_t *call = var(mock, "cur_etheraddr", iface, 6u);
    memcpy(call->request + 14u, mock->link.report.mac, 6u);
}

static void pmk(mock_t *mock, uint8_t iface) {
    call_t *call = expect(mock, 268u, 1u, iface, 68u, 512u);
    call->request[0] = 32u;
    call->request[2] = 1u;
    memcpy(call->request + 4u, password, 32u);
}

static void configure_expected(mock_t *mock) {
    ioctl32(mock, 3u, 0u, 1u);
    var32(mock, "bus:txglom", 0u, 0u);
    var32(mock, "apsta", 0u, 1u);
    mac_set(mock, 0u);
    call_t *call = var(mock, "country", 0u, 12u);
    memcpy(call->request + 8u, "XX", 2u);
    memset(call->request + 12u, 0xff, 4u);
    memcpy(call->request + 16u, "XX", 2u);
    ioctl32(mock, 64u, 0u, 0u);
    var32(mock, "ampdu_ba_wsize", 0u, 8u);
    var32(mock, "ampdu_mpdu", 0u, 4u);
    var32(mock, "ampdu_rx_factor", 0u, 0u);
    call = var(mock, "bsscfg:event_msgs", 0u, 23u);
    const unsigned events[] = {0u, 1u, 3u, 5u, 6u, 7u, 11u, 12u, 16u, 46u};
    for (unsigned i = 0u; i < sizeof(events) / sizeof(events[0]); ++i)
        call->request[22u + events[i] / 8u] |= (uint8_t)(1u << (events[i] % 8u));
    mock->stage = WIFI_LINK_START;
    expect(mock, 2u, 1u, 0u, 0u, 512u);
    ioctl32(mock, 86u, 0u, 0u);
    uint8_t iface = mock->link.report.role == WIFI_LINK_AP ? 1u : 0u;
    if (iface == 1u) {
        var32(mock, "ampdu_ba_wsize", 0u, 2u);
        call = var(mock, "bsscfg:ssid", 0u, 40u);
        store(call->request + 12u, 1u);
        store(call->request + 16u, 15u);
        memcpy(call->request + 20u, ssid, 15u);
        mac_set(mock, 1u);
        ioctl32(mock, 30u, 0u, 1u);
        pair(mock, "bsscfg:wsec", 1u, 4u);
        var32(mock, "mfp", 1u, 0u);
        pair(mock, "bsscfg:wpa_auth", 1u, 0x80u);
        pmk(mock, 1u);
        ioctl32(mock, 110u, 1u, 1u);
        var32(mock, "2g_mrate", 1u, 22u);
        ioctl32(mock, 78u, 1u, 1u);
        pair(mock, "bss", 1u, 1u);
    } else {
        ioctl32(mock, 134u, 0u, 4u);
        pair(mock, "bsscfg:sup_wpa", 0u, 1u);
        pair(mock, "bsscfg:sup_wpa2_eapver", 0u, 0xffffffffu);
        pair(mock, "bsscfg:sup_wpa_tmo", 0u, 5000u);
        pmk(mock, 0u);
        ioctl32(mock, 20u, 0u, 1u);
        ioctl32(mock, 22u, 0u, 0u);
        var32(mock, "mfp", 0u, 1u);
        ioctl32(mock, 165u, 0u, 0x80u);
        call = var(mock, "join", 0u, 70u);
        store(call->request + 5u, 15u);
        memcpy(call->request + 9u, ssid, 15u);
        memset(call->request + 45u, 0xff, 16u);
        memset(call->request + 61u, 0xff, 6u);
        store(call->request + 69u, 1u);
        call->request[73] = 1u;
        call->request[74] = 0x10u;
    }
    call = expect(mock, 262u, 0u, iface, 20u, 20u);
    memcpy(call->request, "cur_etheraddr", 13u);
    memcpy(call->response, mock->link.report.mac, 6u);
    call->response_size = 6u;
}

static void configured(mock_t *mock, uint8_t role) {
    init(mock, role);
    configure_expected(mock);
    mock->link.report.stage = WIFI_LINK_FIRMWARE;
    assert(wifi_link_configure(&mock->link));
    assert(mock->used == mock->count);
    assert(mock->link.report.stage == WIFI_LINK_WAIT_PEER);
    assert(mock->link.report.error == WIFI_LINK_OK);
    assert(mock->link.report.peer_count == 0u && !mock->link.report.authorized);
    assert(mock->radio_up);
    mock->used = mock->count = 0u;
    mock->stage = WIFI_LINK_WAIT_PEER;
}

static call_t *association(mock_t *mock, uint32_t count) {
    call_t *call;
    if (mock->link.report.role == WIFI_LINK_AP) {
        call = expect(mock, 159u, 0u, 1u, 100u, 100u);
        store(call->request, 16u);
        store(call->response, count);
        call->response_size = 4u;
        for (uint32_t i = 0u; i < count && i < 16u; ++i) {
            memcpy(call->response + 4u + 6u * i, sta_mac, 6u);
            call->response[9u + 6u * i] = (uint8_t)(2u + i);
            call->response_size += 6u;
        }
    } else {
        call = expect(mock, 23u, 0u, 0u, 6u, 6u);
        call->response_size = 6u;
        if (count != 0u) memcpy(call->response, ap_mac, 6u);
    }
    return call;
}

static call_t *auth(mock_t *mock, uint32_t flags) {
    uint8_t iface = mock->link.report.role == WIFI_LINK_AP ? 1u : 0u;
    const uint8_t *peer = iface ? sta_mac : ap_mac;
    call_t *call = expect(mock, 262u, 0u, iface, 512u, 512u);
    memcpy(call->request, "sta_info", 9u);
    memcpy(call->request + 9u, peer, 6u);
    call->response_size = 200u;
    call->response[0] = 4u;
    call->response[2] = 200u;
    store(call->response + 8u, flags);
    memcpy(call->response + 16u, peer, 6u);
    return call;
}

static void pending(const mock_t *mock, uint32_t count) {
    assert(mock->used == mock->count);
    assert(mock->link.report.stage == WIFI_LINK_WAIT_PEER);
    assert(mock->link.report.error == WIFI_LINK_OK);
    assert(mock->link.report.peer_count == count);
    assert(!mock->link.report.authorized);
}

static void test_identity_and_security(void) {
    mock_t ap, station;
    configured(&ap, WIFI_LINK_AP);
    configured(&station, WIFI_LINK_STATION);
    assert(strcmp(ap.link.ssid, ssid) == 0);
    assert(strcmp(ap.link.password, password) == 0);
    assert(strcmp(ap.link.ssid, station.link.ssid) == 0);
    assert(strcmp(ap.link.password, station.link.password) == 0);
    assert(memcmp(ap.link.report.mac, station.link.report.mac, 6u) != 0);
    uint8_t other_nonce[16];
    memcpy(other_nonce, nonce, sizeof(nonce));
    other_nonce[0] ^= 0xffu;
    wifi_link_ops_t ops = {.context = &station, .control = control};
    wifi_link_init(&station.link, &ops, WIFI_LINK_STATION, other_nonce, sta_mac);
    assert(strcmp(ap.link.ssid, station.link.ssid) != 0);
    assert(strcmp(ap.link.password, station.link.password) != 0);
    memset(other_nonce, 0xff, sizeof(other_nonce));
    wifi_link_init(&station.link, &ops, WIFI_LINK_STATION, other_nonce, sta_mac);
    assert(strcmp(station.link.ssid, "U26LAB-ffffffff") == 0);
    assert(strcmp(station.link.password, "ffffffffffffffffffffffffffffffff") == 0);
}

static void test_configuration_failures(void) {
    mock_t mock;
    for (uint8_t role = 0u; role < 2u; ++role) {
        init(&mock, role);
        configure_expected(&mock);
        uint32_t count = mock.count;
        for (uint32_t at = 1u; at <= count; ++at) {
            init(&mock, role);
            configure_expected(&mock);
            mock.fail_at = at;
            mock.failure_status = 0xfffffffdu;
            assert(!wifi_link_configure(&mock.link));
            assert(mock.used == at);
            assert(mock.link.report.last_command == mock.calls[at - 1u].command);
            assert(mock.link.report.controller_status == mock.failure_status);
            assert(mock.link.report.stage == WIFI_LINK_FAILED);
            assert(mock.link.report.error == WIFI_LINK_RADIO_ERROR);
            assert(!mock.link.report.authorized && mock.link.report.peer_count == 0u);
            assert(!wifi_link_poll(&mock.link));
            assert(!wifi_link_configure(&mock.link));
            assert(mock.used == at);
        }
        for (unsigned malformed = 0u; malformed < 3u; ++malformed) {
            init(&mock, role);
            configure_expected(&mock);
            call_t *last = &mock.calls[mock.count - 1u];
            if (malformed == 0u) last->response_size = 5u;
            if (malformed == 1u) last->response_size = 21u;
            if (malformed == 2u) last->response[5] ^= 1u;
            assert(!wifi_link_configure(&mock.link));
            assert(mock.link.report.error == WIFI_LINK_BAD_REPLY);
            assert(mock.link.report.stage == WIFI_LINK_FAILED && !mock.link.report.authorized);
        }
    }
    init(&mock, WIFI_LINK_AP);
    configure_expected(&mock);
    mock.calls[0].response_size = 513u;
    assert(!wifi_link_configure(&mock.link));
    assert(mock.used == 1u && mock.link.report.error == WIFI_LINK_BAD_REPLY);
    init(&mock, WIFI_LINK_AP);
    configure_expected(&mock);
    mock.fail_at = 1u;
    mock.failure_status = 0xfffffffbu;
    assert(!wifi_link_configure(&mock.link));
    assert(mock.used == 1u && mock.link.report.last_command == 3u);
    assert(mock.link.report.controller_status == 0xfffffffbu);
    assert(mock.link.report.error == WIFI_LINK_RADIO_ERROR);
}

static void test_wait_auth_and_disconnect(void) {
    mock_t mock;
    for (uint8_t role = 0u; role < 2u; ++role) {
        configured(&mock, role);
        association(&mock, 0u);
        assert(wifi_link_poll(&mock.link));
        pending(&mock, 0u);
        for (uint32_t flags = 0u; flags <= 0x30u; flags += 8u) {
            association(&mock, 1u);
            auth(&mock, flags);
            uint32_t before = mock.used;
            assert(wifi_link_poll(&mock.link));
            assert(mock.used - before == 2u && mock.used == mock.count);
            if (flags == 0x30u) {
                assert(mock.link.report.stage == WIFI_LINK_CONNECTED);
                assert(mock.link.report.authorized == 1u && mock.link.report.peer_count == 1u);
                assert(memcmp(mock.link.report.peer, role ? ap_mac : sta_mac, 6u) == 0);
            } else pending(&mock, 1u);
        }
        association(&mock, 1u);
        auth(&mock, 0x10u);
        assert(wifi_link_poll(&mock.link));
        pending(&mock, 1u);
        association(&mock, 0u);
        assert(wifi_link_poll(&mock.link));
        pending(&mock, 0u);
        for (unsigned i = 0u; i < 6u; ++i) assert(mock.link.report.peer[i] == 0u);
    }
    configured(&mock, WIFI_LINK_AP);
    association(&mock, 16u);
    auth(&mock, 0x30u);
    assert(wifi_link_poll(&mock.link));
    assert(mock.used == 2u && mock.link.report.peer_count == 16u && mock.link.report.authorized);
}

static void test_expected_and_real_errors(void) {
    mock_t mock;
    configured(&mock, WIFI_LINK_STATION);
    call_t *call = association(&mock, 0u);
    call->error = 1u;
    call->status = 0xffffffefu;
    assert(wifi_link_poll(&mock.link));
    pending(&mock, 0u);
    assert(mock.link.report.controller_status == 0xffffffefu);
    call = association(&mock, 0u);
    call->error = 1u;
    call->status = 0u;
    assert(!wifi_link_poll(&mock.link));
    assert(mock.link.report.controller_status == 0u && mock.link.report.error == WIFI_LINK_RADIO_ERROR);
    for (uint8_t role = 0u; role < 2u; ++role) {
        const uint32_t races[] = {0xffffffefu, 0xffffffe2u};
        for (unsigned i = 0u; i < 2u; ++i) {
            configured(&mock, role);
            association(&mock, 1u);
            call = auth(&mock, 0u);
            call->error = 1u;
            call->status = races[i];
            assert(wifi_link_poll(&mock.link));
            pending(&mock, 0u);
            assert(mock.link.report.controller_status == races[i]);
        }
        const uint32_t errors[] = {0u, 0xffffffe9u, 0xffffffffu, 0x80000005u};
        for (unsigned i = 0u; i < 4u; ++i) {
            for (unsigned query = 0u; query < 2u; ++query) {
                configured(&mock, role);
                call = association(&mock, 1u);
                if (query != 0u) call = auth(&mock, 0u);
                call->error = 1u;
                call->status = errors[i];
                assert(!wifi_link_poll(&mock.link));
                assert(mock.used == mock.count && mock.link.report.stage == WIFI_LINK_FAILED);
                assert(mock.link.report.error == WIFI_LINK_RADIO_ERROR);
                assert(mock.link.report.controller_status == errors[i]);
                assert(!mock.link.report.authorized);
            }
        }
    }
    configured(&mock, WIFI_LINK_AP);
    call = association(&mock, 0u);
    call->error = 1u;
    call->status = 0xffffffefu;
    assert(!wifi_link_poll(&mock.link));
    assert(mock.link.report.error == WIFI_LINK_RADIO_ERROR);
}

static void test_malformed_association(void) {
    mock_t mock;
    for (unsigned which = 0u; which < 10u; ++which) {
        configured(&mock, WIFI_LINK_AP);
        call_t *call = association(&mock, 1u);
        switch (which) {
        case 0u: call->response_size = 3u; break;
        case 1u: call->response_size = 9u; break;
        case 2u: call->response_size = 101u; break;
        case 3u: store(call->response, 17u); break;
        case 4u: store(call->response, 0xffffffffu); break;
        case 5u: memset(call->response + 4u, 0, 6u); break;
        case 6u: memset(call->response + 4u, 0xff, 6u); break;
        case 7u: call->response[4] = 3u; break;
        case 8u: memcpy(call->response + 4u, ap_mac, 6u); break;
        case 9u:
            store(call->response, 2u);
            call->response_size = 16u;
            memcpy(call->response + 10u, call->response + 4u, 6u);
            break;
        }
        assert(!wifi_link_poll(&mock.link));
        assert(mock.used == 1u && mock.link.report.error == WIFI_LINK_BAD_REPLY);
        assert(mock.link.report.stage == WIFI_LINK_FAILED && !mock.link.report.authorized);
    }
    for (unsigned which = 0u; which < 5u; ++which) {
        configured(&mock, WIFI_LINK_STATION);
        call_t *call = association(&mock, 1u);
        switch (which) {
        case 0u: call->response_size = 5u; break;
        case 1u: call->response_size = 7u; break;
        case 2u: memset(call->response, 0xff, 6u); break;
        case 3u: call->response[0] = 3u; break;
        case 4u: memcpy(call->response, sta_mac, 6u); break;
        }
        assert(!wifi_link_poll(&mock.link));
        assert(mock.used == 1u && mock.link.report.error == WIFI_LINK_BAD_REPLY);
    }
}

static void test_malformed_authorization(void) {
    mock_t mock;
    for (uint8_t role = 0u; role < 2u; ++role) {
        for (unsigned which = 0u; which < 10u; ++which) {
            configured(&mock, role);
            association(&mock, 1u);
            call_t *call = auth(&mock, 0x30u);
            switch (which) {
            case 0u: call->response_size = 21u; break;
            case 1u: call->response_size = 513u; break;
            case 2u: call->response[0] = 2u; break;
            case 3u: call->response[0] = 8u; break;
            case 4u: call->response[2] = 21u; break;
            case 5u: call->response[2] = 201u; break;
            case 6u: call->response[3] = 0xffu; break;
            case 7u: call->response[16] ^= 4u; break;
            case 8u: call->response_size = 83u; call->response[2] = 83u; break;
            case 9u: call->response[2] = 83u; break;
            }
            assert(!wifi_link_poll(&mock.link));
            assert(mock.used == 2u && mock.link.report.error == WIFI_LINK_BAD_REPLY);
            assert(mock.link.report.stage == WIFI_LINK_FAILED && !mock.link.report.authorized);
        }
        for (uint8_t version = 3u; version <= 7u; ++version) {
            configured(&mock, role);
            association(&mock, 1u);
            call_t *call = auth(&mock, 0x30u);
            call->response[0] = version;
            assert(wifi_link_poll(&mock.link));
            assert(mock.link.report.stage == WIFI_LINK_CONNECTED && mock.link.report.authorized);
        }
    }
}

static void test_bad_configuration(void) {
    mock_t mock;
    wifi_link_ops_t ops = {.context = &mock, .control = control};
    uint8_t invalid_mac[6] = {0u};
    wifi_link_init(0, &ops, WIFI_LINK_AP, nonce, ap_mac);
    assert(!wifi_link_configure(0) && !wifi_link_poll(0));
    for (unsigned which = 0u; which < 8u; ++which) {
        memset(&mock, 0, sizeof(mock));
        wifi_link_ops_t selected_ops = ops;
        if (which == 4u) selected_ops.control = 0;
        if (which == 6u) invalid_mac[0] = 3u;
        if (which == 7u) invalid_mac[0] = 4u;
        wifi_link_init(&mock.link, which == 0u ? 0 : &selected_ops,
                       which == 1u ? 2u : WIFI_LINK_AP,
                       which == 2u ? 0 : nonce,
                       which == 3u ? 0 : which >= 5u ? invalid_mac : ap_mac);
        assert(mock.link.report.stage == WIFI_LINK_FAILED);
        assert(mock.link.report.error == WIFI_LINK_BAD_CONFIG);
        assert(!wifi_link_configure(&mock.link) && !wifi_link_poll(&mock.link));
        assert(mock.used == 0u);
    }
    init(&mock, WIFI_LINK_AP);
    assert(!wifi_link_poll(&mock.link));
    assert(mock.link.report.error == WIFI_LINK_BAD_CONFIG && mock.used == 0u);
    configured(&mock, WIFI_LINK_AP);
    assert(!wifi_link_configure(&mock.link));
    assert(mock.link.report.error == WIFI_LINK_BAD_CONFIG && mock.used == 0u);
    for (int stage = WIFI_LINK_IDLE; stage <= WIFI_LINK_FAILED; ++stage)
        assert(strcmp(wifi_link_stage_name((wifi_link_stage_t)stage), "UNKNOWN") != 0);
    for (int error = WIFI_LINK_OK; error <= WIFI_LINK_BAD_CONFIG; ++error)
        assert(strcmp(wifi_link_error_name((wifi_link_error_t)error), "UNKNOWN") != 0);
    assert(strcmp(wifi_link_stage_name((wifi_link_stage_t)99), "UNKNOWN") == 0);
    assert(strcmp(wifi_link_error_name((wifi_link_error_t)99), "UNKNOWN") == 0);
}

int main(void) {
    test_identity_and_security();
    test_configuration_failures();
    test_wait_auth_and_disconnect();
    test_expected_and_real_errors();
    test_malformed_association();
    test_malformed_authorization();
    test_bad_configuration();
    puts("wifi link tests passed");
    return 0;
}

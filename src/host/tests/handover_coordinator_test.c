#include <assert.h>
#include <stdio.h>
#include <string.h>

#define WIFI_HANDOVER_HOST_TEST
#include "../../sdk/wifi_handover/main.c"

static uint32_t mock_now;
static uint32_t random_counter;
static uint32_t member_calls;
static uint32_t host_calls;
static uint32_t join_calls;
static uint32_t ble_host_calls;
static experiment_wifi_view_t mock_wifi;
static ble_provision_view_t mock_ble;
static handover_ui_view_t rendered;
static handover_credentials_t joined;
static bool mock_ble_busy;

uint32_t handover_test_now_ms(void) { return mock_now; }
uint32_t get_rand_32(void) { return ++random_counter; }
const ble_provision_view_t *ble_provision_view(void) { return &mock_ble; }
void ble_provision_stop(void) { mock_ble.phase = BLE_IDLE; }
void ble_provision_poll(uint32_t now) { (void)now; }
void ble_provision_scan(void) {
    mock_ble.error_code = mock_ble_busy ? BLE_PROVISION_ERR_BUSY : 0;
    if (!mock_ble_busy) mock_ble.phase = BLE_SCANNING;
}
void ble_provision_next(void) { mock_ble.selected = (uint8_t)((mock_ble.selected + 1u) % 2u); }
void ble_provision_connect(void) { mock_ble.phase = BLE_CONNECTING; }
void ble_provision_host(uint32_t id) {
    ++ble_host_calls;
    assert(mock_wifi.phase == WIFI_READY);
    assert(id == session.session_id && id != 0u);
    mock_ble.error_code = mock_ble_busy ? BLE_PROVISION_ERR_BUSY : 0;
    if (mock_ble_busy) return;
    mock_ble.phase = BLE_ADVERTISING;
}
void wifi_transport_stop(void) { mock_wifi = (experiment_wifi_view_t){0}; }
bool wifi_transport_host(const handover_credentials_t *credentials) {
    ++host_calls;
    assert(credentials->slot == 0u && credentials->session_id != 0u);
    assert(strlen(credentials->password) == 32u);
    assert(strlen(credentials->ssid) < EXP_SSID_SIZE);
    mock_wifi.host = true;
    mock_wifi.phase = WIFI_STARTING_AP;
    return true;
}
bool wifi_transport_join(const handover_credentials_t *credentials) {
    ++join_calls;
    joined = *credentials;
    mock_wifi.slot = credentials->slot;
    mock_wifi.phase = WIFI_JOINING;
    return true;
}
void wifi_transport_member(uint8_t slot, uint64_t token) {
    assert(slot > 0u && slot < 4u && token != 0u);
    ++member_calls;
}
void wifi_transport_input(int direction) { assert(direction >= -1 && direction <= 1); }
void wifi_transport_poll(uint32_t now) { (void)now; }
bool wifi_transport_set_power_save(bool enabled) { mock_wifi.power_save = enabled; return true; }
bool wifi_transport_benchmark(uint8_t target) {
    assert(target != mock_wifi.slot);
    mock_wifi.metrics.running = true;
    return true;
}
void wifi_transport_cancel_benchmark(void) { mock_wifi.metrics.running = false; }
const experiment_wifi_view_t *wifi_transport_view(void) { return &mock_wifi; }
void handover_ui_publish(const handover_ui_view_t *view) { rendered = *view; }

static void test_host_policy(void) {
    radio_ready = true;
    mock_ble.stack_ready = true;
    handle_control(CONTROL_LEFT);
    assert(role == ROLE_HOST && host_calls == 1u);
    handover_credentials_t offered[3];
    const uint8_t addresses[4][6] = {{1,2,3,4,5,1}, {1,2,3,4,5,2},
                                    {1,2,3,4,5,3}, {1,2,3,4,5,4}};
    assert(!offer_credentials(addresses[0], &offered[0]));
    assert(ble_host_calls == 0u);
    mock_wifi.phase = WIFI_READY;
    mock_wifi.online_mask = 1u;
    mock_ble_busy = true;
    service_transports(mock_now);
    assert(!admission_open && !host_published);
    mock_ble_busy = false;
    mock_now += 100u;
    service_transports(mock_now);
    assert(admission_open && ble_host_calls == 2u);
    for (unsigned i = 0u; i < 3u; ++i) {
        assert(offer_credentials(addresses[i], &offered[i]));
        assert(offered[i].slot == i + 1u);
        assert(offered[i].session_id == session.session_id);
        assert(strcmp(offered[i].ssid, session.ssid) == 0);
        assert(strcmp(offered[i].password, session.password) == 0);
        if (i != 0u) assert(offered[i].token != offered[i - 1u].token);
    }
    assert(member_calls == 3u);
    handover_credentials_t again;
    assert(offer_credentials(addresses[0], &again));
    assert(again.slot == 1u && again.token == offered[0].token && member_calls == 3u);
    assert(!offer_credentials(addresses[3], &again));
    publish_view();
    assert(rendered.mode == UI_SESSION && rendered.reserved_mask == 14u);
    assert(strstr(rendered.status, session.password) == NULL);
    mock_wifi.online_mask = 15u;
    service_transports(++mock_now);
    assert(!admission_open);
    assert(!offer_credentials(addresses[0], &again));
    mock_wifi.online_mask = 7u;
    mock_now = UINT32_MAX - 30000u;
    open_admission();
    service_transports(mock_now);
    assert(offer_credentials(addresses[0], &again));
    mock_now += 60000u;
    assert(!offer_credentials(addresses[0], &again));
    service_transports(mock_now);
    assert(!admission_open);
    reset_role();
}

static void test_client_policy(void) {
    mock_now = 100u;
    start_scan();
    mock_ble_busy = true;
    service_transports(mock_now);
    assert(ble_request == BLE_REQUEST_SCAN);
    mock_ble_busy = false;
    mock_now += 100u;
    service_transports(mock_now);
    assert(mock_ble.phase == BLE_SCANNING);
    mock_ble.host_count = 1u;
    mock_ble.session_id = 0x12345678u;
    handle_control(CONTROL_RIGHT);
    assert(role == ROLE_CLIENT && provisioning);
    assert(mock_ble.phase == BLE_CONNECTING && !credentials_ready);
    handover_credentials_t credentials = {
        .session_id = 0x12345679u, .slot = 2u, .channel = 6u, .token = 123u,
        .ssid = "U26W-12345678", .password = "0123456789ABCDEF0123456789ABCDEF"
    };
    received_credentials(&credentials);
    assert(!credentials_ready && !wifi_join_pending);
    credentials.session_id = expected_session;
    received_credentials(&credentials);
    assert(credentials_ready && wifi_join_pending && join_calls == 0u);
    service_transports(mock_now);
    assert(join_calls == 1u && joined.slot == 2u && mock_wifi.phase == WIFI_JOINING);
    assert(!wifi_join_pending);
    mock_wifi.phase = WIFI_READY;
    mock_now = 3456u;
    service_transports(mock_now);
    assert(handover_timed && handover_ms == 3256u);
    publish_view();
    assert(rendered.mode == UI_PADDLES);
    handle_control(CONTROL_BOTH);
    assert(online_screen == UI_METRICS);
    handle_control(CONTROL_LEFT);
    assert(mock_wifi.metrics.running);
    handle_control(CONTROL_RIGHT);
    assert(!mock_wifi.metrics.running && metric_target == 1u);
    handle_control(CONTROL_RIGHT);
    assert(metric_target == 3u);
    handle_control(CONTROL_BOTH);
    assert(online_screen == UI_SESSION);
    handle_control(CONTROL_LEFT);
    assert(mock_wifi.power_save);
    mock_wifi.phase = WIFI_ERROR;
    publish_view();
    assert(rendered.mode == UI_ERROR);
    handle_control(CONTROL_LEFT);
    assert(join_calls == 2u);
    reset_role();
    received_credentials(&credentials);
    assert(!credentials_ready && !wifi_join_pending && role == ROLE_NONE);
    start_scan();
    mock_ble_busy = true;
    mock_now += 15000u;
    service_transports(mock_now);
    publish_view();
    assert(rendered.mode == UI_ERROR && app_error[0] != '\0');
    assert(ble_request == BLE_REQUEST_NONE);
    mock_ble_busy = false;
    reset_role();
    mock_ble.stack_ready = false;
    mock_ble.phase = BLE_ERROR;
    publish_view();
    assert(rendered.mode == UI_ERROR && rendered.fatal_error);
}

int main(void) {
    test_host_policy();
    test_client_policy();
    puts("handover coordinator: admission, capacity, slot reuse, handover, retry, UI policy PASS");
    return 0;
}

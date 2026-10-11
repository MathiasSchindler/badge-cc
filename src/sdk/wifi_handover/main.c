#include <stdio.h>
#include <string.h>

#include "ble_provision.h"
#include "controls.h"
#include "ui.h"
#include "wifi_transport.h"
#ifdef WIFI_HANDOVER_HOST_TEST
uint32_t handover_test_now_ms(void);
uint32_t get_rand_32(void);
#else
#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "pico/stdlib.h"
#endif

#ifndef EXPERIMENT_BUTTON_LEFT
#define EXPERIMENT_BUTTON_LEFT 7u
#endif
#ifndef EXPERIMENT_BUTTON_RIGHT
#define EXPERIMENT_BUTTON_RIGHT 9u
#endif

typedef enum { ROLE_NONE, ROLE_HOST, ROLE_CLIENT } role_t;
typedef enum { BLE_REQUEST_NONE, BLE_REQUEST_SCAN, BLE_REQUEST_HOST } ble_request_t;
typedef struct {
    bool reserved;
    uint8_t address[6];
    handover_credentials_t credentials;
} reservation_t;

static role_t role;
static handover_credentials_t session;
static reservation_t reservations[EXP_MAX_PLAYERS - 1u];
static handover_controls_t controls;
static handover_ui_mode_t online_screen = UI_SESSION;
static bool radio_ready;
static bool credentials_ready;
static bool wifi_join_pending;
static bool host_published;
static bool admission_open;
static bool provisioning;
static bool handover_timed;
static uint32_t admission_deadline;
static uint32_t handover_started;
static uint32_t handover_ms;
static uint32_t expected_session;
static uint8_t metric_target;
static ble_request_t ble_request;
static uint32_t ble_request_deadline;
static uint32_t ble_retry_at;
static char app_error[40];

static uint32_t now_ms(void) {
#ifdef WIFI_HANDOVER_HOST_TEST
    return handover_test_now_ms();
#else
    return to_ms_since_boot(get_absolute_time());
#endif
}

static uint64_t random_token(void) {
    uint64_t value = (uint64_t)get_rand_32() << 32u | get_rand_32();
    return value == 0u ? 1u : value;
}

static void new_session(void) {
    memset(&session, 0, sizeof(session));
    session.session_id = get_rand_32();
    if (session.session_id == 0u) session.session_id = 1u;
    session.token = random_token();
    session.channel = 6u;
    snprintf(session.ssid, sizeof(session.ssid), "U26W-%08lX",
             (unsigned long)session.session_id);
    for (unsigned i = 0u; i < 8u; ++i) {
        snprintf(session.password + i * 4u, 5u, "%04X", (unsigned)(get_rand_32() & 0xffffu));
    }
    memset(reservations, 0, sizeof(reservations));
}

static bool offer_credentials(const uint8_t address[6], handover_credentials_t *out) {
    if (role != ROLE_HOST || !admission_open ||
        (int32_t)(now_ms() - admission_deadline) >= 0 ||
        wifi_transport_view()->phase != WIFI_READY) return false;
    for (unsigned i = 0u; i < EXP_MAX_PLAYERS - 1u; ++i) {
        if (reservations[i].reserved && memcmp(reservations[i].address, address, 6u) == 0) {
            *out = reservations[i].credentials;
            return true;
        }
    }
    for (unsigned i = 0u; i < EXP_MAX_PLAYERS - 1u; ++i) {
        if (reservations[i].reserved) continue;
        reservations[i].reserved = true;
        memcpy(reservations[i].address, address, 6u);
        reservations[i].credentials = session;
        reservations[i].credentials.slot = (uint8_t)(i + 1u);
        reservations[i].credentials.token = random_token();
        *out = reservations[i].credentials;
        wifi_transport_member(out->slot, out->token);
        return true;
    }
    return false;
}

static void received_credentials(const handover_credentials_t *credentials) {
    if (role != ROLE_CLIENT || !provisioning ||
        credentials->session_id != expected_session) return;
    session = *credentials;
    credentials_ready = true;
    wifi_join_pending = true;
    provisioning = false;
}

static void reset_role(void) {
    provisioning = false;
    admission_open = false;
    credentials_ready = false;
    wifi_join_pending = false;
    host_published = false;
    handover_timed = false;
    handover_ms = 0u;
    expected_session = 0u;
    ble_request = BLE_REQUEST_NONE;
    app_error[0] = '\0';
    ble_provision_stop();
    wifi_transport_stop();
    memset(&session, 0, sizeof(session));
    role = ROLE_NONE;
    controls.release_required = 1u;
}

static void start_host(void) {
    reset_role();
    role = ROLE_HOST;
    online_screen = UI_SESSION;
    metric_target = 1u;
    new_session();
    wifi_transport_host(&session);
}

static void start_scan(void) {
    reset_role();
    role = ROLE_CLIENT;
    metric_target = 0u;
    online_screen = UI_PADDLES;
    ble_request = BLE_REQUEST_SCAN;
    ble_request_deadline = now_ms() + 15000u;
    ble_retry_at = now_ms();
}

static void open_admission(void) {
    if (role != ROLE_HOST || wifi_transport_view()->phase != WIFI_READY) return;
    ble_request = BLE_REQUEST_HOST;
    ble_request_deadline = now_ms() + 15000u;
    ble_retry_at = now_ms();
}

static void service_ble_request(uint32_t now) {
    if (ble_request == BLE_REQUEST_NONE) return;
    if ((int32_t)(now - ble_request_deadline) >= 0) {
        snprintf(app_error, sizeof(app_error), "BLE teardown timed out; retry");
        ble_request = BLE_REQUEST_NONE;
        return;
    }
    if ((int32_t)(now - ble_retry_at) < 0) return;
    if (ble_request == BLE_REQUEST_SCAN) ble_provision_scan();
    else ble_provision_host(session.session_id);
    const ble_provision_view_t *ble = ble_provision_view();
    if (ble->error_code == BLE_PROVISION_ERR_BUSY) {
        ble_retry_at = now + 100u;
        return;
    }
    if (ble->error_code != 0 ||
        (ble_request == BLE_REQUEST_SCAN && ble->phase != BLE_SCANNING) ||
        (ble_request == BLE_REQUEST_HOST && ble->phase != BLE_ADVERTISING)) {
        snprintf(app_error, sizeof(app_error), "%s", ble->status);
    } else if (ble_request == BLE_REQUEST_HOST) {
        admission_open = true;
        admission_deadline = now + 60000u;
        host_published = true;
    }
    ble_request = BLE_REQUEST_NONE;
}

static void next_target(void) {
    const experiment_wifi_view_t *wifi = wifi_transport_view();
    wifi_transport_cancel_benchmark();
    for (unsigned i = 0u; i < EXP_MAX_PLAYERS; ++i) {
        metric_target = (uint8_t)((metric_target + 1u) % EXP_MAX_PLAYERS);
        if (metric_target != wifi->slot) return;
    }
}

static void handle_control(handover_control_event_t event) {
    const experiment_wifi_view_t *wifi = wifi_transport_view();
    const ble_provision_view_t *ble = ble_provision_view();
    if (!radio_ready || !ble->stack_ready || event == CONTROL_NONE) return;
    if (event == CONTROL_BOTH) {
        if (wifi->phase == WIFI_READY) {
            online_screen = online_screen == UI_PADDLES ? UI_METRICS :
                            online_screen == UI_METRICS ? UI_SESSION : UI_PADDLES;
            wifi_transport_input(0);
        } else reset_role();
        return;
    }
    if (role == ROLE_NONE) {
        if (event == CONTROL_LEFT) start_host();
        else start_scan();
    } else if (app_error[0] != '\0' || wifi->phase == WIFI_ERROR ||
               (ble->phase == BLE_ERROR && !credentials_ready && role == ROLE_CLIENT)) {
        app_error[0] = '\0';
        if (event == CONTROL_RIGHT) reset_role();
        else if (role == ROLE_HOST) start_host();
        else if (credentials_ready) {
            handover_started = now_ms();
            handover_timed = false;
            wifi_transport_join(&session);
        } else start_scan();
    } else if (wifi->phase == WIFI_READY) {
        if (online_screen == UI_METRICS) {
            if (event == CONTROL_RIGHT) next_target();
            else if (wifi->metrics.running) wifi_transport_cancel_benchmark();
            else wifi_transport_benchmark(metric_target);
        } else if (online_screen == UI_SESSION) {
            if (event == CONTROL_LEFT) wifi_transport_set_power_save(!wifi->power_save);
            else if (role == ROLE_HOST) open_admission();
        }
    } else if (role == ROLE_CLIENT && !credentials_ready && ble->phase == BLE_SCANNING) {
        if (event == CONTROL_LEFT) ble_provision_next();
        else if (ble->host_count != 0u) {
            expected_session = ble->session_id;
            provisioning = true;
            handover_started = now_ms();
            handover_timed = false;
            ble_provision_connect();
        }
    }
}

static void publish_view(void) {
    const experiment_wifi_view_t *wifi = wifi_transport_view();
    const ble_provision_view_t *ble = ble_provision_view();
    handover_ui_view_t view = {0};
    view.wifi = *wifi;
    view.host = role == ROLE_HOST;
    view.admission_open = admission_open;
    view.credentials_ready = credentials_ready;
    view.metric_target = metric_target;
    view.session_id = role == ROLE_HOST || credentials_ready ? session.session_id : ble->session_id;
    view.host_count = ble->host_count;
    view.selected_host = ble->selected;
    view.rssi = ble->rssi;
    view.channel = session.channel;
    view.handover_ms = handover_ms;
    snprintf(view.ssid, sizeof(view.ssid), "%s", session.ssid);
    if (admission_open && (int32_t)(admission_deadline - now_ms()) > 0) {
        view.admission_seconds = (admission_deadline - now_ms() + 999u) / 1000u;
    }
    for (unsigned i = 0u; i < EXP_MAX_PLAYERS - 1u; ++i) {
        if (reservations[i].reserved) view.reserved_mask |= (uint8_t)(1u << (i + 1u));
    }
    if (ble->phase == BLE_ERROR && !ble->stack_ready) {
        view.mode = UI_ERROR;
        view.fatal_error = true;
        snprintf(view.status, sizeof(view.status), "%s", ble->status);
        snprintf(view.detail, sizeof(view.detail), "RESET BADGE TO RETRY");
    } else if (!radio_ready || !ble->stack_ready) {
        view.mode = UI_STARTUP;
        snprintf(view.status, sizeof(view.status), "%s", ble->status);
    } else if (role == ROLE_NONE) {
        view.mode = UI_ROLE;
    } else if (app_error[0] != '\0' || wifi->phase == WIFI_ERROR ||
               (ble->phase == BLE_ERROR && !credentials_ready && role == ROLE_CLIENT)) {
        view.mode = UI_ERROR;
        snprintf(view.status, sizeof(view.status), "%s",
                 app_error[0] != '\0' ? app_error :
                 wifi->phase == WIFI_ERROR ? wifi->status : ble->status);
        snprintf(view.detail, sizeof(view.detail), "ERROR CODE %d",
                 wifi->phase == WIFI_ERROR ? wifi->error_code : ble->error_code);
    } else if (wifi->phase == WIFI_READY) {
        view.mode = online_screen;
        snprintf(view.status, sizeof(view.status), "%s", wifi->status);
        if (role == ROLE_HOST && ble->phase == BLE_ERROR) {
            snprintf(view.status, sizeof(view.status), "%s", ble->status);
        }
    } else if (role == ROLE_CLIENT && !credentials_ready && ble->phase == BLE_SCANNING) {
        view.mode = UI_DISCOVERY;
        snprintf(view.status, sizeof(view.status), "%s", ble->status);
    } else {
        view.mode = UI_PROGRESS;
        snprintf(view.status, sizeof(view.status), "%s",
                 role == ROLE_CLIENT && !credentials_ready ? ble->status : wifi->status);
        snprintf(view.detail, sizeof(view.detail), "SESSION %08lX", (unsigned long)view.session_id);
    }
    handover_ui_publish(&view);
}

static void service_transports(uint32_t now) {
    ble_provision_poll(now);
    if (wifi_join_pending) {
        wifi_join_pending = false;
        ble_provision_stop();
        wifi_transport_join(&session);
    }
    wifi_transport_poll(now);
    const experiment_wifi_view_t *wifi = wifi_transport_view();
    if (role == ROLE_HOST && wifi->phase == WIFI_READY && !host_published &&
        ble_request == BLE_REQUEST_NONE && app_error[0] == '\0') open_admission();
    service_ble_request(now);
    if (admission_open && (wifi->phase != WIFI_READY ||
        (int32_t)(now - admission_deadline) >= 0 || wifi->online_mask == 0x0fu)) {
        admission_open = false;
        ble_provision_stop();
    }
    if (role == ROLE_CLIENT && credentials_ready && wifi->phase == WIFI_READY && !handover_timed) {
        handover_ms = now - handover_started;
        handover_timed = true;
    }
}

#ifndef WIFI_HANDOVER_HOST_TEST
void __attribute__((noreturn)) mp_pico_panic(const char *format, ...) {
    (void)format;
    handover_ui_panic();
    while (true) tight_loop_contents();
}

int main(void) {
    handover_ui_init();
    if (cyw43_arch_init() != 0) {
        handover_ui_view_t failure = {0};
        failure.mode = UI_ERROR;
        failure.fatal_error = true;
        snprintf(failure.status, sizeof(failure.status), "RADIO INITIALIZATION FAILED");
        snprintf(failure.detail, sizeof(failure.detail), "RESET BADGE TO RETRY");
        handover_ui_publish(&failure);
        while (true) sleep_ms(1000u);
    }
    radio_ready = true;
    gpio_init(EXPERIMENT_BUTTON_LEFT);
    gpio_set_dir(EXPERIMENT_BUTTON_LEFT, GPIO_IN);
    gpio_pull_up(EXPERIMENT_BUTTON_LEFT);
    gpio_init(EXPERIMENT_BUTTON_RIGHT);
    gpio_set_dir(EXPERIMENT_BUTTON_RIGHT, GPIO_IN);
    gpio_pull_up(EXPERIMENT_BUTTON_RIGHT);
    controls.release_required = 1u;
    wifi_transport_init();
    ble_provision_init(offer_credentials, received_credentials);
    uint32_t last_ui = 0u;
    while (true) {
        cyw43_arch_poll();
        uint32_t now = now_ms();
        service_transports(now);
        uint8_t raw = (!gpio_get(EXPERIMENT_BUTTON_LEFT) ? 1u : 0u) |
                      (!gpio_get(EXPERIMENT_BUTTON_RIGHT) ? 2u : 0u);
        handover_control_event_t event = handover_controls_update(&controls, raw, time_us_32());
        handle_control(event);
        wifi_transport_input(wifi_transport_view()->phase == WIFI_READY &&
                             online_screen == UI_PADDLES ?
                             handover_controls_direction(&controls) : 0);
        if (event != CONTROL_NONE || now - last_ui >= 100u) {
            publish_view();
            last_ui = now;
        }
        sleep_us(500u);
    }
}
#endif

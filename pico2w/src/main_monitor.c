#include "rp2350/bootrom.h"
#include "usb/hardware.h"
#include "lora/hardware.h"
#include "lora/receiver.h"
#include "lora/packet_record.h"

_Static_assert(LORA_PACKET_RECORD_SIZE == USB_LORA_PACKET_SIZE, "packet record size");
#include "wifi/wifi_station.h"
#include "app/dashboard.h"
#include "net/network.h"
#include "wifi_credentials_generated.h"
#include "build_id.h"
#ifdef PICO2W_REMOTE
#include "app/health.h"
static health_t health;
static uint8_t radio_busy = 1u;
static uint32_t radio_retry_ms, wifi_retry_ms;
static void remote_service(void);
#endif

static usb_control_t control;
static network_t network;
static dashboard_t dashboard;
static sx1262_receiver_t receiver;
static activity_t activity;
static uint8_t frame[NET_FRAME_MAX];
static uint32_t last_us;
static uint32_t remainder_us;
static uint32_t uptime_ms;
static uint8_t usb_ready;
static uint8_t network_ready;
static uint8_t wifi_started;
static uint32_t last_packet_sequence;
volatile int32_t pico2w_error;
volatile int32_t pico2w_lora_error;
volatile uint32_t pico2w_stage;

static void recover(int32_t error) __attribute__((noreturn));
static void recover(int32_t error) {
    pico2w_error = error;
    pico2w_stage = 4u;
    sx1262_hw_release();
    if (usb_ready) usb_hw_disconnect();
    int32_t reboot_error = rp2350_schedule_bootsel(10u);
    if (reboot_error != 0) pico2w_error = reboot_error;
    while (1) __asm__ volatile ("wfi");
}

static void time_update(void) {
    uint32_t now = sx1262_hw_time_us();
    uint32_t elapsed = now - last_us;
    last_us = now;
    uptime_ms += elapsed / 1000u;
    remainder_us += elapsed % 1000u;
    if (remainder_us >= 1000u) {
        ++uptime_ms;
        remainder_us -= 1000u;
    }
}

static void publish_status(void) {
    uint8_t *status = control.app_status;
    for (uint32_t i = 0u; i < USB_REPORT_SIZE; ++i) status[i] = 0u;
    usb_store_u32(status, 0x53573250u);
    status[4] = 1u;
    status[6] = USB_REPORT_SIZE;
    usb_store_u32(status + 8u, PICO2W_BUILD_ID);
    usb_store_u32(status + 12u, (uint32_t)wifi_station_stage());
    usb_store_u32(status + 16u, (uint32_t)wifi_station_error());
    usb_store_u32(status + 20u, network.dhcp_state);
    usb_store_u32(status + 24u, network.error);
    usb_store_u32(status + 28u, network.ip);
    usb_store_u32(status + 32u, (uint32_t)pico2w_lora_error);
    usb_store_u32(status + 36u, receiver.counters.packets);
    usb_store_u32(status + 40u, network.dropped);
    usb_store_u32(status + 44u, network.transmitted);
    usb_store_u32(status + 48u, uptime_ms);
    usb_store_u32(status + 52u, wifi_station_link_up() ? 1u : 0u);
    usb_store_u32(status + 56u, wifi_station_controller_status());
    usb_store_u32(status + 60u, usb_crc32(status, 60u));
    control.app_status_valid = 1u;
    const wifi_station_diagnostic_t *diagnostic = wifi_station_diagnostic();
    status = control.wifi_status;
    for (uint32_t i = 0u; i < USB_REPORT_SIZE; ++i) status[i] = 0u;
    usb_store_u32(status, 0x44573250u);
    status[4] = 1u;
    status[6] = USB_REPORT_SIZE;
    usb_store_u32(status + 8u, PICO2W_BUILD_ID);
    usb_store_u32(status + 12u, diagnostic->step);
    usb_store_u32(status + 16u, diagnostic->address);
    usb_store_u32(status + 20u, diagnostic->expected);
    usb_store_u32(status + 24u, diagnostic->actual);
    usb_store_u32(status + 28u, diagnostic->completed);
    usb_store_u32(status + 32u, diagnostic->command);
    usb_store_u32(status + 36u, (uint32_t)wifi_station_stage());
    usb_store_u32(status + 40u, (uint32_t)wifi_station_error());
    usb_store_u32(status + 44u, wifi_station_controller_status());
    usb_store_u32(status + 48u, diagnostic->event_type);
    usb_store_u32(status + 52u, diagnostic->event_status);
    usb_store_u32(status + 56u, diagnostic->event_reason);
    usb_store_u32(status + 60u, usb_crc32(status, 60u));
    control.wifi_status_valid = 1u;
}

static int32_t service(void) {
    time_update();
#ifdef PICO2W_REMOTE
    if (!radio_busy) remote_service();
#endif
    if (usb_ready) {
        publish_status();
        int32_t error = usb_hw_poll(&control);
        if (error != 0 || control.reboot_requested) recover(error);
    }
    return 0;
}

static void wifi_service(void *context) {
    (void)context;
    (void)service();
}

static int send_frame(void *context, const uint8_t *data, uint32_t length) {
    (void)context;
    return wifi_station_send(data, length);
}

static int32_t configure_radio(void *context, const dashboard_config_t *config) {
    (void)context;
    if (activity.running) return SX1262_RX_BUSY_ERROR;
    sx1262_rx_config_t radio_config;
    radio_config.frequency_hz = config->frequency_hz;
    radio_config.spreading_factor = config->sf;
    radio_config.bandwidth_khz = config->bandwidth_khz;
    radio_config.profile = config->profile;
    pico2w_lora_error = sx1262_receiver_configure(&receiver, &radio_config);
    return pico2w_lora_error;
}

static int32_t control_activity(void *context, uint8_t start) {
    (void)context;
    int32_t error = start ? activity_start(&activity, &receiver, uptime_ms) :
                            activity_stop(&activity, &receiver, uptime_ms);
    pico2w_lora_error = receiver.state == SX1262_RX_RUNNING ? 0 : receiver.last_error;
    dashboard.config.frequency_hz = receiver.active.frequency_hz;
    dashboard.config.sf = receiver.active.spreading_factor;
    dashboard.config.bandwidth_khz = receiver.active.bandwidth_khz;
    dashboard.config.profile = receiver.active.profile;
    dashboard.uptime_ms = uptime_ms;
    return error;
}

static void packet_source(void *context, uint32_t after_sequence, uint8_t *record) {
    (void)context;
    lora_packet_record(record, PICO2W_BUILD_ID, &receiver, after_sequence);
}

#ifdef PICO2W_REMOTE
static void remote_service(void) {
    radio_busy = 1u;
    if (receiver.state == SX1262_RX_RUNNING) {
        uint32_t rearms = receiver.counters.rearms;
        int32_t error = sx1262_receiver_poll(&receiver, uptime_ms);
        if (!error && rearms != receiver.counters.rearms) error = link_test_configure(&receiver);
        if (error) pico2w_lora_error = error;
    } else if (uptime_ms - radio_retry_ms >= HEALTH_MIN_MS) {
        radio_retry_ms = uptime_ms;
        pico2w_lora_error = link_test_configure(&receiver);
    }
    const sx1262_rx_packet_t *p = sx1262_receiver_packet(&receiver, 0u);
    if (p && p->sequence != health.rx_sequence) {
        health.rx_sequence = p->sequence;
        if (!(p->flags & SX1262_RX_CRC_ERROR) && p->length == LINK_PACKET_SIZE &&
            (usb_load_u32(p->payload) == LINK_PACKET_MAGIC || usb_load_u32(p->payload) == HEALTH_MAGIC) &&
            usb_load_u32(p->payload + 32u) == usb_crc32(p->payload, 32u)) {
            health.peer_seen = 1u;
            health.last_rx_ms = uptime_ms;
        }
    }
    health_snapshot_t snapshot;
    snapshot.wifi_stage = (uint32_t)wifi_station_stage();
    snapshot.wifi_error = (uint32_t)wifi_station_error();
    snapshot.dhcp_state = network.dhcp_state;
    snapshot.network_error = network.error;
    snapshot.ip = network.ip;
    snapshot.connected = wifi_station_link_up() ? 1u : 0u;
    snapshot.radio_state = receiver.state;
    snapshot.radio_error = pico2w_lora_error;
    health.uptime_ms = uptime_ms;
    health_update(&health, &snapshot);
    if (health_due(&health, uptime_ms)) {
        uint8_t payload[LINK_PACKET_SIZE];
        health_payload(&health, uptime_ms, payload);
        health.tx_error = link_radio_send(&receiver, payload, &health.restore_error);
        if (!health.tx_error) { ++health.sent; health.last_success_ms = uptime_ms; }
        if (health.tx_error || health.restore_error) health.pending = 1u;
        pico2w_lora_error = health.restore_error;
    }
    radio_busy = 0u;
}

static int path_is(const char *a, const char *b) {
    uint32_t i = 0u;
    while (a[i] && a[i] == b[i]) ++i;
    return a[i] == b[i];
}

static uint32_t remote_http(void *context, const http_request_t *request,
                            uint8_t *response, uint32_t capacity) {
    if (request->post || path_is(request->path, "/") || path_is(request->path, "/api/health"))
        return health_http(&health, request, response, capacity);
    return dashboard_http(context, request, response, capacity);
}
#endif

static void capture_update(void) {
    dashboard.config.frequency_hz = receiver.active.frequency_hz;
    dashboard.config.sf = receiver.active.spreading_factor;
    dashboard.config.bandwidth_khz = receiver.active.bandwidth_khz;
    dashboard.config.profile = receiver.active.profile;
    dashboard.uptime_ms = uptime_ms;
    dashboard.wifi_error = (int32_t)wifi_station_error();
    dashboard.lora_error = pico2w_lora_error;
    dashboard.lora_state = receiver.state;
    dashboard.lora_status = receiver.last_status;
    dashboard.lora_command = receiver.last_command;
    dashboard.lora_device_errors = receiver.device_errors;
    dashboard.lora_polls = receiver.counters.polls;
    dashboard.lora_response = receiver.last_response;
    dashboard.received = receiver.counters.packets;
    dashboard.crc_errors = receiver.counters.crc_errors;
    dashboard.header_errors = receiver.counters.header_errors;
    dashboard.noise_qdb = receiver.noise_qdb;
    dashboard.noise_min_qdb = receiver.noise_min_qdb;
    dashboard.noise_max_qdb = receiver.noise_max_qdb;
    dashboard.noise_samples = receiver.noise_samples;
    dashboard.network_drops = network.dropped;
    dashboard.retransmits = network.retransmits;
    for (uint32_t i = 0u; i < sizeof(dashboard.ip); ++i) dashboard.ip[i] = network.address[i];
    const sx1262_rx_packet_t *newest = sx1262_receiver_packet(&receiver, 0u);
    if (newest == 0 || newest->sequence == last_packet_sequence) return;
    last_packet_sequence = newest->sequence;
    dashboard.packet_count = receiver.count < DASHBOARD_PACKETS ? receiver.count : DASHBOARD_PACKETS;
    dashboard.overwritten = newest->sequence > DASHBOARD_PACKETS ?
                              newest->sequence - DASHBOARD_PACKETS : 0u;
    for (uint32_t i = 0u; i < dashboard.packet_count; ++i) {
        const sx1262_rx_packet_t *source = sx1262_receiver_packet(&receiver, i);
        dashboard_packet_t *packet = dashboard.packets + i;
        packet->sequence = source->sequence;
        packet->uptime_ms = source->timestamp_ms;
        packet->frequency_hz = source->frequency_hz;
        packet->sf = source->spreading_factor;
        packet->bandwidth_khz = source->bandwidth_khz;
        packet->profile = source->profile;
        packet->rssi_qdb = source->rssi_qdb;
        packet->snr_qdb = source->snr_qdb;
        packet->signal_rssi_qdb = source->signal_rssi_qdb;
        packet->flags = source->flags;
        packet->length = source->length;
        packet->crc_error = (source->flags & SX1262_RX_CRC_ERROR) != 0u;
        for (uint32_t j = 0u; j < source->length; ++j) packet->data[j] = source->payload[j];
    }
}

/* Runs here, outside the radio driver: USB requests are serviced inside SPI
   waits, so the request handler only queues the frequency. */
#ifndef PICO2W_REMOTE
static void run_survey(void) {
    uint32_t khz = control.survey_pending_khz;
    if (khz == 0u) return;
    if (activity.running) {
        usb_control_survey_result(&control, khz, 0u, 0u, SX1262_RX_BUSY_ERROR, 0, 0, 0);
        return;
    }
    sx1262_rx_survey_t result;
    int32_t error = sx1262_receiver_survey(&receiver, khz * 1000u, &result);
    if (error != 0 && receiver.state != 1u) pico2w_lora_error = error;
    usb_control_survey_result(&control, khz, result.bandwidth_khz, result.samples, error,
                              result.min_qdb, result.mean_qdb, result.max_qdb);
}
#endif

void bare_main(void) {
    pico2w_stage = 1u;
    int32_t error = sx1262_hw_clock_init();
    if (error != 0) recover(error);
    last_us = sx1262_hw_time_us();
    uint32_t id[2];
    error = rp2350_chip_id(id);
    if (error != 0) recover(error);
    usb_control_init(&control, PICO2W_BUILD_ID, id);
#ifdef PICO2W_REMOTE
    health_init(&health, id, uptime_ms);
#endif
    control.lora_disabled = 1u;
    control.packet_source = packet_source;
    control.survey_enabled = 1u;
    pico2w_stage = 2u;
    error = usb_hw_init();
    if (error != 0) recover(error);
    usb_ready = 1u;
    dashboard.build_id = PICO2W_BUILD_ID;
    dashboard.config.frequency_hz = SX1262_RX_DEFAULT_FREQUENCY_HZ;
    dashboard.config.sf = SX1262_RX_DEFAULT_SF;
    dashboard.config.bandwidth_khz = SX1262_RX_DEFAULT_BANDWIDTH_KHZ;
    dashboard.config.profile = SX1262_RX_DEFAULT_PROFILE;
    dashboard.configure = configure_radio;
    dashboard.activity = &activity;
    dashboard.activity_control = control_activity;
    sx1262_receiver_init(&receiver, service);
    wifi_station_set_service(wifi_service, 0);
#ifndef PICO2W_REMOTE
    wifi_started = wifi_station_init(wifi_default_ssid, wifi_default_passphrase) ? 1u : 0u;
#endif
    pico2w_lora_error = sx1262_receiver_confirm_board(
        &receiver, SX1262_RX_BOARD_WAVESHARE_PICO_LORA_SX1262_868M);
    if (pico2w_lora_error == 0) {
#ifdef PICO2W_REMOTE
        pico2w_lora_error = link_test_configure(&receiver);
#else
        pico2w_lora_error = configure_radio(0, &dashboard.config);
#endif
    }
#ifdef PICO2W_REMOTE
    radio_busy = 0u;
    wifi_started = wifi_station_init(wifi_default_ssid, wifi_default_passphrase) ? 1u : 0u;
    wifi_retry_ms = uptime_ms;
#endif
    pico2w_stage = 3u;
    while (1) {
        (void)service();
#ifndef PICO2W_REMOTE
        if (receiver.state == 1u) {
            error = sx1262_receiver_poll(&receiver, uptime_ms);
            if (error != 0) pico2w_lora_error = error;
        }
        uint8_t surveying = activity.running;
        activity_tick(&activity, &receiver, uptime_ms);
        if (surveying && !activity.running)
            pico2w_lora_error = receiver.state == SX1262_RX_RUNNING ? 0 : receiver.last_error;
        run_survey();
#else
        if (control.survey_pending_khz)
            usb_control_survey_result(&control, control.survey_pending_khz, 0u, 0u,
                                     SX1262_RX_BUSY_ERROR, 0, 0, 0);
        if ((!wifi_started || wifi_station_stage() == WIFI_STATION_FAILED || network.dhcp_state == 5u) &&
            uptime_ms - wifi_retry_ms >= 120000u) {
            wifi_retry_ms = uptime_ms;
            network_ready = 0u;
            network.dhcp_state = 0u; network.ip = 0u; network.error = 0u;
            wifi_started = wifi_station_init(wifi_default_ssid, wifi_default_passphrase) ? 1u : 0u;
        }
#endif
        if (wifi_started) wifi_station_service();
        if (!network_ready && wifi_station_link_up()) {
            uint8_t mac[6];
            if (wifi_station_get_mac(mac)) {
                const net_ops_t ops = {&dashboard, send_frame,
#ifdef PICO2W_REMOTE
                                      remote_http
#else
                                      dashboard_http
#endif
                };
                network_init(&network, &ops, mac, id[0] ^ id[1] ^ sx1262_hw_time_us(), uptime_ms);
                network_ready = 1u;
            }
        }
        capture_update();
        if (network_ready && wifi_station_link_up()) {
            network_tick(&network, uptime_ms);
            for (uint32_t n = 0u; n < 4u; ++n) {
                unsigned int length = 0u;
                int result = wifi_station_read(frame, sizeof(frame), &length);
                if (result != 1) break;
                network_input(&network, frame, length);
                (void)service();
            }
        }
    }
}

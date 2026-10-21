#include "rp2350/bootrom.h"
#include "usb/hardware.h"
#include "lora/hardware.h"
#include "lora/receiver.h"
#include "wifi/wifi_station.h"
#include "app/dashboard.h"
#include "net/network.h"
#include "wifi_credentials_generated.h"
#include "build_id.h"

static usb_control_t control;
static network_t network;
static dashboard_t dashboard;
static sx1262_receiver_t receiver;
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
    sx1262_rx_config_t radio_config;
    radio_config.frequency_hz = config->frequency_hz;
    radio_config.spreading_factor = config->sf;
    radio_config.bandwidth_khz = config->bandwidth_khz;
    pico2w_lora_error = sx1262_receiver_configure(&receiver, &radio_config);
    return pico2w_lora_error;
}

static void capture_update(void) {
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
    dashboard.network_drops = network.dropped;
    dashboard.retransmits = network.retransmits;
    for (uint32_t i = 0u; i < sizeof(dashboard.ip); ++i) dashboard.ip[i] = network.address[i];
    const sx1262_rx_packet_t *newest = sx1262_receiver_packet(&receiver, 0u);
    if (newest == 0 || newest->sequence == last_packet_sequence) return;
    last_packet_sequence = newest->sequence;
    dashboard.packet_count = receiver.count < DASHBOARD_PACKETS ? receiver.count : DASHBOARD_PACKETS;
    dashboard.overwritten = receiver.counters.packets > DASHBOARD_PACKETS ?
                              receiver.counters.packets - DASHBOARD_PACKETS : 0u;
    for (uint32_t i = 0u; i < dashboard.packet_count; ++i) {
        const sx1262_rx_packet_t *source = sx1262_receiver_packet(&receiver, i);
        dashboard_packet_t *packet = dashboard.packets + i;
        packet->sequence = source->sequence;
        packet->uptime_ms = source->timestamp_ms;
        packet->frequency_hz = source->frequency_hz;
        packet->sf = source->spreading_factor;
        packet->bandwidth_khz = source->bandwidth_khz;
        packet->rssi_qdb = source->rssi_qdb;
        packet->snr_qdb = source->snr_qdb;
        packet->length = source->length;
        for (uint32_t j = 0u; j < source->length; ++j) packet->data[j] = source->payload[j];
    }
}

void bare_main(void) {
    pico2w_stage = 1u;
    int32_t error = sx1262_hw_clock_init();
    if (error != 0) recover(error);
    last_us = sx1262_hw_time_us();
    uint32_t id[2];
    error = rp2350_chip_id(id);
    if (error != 0) recover(error);
    usb_control_init(&control, PICO2W_BUILD_ID, id);
    control.lora_disabled = 1u;
    pico2w_stage = 2u;
    error = usb_hw_init();
    if (error != 0) recover(error);
    usb_ready = 1u;
    dashboard.build_id = PICO2W_BUILD_ID;
    dashboard.config.frequency_hz = 868100000u;
    dashboard.config.sf = 7u;
    dashboard.config.bandwidth_khz = 125u;
    dashboard.configure = configure_radio;
    sx1262_receiver_init(&receiver, service);
    wifi_station_set_service(wifi_service, 0);
    wifi_started = wifi_station_init(wifi_default_ssid, wifi_default_passphrase) ? 1u : 0u;
    pico2w_lora_error = sx1262_receiver_confirm_board(
        &receiver, SX1262_RX_BOARD_WAVESHARE_PICO_LORA_SX1262_868M);
    if (pico2w_lora_error == 0) pico2w_lora_error = configure_radio(0, &dashboard.config);
    pico2w_stage = 3u;
    while (1) {
        (void)service();
        if (receiver.state == 1u) {
            error = sx1262_receiver_poll(&receiver, uptime_ms);
            if (error != 0) pico2w_lora_error = error;
        }
        if (wifi_started) wifi_station_service();
        if (!network_ready && wifi_station_link_up()) {
            uint8_t mac[6];
            if (wifi_station_get_mac(mac)) {
                const net_ops_t ops = {&dashboard, send_frame, dashboard_http};
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

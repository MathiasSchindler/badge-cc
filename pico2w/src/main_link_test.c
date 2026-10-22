#include "rp2350/bootrom.h"
#include "usb/hardware.h"
#include "lora/hardware.h"
#include "lora/link_test.h"
#include "lora/packet_record.h"
#include "build_id.h"

static usb_control_t control;
static sx1262_receiver_t receiver;
static link_test_t link;
static uint32_t uptime_ms, last_us, remainder_us;
volatile int32_t pico2w_error;
volatile uint32_t pico2w_stage;

static void recover(int32_t error) __attribute__((noreturn));
static void recover(int32_t error) {
    pico2w_error = error;
    sx1262_hw_release();
    usb_hw_disconnect();
    int32_t reboot = rp2350_schedule_bootsel(10u);
    if (reboot) pico2w_error = reboot;
    while (1) __asm__ volatile ("wfi");
}

static int32_t service(void) {
    uint32_t now = sx1262_hw_time_us(), elapsed = now - last_us;
    last_us = now;
    uptime_ms += elapsed / 1000u;
    remainder_us += elapsed % 1000u;
    if (remainder_us >= 1000u) { ++uptime_ms; remainder_us -= 1000u; }
    int32_t error = usb_hw_poll(&control);
    if (error || control.reboot_requested) recover(error);
    return 0;
}

static void packet_source(void *context, uint32_t after, uint8_t *record) {
    (void)context;
    lora_packet_record(record, PICO2W_BUILD_ID, &receiver, after);
}

static void status(void) {
    uint8_t *r = control.app_status;
    for (uint32_t i = 0u; i < USB_REPORT_SIZE; ++i) r[i] = 0u;
    usb_store_u32(r, LINK_STATUS_MAGIC);
    r[4] = 1u; r[6] = USB_REPORT_SIZE;
    usb_store_u32(r + 8u, PICO2W_BUILD_ID);
    usb_store_u32(r + 12u, uptime_ms);
    usb_store_u32(r + 16u, receiver.state);
    usb_store_u32(r + 20u, (uint32_t)receiver.last_error);
    usb_store_u32(r + 24u, link.attempts);
    usb_store_u32(r + 28u, link.transmitted);
    usb_store_u32(r + 32u, (uint32_t)link.error);
    usb_store_u32(r + 36u, (uint32_t)link.restore_error);
    usb_store_u32(r + 40u, receiver.counters.packets);
    usb_store_u32(r + 44u, receiver.counters.crc_errors);
    usb_store_u32(r + 48u, LINK_FREQUENCY_HZ);
    usb_store_u32(r + 52u, LINK_INTERVAL_MS);
    usb_store_u32(r + 56u, LINK_MAX_ATTEMPTS);
    usb_store_u32(r + 60u, usb_crc32(r, 60u));
    control.app_status_valid = 1u;
    r = control.wifi_status;
    for (uint32_t i = 0u; i < USB_REPORT_SIZE; ++i) r[i] = 0u;
    usb_store_u32(r, LINK_STATUS_MAGIC);
    usb_store_u32(r + 8u, PICO2W_BUILD_ID);
    usb_store_u32(r + 12u, receiver.last_command);
    usb_store_u32(r + 16u, receiver.last_response);
    usb_store_u32(r + 20u, receiver.last_status);
    usb_store_u32(r + 24u, receiver.device_errors);
    usb_store_u32(r + 28u, (uint32_t)pico2w_error);
    usb_store_u32(r + 60u, usb_crc32(r, 60u));
    control.wifi_status_valid = 1u;
}

void bare_main(void) {
    pico2w_stage = 1u;
    int32_t error = sx1262_hw_clock_init();
    if (error) recover(error);
    last_us = sx1262_hw_time_us();
    uint32_t id[2];
    error = rp2350_chip_id(id);
    if (error) recover(error);
    usb_control_init(&control, PICO2W_BUILD_ID, id);
    control.lora_disabled = 1u;
    control.link_enabled = 1u;
    control.packet_source = packet_source;
    error = usb_hw_init();
    if (error) recover(error);
    sx1262_receiver_init(&receiver, service);
    error = sx1262_receiver_confirm_board(&receiver, SX1262_RX_BOARD_WAVESHARE_PICO_LORA_SX1262_868M);
    if (!error) error = link_test_configure(&receiver);
    pico2w_error = error;
    pico2w_stage = 3u;
    while (1) {
        status();
        (void)service();
        if (receiver.state == SX1262_RX_RUNNING) {
            uint32_t rearms = receiver.counters.rearms;
            error = sx1262_receiver_poll(&receiver, uptime_ms);
            if (!error && receiver.counters.rearms != rearms) error = link_test_configure(&receiver);
            if (error) pico2w_error = error;
        }
        if (control.lora_requested) {
            sx1262_result_t result;
            control.lora_requested = 0u;
            control.lora_running = 1u;
            for (uint32_t i = 0u; i < sizeof(result.samples); ++i) result.samples[i] = 0u;
            result.error = link_test_send(&link, &receiver, uptime_ms, id, control.lora_report + 16u);
            usb_control_lora_result(&control, &result);
        }
    }
}

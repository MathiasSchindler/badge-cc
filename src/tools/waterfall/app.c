#include "activity.h"
#include "../../../clean-rx/badge/loader.h"
#include "../../usb/live_usb_hw.h"
#include "../../arch/rp2350/bootrom_reboot.h"
#include "../../arch/rp2350/nametag_clock.h"
#include "../../platform/badger/wifi_board.h"
#include "../../platform/badger/display.h"
#include "../../platform/badger/home.h"
#include "../../radio/wifi_usb_report.h"
#include "../diagnostics_logic.h"
#include "activity_build.h"

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

extern const uint8_t clean_image[], clean_image_end[];
static activity_t activity;
static badger_canvas_t canvas;
static clean_loader_t loader;
static live_usb_control_t control;
static uint8_t packet[CLEAN_RX_PACKET_BYTES];
static uint32_t channel, home_at, refresh_at;
static uint32_t tuning_sequence;
static activity_button_t speed_button;
static uint8_t home_held, quitting, usb_ready;
static int32_t usb_error;

static uint32_t now(void) { return REG(0x400b000cu); }

static void publish(void) {
    uint8_t nonce[16], report[64];
    uint32_t generation = live_usb_probe_take(&control, nonce);
    if (!generation) return;
    if (nonce[14] == 0x57u && (nonce[15] == 1u || nonce[15] == 2u))
        speed_button.fast = nonce[15] == 1u;
    usb_report_header(report, 0x57363255u, ACTIVITY_BUILD_ID, nonce, WIFI_USB_TEST_ID);
    const uint32_t words[] = {activity.sweeps, activity.frames, activity.rejected,
        activity.rendered, loader.error,
        channel | ((uint32_t)(activity.dwell_us == ACT_FAST_DWELL_US) << 8u), ACT_MAGIC};
    for (uint32_t i = 0u; i < 7u; ++i)
        badger_bytes_store_u32(report + 32u + 4u * i, words[i]);
    badger_bytes_store_u32(report + 60u, diag_crc32(report, 60u));
    live_usb_probe_finish(&control, generation, report);
    uint8_t trace[WIFI_TRACE_SIZE];
    badger_bytes_zero(trace, sizeof(trace));
    badger_bytes_store_u32(trace, activity.channel_updates);
    badger_bytes_store_u32(trace + 4u, activity.sweep_us);
    badger_bytes_store_u32(trace + 8u, activity.dwell_us);
    live_usb_trace_finish(&control, generation, trace);
}

static int service(void *unused) {
    (void)unused;
    if (usb_ready) {
        usb_error = live_usb_hw_poll(&control);
        publish();
    }
    if (!badger_home_pressed()) home_held = 0u;
    else if (!home_held) { home_held = 1u; home_at = now(); }
    else if (now() - home_at >= 1500000u) quitting = 1u;
    activity_button(&speed_button, (REG(0xd0000004u) & (1u << 10u)) == 0u, now());
    if (now() - refresh_at >= 1000000u) {
        REG(0x400d8004u) = 8000000u;
        refresh_at = now();
    }
    return !quitting && !usb_error && !control.reboot_requested;
}

static void draw(const char *status) {
    activity_render(&canvas, &activity, status);
    badger_display_begin();
    for (uint32_t x = 0u; x < 320u; ++x) {
        badger_display_write_pixels(canvas.pixels + x * 240u, 240u);
        if (usb_ready) (void)service(0);
    }
    badger_display_end();
    ++activity.rendered;
}

static void progress(uint32_t completed) {
    activity_progress(&canvas, &activity, completed);
    badger_display_begin_region(ACT_X, ACT_Y - 3u, ACT_CHANNELS * ACT_CELL, 4u);
    for (uint32_t x = ACT_X; x < ACT_X + ACT_CHANNELS * ACT_CELL; ++x) {
        badger_display_write_pixels(canvas.pixels + x * 240u + ACT_Y - 3u, 4u);
        (void)service(0);
    }
    badger_display_end();
}

static void bootsel(void) {
    wifi_wire_power_off(wifi_board_wire());
    uint32_t start = now();
    while (usb_ready && now() - start < 10000u) (void)live_usb_hw_poll(&control);
    live_usb_hw_disconnect();
    (void)badger_schedule_bootsel(10u);
    while (1) __asm__ volatile("wfi");
}

static int receive_window(uint32_t duration, uint8_t count) {
    uint32_t start = now();
    while (now() - start < duration) {
        uint32_t length;
        if (!clean_receive(&loader, packet, &length)) return 0;
        if (length && count) (void)activity_frame(&activity, packet, length, channel);
    }
    return 1;
}

static int tune(uint32_t target) {
    uint32_t cmd = 4u | (target << 8u);
    uint8_t status[16];
    if (!clean_write(&loader, CLEAN_MBOX + 24u, cmd) ||
        !clean_wait_command(&loader, cmd, 200000u) ||
        !clean_block(&loader, 0x7f000u, status, sizeof(status), 0u)) return 0;
    if (badger_bytes_load_u32(status) != ACT_MAGIC ||
        badger_bytes_load_u32(status + 4u) != target ||
        badger_bytes_load_u32(status + 8u) != 1u ||
        badger_bytes_load_u32(status + 12u) != tuning_sequence + 1u) {
        loader.error = CLEAN_READBACK;
        loader.actual = badger_bytes_load_u32(status + 8u);
        return 0;
    }
    ++tuning_sequence;
    channel = target;
    /* Discard queued old-channel frames and let the receiver settle. */
    return receive_window(15000u, 0u);
}

void bare_main(void) {
    if (badger_schedule_bootsel(8000u) || nametag_clock_reference())
        while (1) __asm__ volatile("wfi");
    refresh_at = now();
    badger_home_init();
    REG(0xd0000040u) = 1u << 10u;
    REG(0x40028004u + 10u * 8u) = 5u;
    REG(0x40038004u + 10u * 4u) = 0xcau;
    activity.dwell_us = ACT_DWELL_US;
    uint32_t id[2];
    if (badger_rom_chip_id(id)) while (1) __asm__ volatile("wfi");
    char serial[17], half[9];
    diag_hex(half, id[1], 8u); badger_bytes_copy((uint8_t *)serial, (uint8_t *)half, 8u);
    diag_hex(half, id[0], 8u); badger_bytes_copy((uint8_t *)serial + 8u, (uint8_t *)half, 8u);
    serial[16] = 0;
    live_usb_control_init(&control, ACTIVITY_BUILD_ID, serial);
    if (live_usb_hw_init()) while (1) __asm__ volatile("wfi");
    usb_ready = 1u;
    if (!badger_display_init_serviced(service, 0)) bootsel();
    draw("STARTING RX");
    wifi_board_init();
    wifi_board_select_pio(1u);
    clean_loader_init(&loader, wifi_board_wire(), service, 0);
    if (!clean_boot(&loader, clean_image, (uint32_t)(clean_image_end - clean_image),
                    1u, 0x57464131u, 0u) ||
        !clean_write(&loader, CLEAN_MBOX + 24u, 2u) ||
        !clean_wait_state(&loader, 9u, 200000u)) goto failed;
    for (;;) {
        loader.budget_us = loader.elapsed_us + 6000000u;
        uint32_t sweep_started = now();
        uint32_t dwell = speed_button.fast ? ACT_FAST_DWELL_US : ACT_DWELL_US;
        if (dwell != activity.dwell_us) {
            activity.dwell_us = dwell;
            draw("RX FRAMES");
        }
        progress(0u);
        for (uint32_t ch = 1u; ch <= ACT_CHANNELS; ++ch) {
            if (!tune(ch)) goto failed;
            uint32_t started = now();
            if (!receive_window(activity.dwell_us, 1u)) goto failed;
            activity.rates[ch - 1u] = activity_rate(activity.counts[ch - 1u], now() - started);
            progress(ch);
            ++activity.channel_updates;
        }
        activity_commit(&activity);
        draw("RX FRAMES");
        activity.sweep_us = now() - sweep_started;
    }
failed:
    wifi_wire_power_off(wifi_board_wire());
    if (quitting || control.reboot_requested || usb_error) bootsel();
    draw("RADIO ERROR");
    char error[12];
    diag_decimal(error, (int32_t)loader.error);
    badger_gfx_text(&canvas, 12, 70, error, 3u, 0xf800u);
    badger_display_begin_region(12u, 70u, 60u, 24u);
    for (uint32_t x = 12u; x < 72u; ++x)
        badger_display_write_pixels(canvas.pixels + x * 240u + 70u, 24u);
    badger_display_end();
    while (service(0)) __asm__ volatile("nop");
    bootsel();
}

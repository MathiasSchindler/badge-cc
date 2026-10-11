#include "display.h"
#include "graphics.h"
#include "diagnostics_logic.h"
#include "../usb/live_usb_hw.h"
#include "../arch/rp2350/bootrom_reboot.h"
#include "../radio/wifi_usb_report.h"
#include "wifi_board.h"
#ifdef LIVE_USB_FIRMWARE
#include "../radio/wifi_firmware.h"
#include "../radio/wifi_assets.h"
#include "usb_wifi_build.h"
#else
#include "usb_live_build.h"
#endif

static badger_canvas_t canvas;
static live_usb_control_t control;
extern volatile uint8_t __report_response[];

static void screen(const char *status, int32_t error) {
    char number[12];
    badger_gfx_clear(&canvas, 0x0863u);
    badger_gfx_text(&canvas, 8, 12, "LIVE USB REPORTS", 2u, 0xffffu);
    badger_gfx_text(&canvas, 8, 50, status, 1u, error == 0 ? 0x5ecbu : 0xf9c7u);
#ifdef LIVE_USB_FIRMWARE
    badger_gfx_text(&canvas, 8, 76, "NO SDK / NO LIBC / VENDOR RADIO DATA", 1u, 0x9cf3u);
#else
    badger_gfx_text(&canvas, 8, 76, "NO SDK / NO LIBC / RADIO PROBE ON REQUEST", 1u, 0x9cf3u);
#endif
    badger_gfx_text(&canvas, 8, 108, "SERIAL", 1u, 0x9cf3u);
    badger_gfx_text(&canvas, 56, 108, control.serial, 1u, 0xffffu);
    badger_gfx_text(&canvas, 8, 144, "CONTROL ENDPOINT REPORT INTERFACE", 1u, 0x9cf3u);
    badger_gfx_text(&canvas, 8, 170, "AUTOMATIC RETURN TO BOOTSEL", 1u, 0xfda8u);
    diag_decimal(number, error);
    badger_gfx_text(&canvas, 8, 194, number, 1u, 0xffffu);
    badger_display_begin();
    for (uint32_t i = 0u; i < BADGER_GFX_WIDTH * BADGER_GFX_HEIGHT; ++i) badger_display_write(canvas.pixels[i]);
    badger_display_end();
}

static void service_usb_during_probe(void *context, const wifi_probe_report_t *report) {
    (void)report;
    int32_t *error = context;
    if (*error == 0) *error = live_usb_hw_poll(&control);
}

static int32_t measure_usb_frames(void) {
    volatile uint32_t *sof = (volatile uint32_t *)0x50110048u;
    volatile uint32_t *timer = (volatile uint32_t *)0x400b000cu;
    uint32_t previous = *sof & 0x7ffu;
    uint32_t started = *timer, first = 0u, frames = 0u;
    uint8_t aligned = 0u;
    for (uint32_t i = 0u; i < 2000000u; ++i) {
        int32_t error = live_usb_hw_poll(&control);
        if (error != 0) return error;
        uint32_t now = *timer, frame = *sof & 0x7ffu;
        if (frame != previous) {
            if (!aligned) { first = now; aligned = 1u; }
            else frames += (frame - previous) & 0x7ffu;
            previous = frame;
            if (frames >= 32u) {
                wifi_board_trace_clock_sample(now - first, frames);
                return 0;
            }
        }
        if (now - started >= 100000u) break;
    }
    wifi_board_trace_clock_sample(0u, 0u);
    return 0;
}

#ifdef LIVE_USB_FIRMWARE
typedef struct {
    uint32_t generation;
    int32_t usb_error;
} fw_service_context_t;

static int service_firmware(void *context, const wifi_fw_report_t *report) {
    (void)report;
    fw_service_context_t *service = context;
    if (service->usb_error == 0) service->usb_error = live_usb_hw_poll(&control);
    return service->usb_error == 0 && !control.reboot_requested && control.configured &&
           service->generation == control.probe_generation;
}
#endif

void bare_main(void) {
    /* Arm recovery before touching USB clocks; do not feed or disable it. */
    int32_t recovery = badger_schedule_bootsel(8000u);
    badger_display_init();
    uint32_t id[2];
    char serial[17];
    int32_t error = badger_rom_chip_id(id);
    if (error == 0) {
        char half[9];
        diag_hex(half, id[1], 8u);
        for (uint32_t i = 0u; i < 8u; ++i) serial[i] = half[i];
        diag_hex(half, id[0], 8u);
        for (uint32_t i = 0u; i < 8u; ++i) serial[8u + i] = half[i];
        serial[16] = '\0';
        live_usb_control_init(&control, USB_REPORT_BUILD_ID, serial);
    }
    if (recovery != 0 || error != 0) {
        screen("ROM SETUP FAILED / USE BOOT BUTTON", recovery != 0 ? recovery : error);
        while (1) __asm__ volatile ("wfi");
    }
    screen("USB WINDOW STARTING", 0);
    error = live_usb_hw_init();
    if (error != 0) {
        screen("USB INITIALIZATION FAILED", error);
        while (1) __asm__ volatile ("wfi");
    }
    uint32_t mirrored = 0u;
    uint32_t reboot_at = 0u;
    uint8_t reboot_wait = 0u;
    while (1) {
        error = live_usb_hw_poll(&control);
        uint8_t nonce[16];
        uint32_t generation = live_usb_probe_take(&control, nonce);
        if (generation != 0u && error == 0) {
            wifi_probe_report_t report;
            uint8_t wire_report[64];
            uint8_t trace[WIFI_TRACE_SIZE];
            wifi_board_init();
            wifi_board_select_pio(control.probe_pio);
            error = measure_usb_frames();
#ifdef LIVE_USB_FIRMWARE
            if (control.incoming_test == WIFI_FW_TEST_ID && error == 0) {
                static wifi_fw_report_t firmware;
                fw_service_context_t service = {.generation = generation, .usb_error = 0};
                wifi_board_select_pio(1u);
                wifi_firmware_run(wifi_board_wire(), wifi_firmware_assets(), &firmware,
                                  service_firmware, &service);
                error = service.usb_error;
                if (error == 0) {
                    wifi_firmware_report(wire_report, USB_REPORT_BUILD_ID, nonce, &firmware);
                    wifi_firmware_detail(trace, wifi_firmware_assets(), &firmware);
                    live_usb_probe_finish(&control, generation, wire_report);
                    live_usb_trace_finish(&control, generation, trace);
                }
            } else
#endif
            {
            if (error == 0) wifi_probe_run(wifi_board_wire(), &report, service_usb_during_probe, &error);
            wifi_wire_power_off(wifi_board_wire());
            if (error == 0) {
                wifi_usb_report_encode(wire_report, USB_REPORT_BUILD_ID, nonce, &report);
                wifi_board_trace(trace);
                live_usb_probe_finish(&control, generation, wire_report);
                live_usb_trace_finish(&control, generation, trace);
            }
            }
        }
        if (control.requests != mirrored) {
            for (uint32_t i = 0u; i < USB_REPORT_WIRE_SIZE; ++i) __report_response[i] = control.report[i];
            mirrored = control.requests;
        }
        if (control.reboot_requested && !reboot_wait) {
            reboot_wait = 1u;
            reboot_at = *(volatile uint32_t *)0x400b000cu + 10000u;
        }
        if (error != 0 || (reboot_wait &&
            (int32_t)(*(volatile uint32_t *)0x400b000cu - reboot_at) >= 0)) {
            live_usb_hw_disconnect();
            if (error != 0) screen("USB TRANSFER ERROR", error);
            int32_t reboot_error = badger_schedule_bootsel(10u);
            if (reboot_error != 0) screen("REBOOT REQUEST FAILED", reboot_error);
            while (1) __asm__ volatile ("wfi");
        }
    }
}

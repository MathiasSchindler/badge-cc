#include "probe.h"
#include "../../../clean-rx/badge/loader.h"
#include "../../usb/live_usb_hw.h"
#include "../../arch/rp2350/bootrom_reboot.h"
#include "../../arch/rp2350/nametag_clock.h"
#include "../../platform/badger/wifi_board.h"
#include "../../platform/badger/display.h"
#include "../../platform/badger/graphics.h"
#include "../../radio/wifi_usb_report.h"
#include "../diagnostics_logic.h"
#include "build_id.h"

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

extern const uint8_t clean_image[], clean_image_end[];
static live_usb_control_t control;
#if WATERFALL_PICO
int32_t pr_clock_sys(void);
#else
static badger_canvas_t canvas;
#endif
static clean_loader_t loader;
static uint8_t trace[WIFI_TRACE_SIZE];
static int32_t usb_error;

static int service(void *context) {
    (void)context;
    usb_error = live_usb_hw_poll(&control);
    return !usb_error && !control.reboot_requested;
}

static void screen(const char *message, uint32_t error) {
#if WATERFALL_PICO
    (void)message; (void)error;
#else
    char number[12];
    badger_gfx_clear(&canvas, 0x0863u);
    badger_gfx_text(&canvas, 8, 12, "WATERFALL RX PROBE", 2u, 0xffffu);
    badger_gfx_text(&canvas, 8, 55, message, 1u, 0xffffu);
    badger_gfx_text(&canvas, 8, 100, "PLL LOCK IS NOT RF RECEPTION", 1u, 0xfda8u);
    badger_gfx_text(&canvas, 8, 130, "NO TX / NO PSM / NO LIBC", 1u, 0x9cf3u);
    diag_decimal(number, (int32_t)error);
    badger_gfx_text(&canvas, 8, 165, number, 2u, error ? 0xf9c7u : 0x5ecbu);
    badger_display_begin();
    for (uint32_t i = 0u; i < 320u * 240u; ++i)
        badger_display_write(canvas.pixels[i]);
    badger_display_end();
#endif
}

static void run(uint32_t generation, const uint8_t nonce[16]) {
    badger_bytes_zero(trace, sizeof(trace));
    wifi_board_init();
    wifi_board_select_pio(1u);
    clean_loader_init(&loader, wifi_board_wire(), service, 0);
    uint32_t token = clean_request_generation(nonce);
    if (badger_schedule_bootsel(8000u)) loader.error = CLEAN_CANCELLED;
    int ok = loader.error == CLEAN_OK &&
        clean_boot(&loader, clean_image, (uint32_t)(clean_image_end - clean_image),
                    1u, token, 0u);
    if (ok) ok = clean_write(&loader, CLEAN_MBOX + 24u, 2u) &&
        clean_wait_state(&loader, 9u, 200000u) &&
        clean_write(&loader, CLEAN_MBOX + 24u, WF_COMMAND) &&
        clean_wait_state(&loader, 10u, 2000000u) &&
        clean_block(&loader, WF_ADDRESS, trace, WF_REPORT_BYTES, 0u);
    if (!ok && loader.error != CLEAN_CANCELLED && loader.stage >= 4u) {
        clean_loader_t snapshot;
        if (clean_loader_begin_snapshot(&loader, &snapshot)) {
            uint32_t words = 0u;
            for (uint32_t i = 0u; i < 3u; ++i) {
                uint32_t value;
                if (!clean_read(&snapshot, CLEAN_MBOX + 12u + i * 4u, &value)) break;
                badger_bytes_store_u32(trace + WF_REPORT_BYTES + 12u + i * 4u, value);
                ++words;
            }
            badger_bytes_store_u32(trace + WF_REPORT_BYTES + 36u, words == 3u);
        }
    }
    wifi_wire_power_off(loader.wire);
    badger_bytes_store_u32(trace + WF_REPORT_BYTES, loader.error);
    badger_bytes_store_u32(trace + WF_REPORT_BYTES + 4u, loader.address);
    badger_bytes_store_u32(trace + WF_REPORT_BYTES + 8u, loader.actual);
    badger_bytes_store_u32(trace + WF_REPORT_BYTES + 24u, loader.stage);
    badger_bytes_store_u32(trace + WF_REPORT_BYTES + 28u, loader.verified);
    badger_bytes_store_u32(trace + WF_REPORT_BYTES + 32u, (uint32_t)loader.elapsed_us);
    uint32_t error = loader.error ? loader.error : badger_bytes_load_u32(trace + 20u);
    uint8_t report[64];
    usb_report_header(report, 0x57363255u, USB_REPORT_BUILD_ID, nonce, WIFI_USB_TEST_ID);
    badger_bytes_store_u32(report + 32u, error);
    badger_bytes_store_u32(report + 36u, loader.error);
    badger_bytes_store_u32(report + 40u, loader.address);
    badger_bytes_store_u32(report + 44u, loader.actual);
    badger_bytes_store_u32(report + 48u, WF_MAGIC);
    badger_bytes_store_u32(report + 52u, WF_VERSION);
    badger_bytes_store_u32(report + 56u, WF_REPORT_BYTES);
    badger_bytes_store_u32(report + 60u, diag_crc32(report, 60u));
    live_usb_probe_finish(&control, generation, report);
    live_usb_trace_finish(&control, generation, trace);
    screen(error ? "PROBE FAILED - KEEP EVIDENCE" : "TUNING PROBE COMPLETE", error);
}

void bare_main(void) {
    int32_t startup = badger_schedule_bootsel(8000u);
    if (!startup) startup = nametag_clock_reference();
#if WATERFALL_PICO
    if (!startup) startup = pr_clock_sys();
#endif
    uint32_t id[2];
    if (!startup) startup = badger_rom_chip_id(id);
    if (startup) while (1) __asm__ volatile("wfi");
    char serial[17], half[9];
    diag_hex(half, id[1], 8u);
    badger_bytes_copy((uint8_t *)serial, (uint8_t *)half, 8u);
    diag_hex(half, id[0], 8u);
    badger_bytes_copy((uint8_t *)serial + 8u, (uint8_t *)half, 8u);
    serial[16] = 0;
#if !WATERFALL_PICO
    badger_display_init();
#endif
    screen("WAITING FOR BOUNDED USB PROBE", 0u);
    live_usb_control_init(&control, USB_REPORT_BUILD_ID, serial);
    if (live_usb_hw_init()) while (1) __asm__ volatile("wfi");
    for (;;) {
        (void)service(0);
        if (usb_error || control.reboot_requested) {
            wifi_wire_power_off(wifi_board_wire());
            uint32_t start = REG(0x400b000cu);
            while (REG(0x400b000cu) - start < 10000u)
                (void)live_usb_hw_poll(&control);
            live_usb_hw_disconnect();
            (void)badger_schedule_bootsel(10u);
            while (1) __asm__ volatile("wfi");
        }
        uint8_t nonce[16];
        uint32_t generation = live_usb_probe_take(&control, nonce);
        if (generation) run(generation, nonce);
    }
}

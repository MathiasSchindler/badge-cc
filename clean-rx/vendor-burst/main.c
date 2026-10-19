#include "app.h"
#include "build_id.h"
#include "../../src/arch/rp2350/bootrom_reboot.h"

live_usb_control_t ref_usb;
uint8_t ref_detail[REF_TRACE_BYTES], ref_ssid[33], ref_password[64];
uint32_t ref_generation;
static uint32_t started, configured_at, watchdog_at;
static int32_t usb_error;
static uint8_t credential_part;

uint32_t ref_now(void) { return *(volatile uint32_t *)0x400b000cu; }

static int clock_sample(void) {
    volatile uint32_t *sof = (volatile uint32_t *)0x50110048u;
    uint32_t previous = *sof & 0x7ffu, begin = ref_now(), first = 0u, frames = 0u;
    uint8_t aligned = 0u;
    while (ref_now() - begin < 100000u && ref_service(0)) {
        uint32_t frame = *sof & 0x7ffu, now = ref_now();
        if (frame == previous) continue;
        if (!aligned) { first = now; aligned = 1u; }
        else frames += (frame - previous) & 0x7ffu;
        previous = frame;
        if (frames >= 32u) {
            wifi_board_trace_clock_sample(now - first, frames);
            return 1;
        }
    }
    return 0;
}

int ref_service(void *context) {
    (void)context;
    uint32_t now = ref_now();
    if (!configured_at && ref_usb.configured) configured_at = now;
    if (configured_at && now - configured_at > 45000000u) usb_error = -102;
    if (!usb_error && ref_usb.configured && now - watchdog_at > 3000000u) {
        usb_error = badger_schedule_bootsel(8000u);
        watchdog_at = now;
    }
    if (!usb_error) usb_error = live_usb_hw_poll(&ref_usb);
    return !usb_error && !ref_usb.reboot_requested && ref_usb.configured &&
        (!ref_generation || ref_generation == ref_usb.probe_generation);
}

void bare_main(void) {
    uint32_t id[2];
    char serial[17], half[9];
    if (badger_schedule_bootsel(8000u) || badger_rom_chip_id(id))
        while (1) __asm__ volatile("wfi");
    diag_hex(half, id[1], 8u);
    badger_bytes_copy((uint8_t *)serial, (const uint8_t *)half, 8u);
    diag_hex(half, id[0], 8u);
    badger_bytes_copy((uint8_t *)serial + 8u, (const uint8_t *)half, 8u);
    serial[16] = '\0';
    live_usb_control_init(&ref_usb, USB_REPORT_BUILD_ID, serial);
    usb_error = live_usb_hw_init();
    while (1) {
        ref_generation = 0u;
        (void)ref_service(0);
        if (ref_usb.clean_secret_ready) {
            if (credential_part == 0u && ref_usb.clean_secret[0] == 1u &&
                ref_usb.clean_secret[1] && ref_usb.clean_secret[1] <= 32u) {
                badger_bytes_copy(ref_ssid, ref_usb.clean_secret + 2u, ref_usb.clean_secret[1]);
                credential_part = 1u;
            } else if (credential_part == 1u) {
                badger_bytes_copy(ref_password, ref_usb.clean_secret, 64u);
                credential_part = 2u;
            }
            ref_wipe(ref_usb.clean_secret, 64u);
            ref_usb.clean_secret_ready = 0u;
        }
        uint8_t nonce[16];
        ref_generation = live_usb_probe_take(&ref_usb, nonce);
        if (ref_generation && !usb_error) {
            if (!started) {
                started = ref_now();
                wifi_board_init();
                wifi_board_select_pio(1u);
                if (!clock_sample()) usb_error = -100;
            }
            if (!usb_error) ref_run(nonce);
            uint8_t report[64];
            usb_report_header(report, 0x46363255u, USB_REPORT_BUILD_ID, nonce, 3u);
            badger_bytes_store_u32(report + 32u, badger_bytes_load_u32(ref_detail + 4u));
            badger_bytes_store_u32(report + 36u, badger_bytes_load_u32(ref_detail + 8u));
            badger_bytes_store_u32(report + 60u, diag_crc32(report, 60u));
            live_usb_probe_finish(&ref_usb, ref_generation, report);
            live_usb_trace_finish(&ref_usb, ref_generation, ref_detail);
        } else if (!usb_error && !ref_usb.reboot_requested) ref_idle();
        if (usb_error || ref_usb.reboot_requested) {
            ref_stop();
            ref_wipe(ref_ssid, sizeof(ref_ssid));
            ref_wipe(ref_password, sizeof(ref_password));
            uint32_t end = ref_now();
            while (ref_now() - end < 10000u) (void)live_usb_hw_poll(&ref_usb);
            live_usb_hw_disconnect();
            (void)badger_schedule_bootsel(10u);
            while (1) __asm__ volatile("wfi");
        }
    }
}

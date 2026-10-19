#include "app.h"
#include "build_id.h"
#include "../../src/arch/rp2350/bootrom_reboot.h"
#include "../../src/platform/badger/display.h"
#include "../../src/platform/badger/graphics.h"

live_usb_control_t ref_usb;
uint8_t ref_detail[REF_TRACE_BYTES], ref_ssid[33], ref_password[64];
uint32_t ref_generation;
uint32_t ref_clock_ticks, ref_clock_frames;
static uint32_t started, configured_at, watchdog_at;
static int32_t usb_error;
static uint8_t credential_part;

static void screen_line(uint32_t y, const char *text, uint16_t color) {
    uint32_t length = 0u;
    while (length < 25u && text[length]) ++length;
    badger_display_begin_region(8u,y,300u,16u);
    for (uint32_t x = 0u; x < 300u; ++x) {
        uint32_t index = x/12u, column = (x%12u)/2u;
        for (uint32_t row = 0u; row < 16u; ++row) {
            uint8_t bits = index < length ? badger_gfx_glyph_row(text[index],row/2u) : 0u;
            badger_display_write(column < 5u && (bits&(1u<<(4u-column))) ? color : 0u);
        }
    }
    badger_display_end();
}

static void screen_result(void) {
    char value[9];
    uint32_t error = badger_bytes_load_u32(ref_detail+8u);
    screen_line(92u,error ? "RADIO ERROR" : "RADIO STOPPED",error ? 0xf800u : 0x07e0u);
    diag_hex(value,error,8u);
    screen_line(120u,value,0xffffu);
#if CLEAN_RX_HT_WINDOW
    if (badger_bytes_load_u32(ref_detail+4u) == 4u) {
        const uint8_t *window = ref_detail+REF_WINDOW_BASE;
        if (ref_window_valid(window)) {
            uint32_t count = badger_bytes_load_u32(window+4u);
            const uint8_t *last = window+REF_WINDOW_HEADER_BYTES+
                (count-1u)*REF_WINDOW_RECORD_BYTES;
            screen_line(148u,"RX COUNT / FIRST FAULT",0xffffu);
            diag_hex(value,badger_bytes_load_u32(last+8u),8u);
            screen_line(176u,value,0xffffu);
            diag_hex(value,ref_u16(window+64u+2u),8u);
            screen_line(204u,value,0xffe0u);
        }
    }
#endif
}

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
            ref_clock_ticks = now-first;
            ref_clock_frames = frames;
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
                if (!badger_display_init_serviced(ref_service,0)) usb_error = -103;
                if (!usb_error) {
                    badger_display_begin();
                    for (uint32_t x = 0u; x < BADGER_DISPLAY_WIDTH && !usb_error; ++x) {
                        for (uint32_t y = 0u; y < BADGER_DISPLAY_HEIGHT; ++y)
                            badger_display_write(0u);
                        if (!ref_service(0)) usb_error = -103;
                    }
                    badger_display_end();
                    screen_line(12u,ref_role,0xffffu);
                    screen_line(40u,ref_role[0] == 'C' ?
                        "PASSIVE RX - NO TX" : "JOIN THEN SEND 128",0x07ffu);
                    screen_line(68u,CLEAN_RX_HT_REARM_INJECT ? "INJECTED RECOVERY TEST" :
                        CLEAN_RX_HT_REARM_RECOVER ? "ONE SHOT RECOVERY" :
                        CLEAN_RX_HT_REARM_WAIT ? "STRICT NO RECOVERY" : "REFERENCE",0xffe0u);
                }
                wifi_board_init();
                wifi_board_select_pio(1u);
                if (!clock_sample()) usb_error = -100;
            }
            if (!usb_error && nonce[14] == 2u &&
                badger_bytes_load_u32(ref_detail+12u) == 1u)
                screen_line(40u,"SENDING 128 FRAMES",0x07ffu);
            if (!usb_error) ref_run(nonce);
            if (!usb_error && (nonce[14] == 2u || nonce[14] == 4u)) screen_result();
            uint8_t report[64];
            usb_report_header(report, 0x46363255u, USB_REPORT_BUILD_ID, nonce, 3u);
            badger_bytes_store_u32(report + 32u, badger_bytes_load_u32(ref_detail + 4u));
            badger_bytes_store_u32(report + 36u, badger_bytes_load_u32(ref_detail + 8u));
            badger_bytes_store_u32(report+40u,ref_clock_ticks);
            badger_bytes_store_u32(report+44u,ref_clock_frames);
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

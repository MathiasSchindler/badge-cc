#include "protocol.h"
#include "build_id.h"
#include "../../clean-rx/badge/loader.h"
#include "../../src/arch/rp2350/bootrom_reboot.h"
#include "../../src/arch/rp2350/nametag_clock.h"
#include "../../src/usb/live_usb_hw.h"
#include "../../src/platform/badger/wifi_board.h"
#if PR_NODE != 1
#include "../../src/platform/badger/display.h"
#include "../../src/platform/badger/graphics.h"
static badger_canvas_t canvas;
static uint32_t screen_at, screen_column = 320u;
#endif

_Static_assert(PR_NODE >= 1 && PR_NODE <= 3, "Select Pico=1, badge A=2 or badge B=3");
extern const uint8_t clean_image[], clean_image_end[];
static live_usb_control_t usb;
static clean_loader_t radio;
static pr_link_t link;
static uint8_t packet[CLEAN_RX_PACKET_BYTES], last_request[PR_PACKET], last_reply[PR_PACKET];
static uint32_t radio_ready, boots, fault, fault_address, fault_actual, retry_at;
static uint32_t watchdog_at, began_at, tx_requests, tx_replies, raw_rx, rejected;
static uint32_t sys_before, sys_after;
#if PR_NODE == 1
int32_t pr_clock_sys(void);
#endif
volatile int32_t pico2w_radio_error;
volatile uint32_t pico2w_radio_stage;

static uint32_t now_us(void) { return *(volatile uint32_t *)0x400b000cu; }

static int service(void *unused) {
    (void)unused;
    pico2w_radio_error = live_usb_hw_poll(&usb);
    uint32_t now = now_us();
    if (!pico2w_radio_error && now - watchdog_at >= 3000000u) {
        pico2w_radio_error = badger_schedule_bootsel(8000u);
        watchdog_at = now;
    }
    return !pico2w_radio_error && !usb.reboot_requested && now - began_at < 180000000u;
}

static void recover(void) __attribute__((noreturn));
static void recover(void) {
    wifi_wire_power_off(wifi_board_wire());
    live_usb_hw_disconnect();
    pico2w_radio_error = badger_schedule_bootsel(10u);
    while (1) __asm__ volatile ("wfi");
}

static void radio_fault(void) {
    fault = radio.error;
    fault_address = radio.address;
    fault_actual = radio.actual;
    if (link.active) { ++link.failed; link.active = 0u; }
    link.reply_pending = 0u;
    radio_ready = 0u;
    retry_at = now_us() + 1000000u;
    wifi_wire_power_off(wifi_board_wire());
}

static void radio_boot(void) {
    pico2w_radio_stage = 3u;
    wifi_board_init();
    wifi_board_select_pio(1u);
    clean_loader_init(&radio, wifi_board_wire(), service, 0);
    ++boots;
    if (clean_boot(&radio, clean_image, (uint32_t)(clean_image_end - clean_image),
                   PR_CHANNEL, link.session, 1u) &&
        clean_write(&radio, 0x18002024u, 0xf0u) &&
        clean_write(&radio, CLEAN_MBOX + 24u, 2u) &&
        clean_wait_state(&radio, 9u, 100000u)) {
        radio_ready = 1u;
        pico2w_radio_stage = 4u;
    } else radio_fault();
}

static int transmit(const pr_message_t *m) {
    uint8_t frame[PR_STORAGE], readback[PR_STORAGE];
    badger_bytes_zero(frame, sizeof(frame));
    if (!pr_encode(frame + 6u, m) || !clean_ofdm_plcp_rate(frame, PR_MPDU, 0u)) {
        radio.error = CLEAN_INPUT;
        return 0;
    }
    if (!clean_block(&radio, CLEAN_TX_BUFFER_ADDR, frame, sizeof(frame), 1u) ||
        !clean_block(&radio, CLEAN_TX_BUFFER_ADDR, readback, sizeof(readback), 0u)) return 0;
    if (!badger_bytes_equal(frame, readback, sizeof(frame))) {
        radio.error = CLEAN_READBACK;
        radio.address = CLEAN_TX_BUFFER_ADDR;
        radio.actual = diag_crc32(readback, sizeof(readback));
        return 0;
    }
    uint32_t command = 5u | clean_tx_length_bits(PR_MPDU + 6u) |
                       (1u << 19u) | (1u << 18u);
    if (!clean_write(&radio, CLEAN_MBOX + 24u, command) ||
        !clean_wait_command(&radio, command, 100000u)) return 0;
    uint32_t conditions, state;
    if (!clean_read(&radio, CLEAN_MBOX + 8u, &conditions) ||
        !clean_read(&radio, CLEAN_MBOX + 4u, &state)) return 0;
    if (state != 9u || conditions != 7u) {
        radio.error = CLEAN_CONTROLLER;
        radio.actual = state != 9u ? state : conditions;
        return 0;
    }
    if (m->kind == PR_REQUEST) ++tx_requests;
    else ++tx_replies;
    return 1;
}

static void snapshot(void) {
    uint8_t nonce[16], report[64], trace[WIFI_TRACE_SIZE];
    uint32_t generation = live_usb_probe_take(&usb, nonce);
    if (!generation) return;
    uint32_t error = 0u;
    if (nonce[0] == 1u) {
        if (!radio_ready || !pr_begin(&link, nonce[1], nonce + 2u, now_us()))
            error = CLEAN_INPUT;
    } else if (nonce[0]) error = CLEAN_INPUT;
    badger_bytes_zero(trace, sizeof(trace));
    uint32_t words[] = {
        PR_TRACE_MAGIC, PR_NODE, link.session, link.sequence,
        radio_ready, boots, fault, fault_address, fault_actual,
        tx_requests, tx_replies, link.requests, link.confirmed, link.failed,
        link.duplicates, link.reply_drops, raw_rx, rejected, radio.rx_resyncs,
        link.active, link.attempts, error, now_us(), pico2w_radio_stage,
    };
    for (uint32_t i = 0u; i < sizeof(words) / sizeof(words[0]); ++i)
        badger_bytes_store_u32(trace + i * 4u, words[i]);
    badger_bytes_copy(trace + 96u, link.id, 8u);
    badger_bytes_store_u32(trace + 104u, sys_before);
    badger_bytes_store_u32(trace + 108u, sys_after);
    badger_bytes_copy(trace + 128u, last_request, sizeof(last_request));
    badger_bytes_copy(trace + 256u, last_reply, sizeof(last_reply));
    usb_report_header(report, 0x46363255u, USB_REPORT_BUILD_ID, nonce, 3u);
    badger_bytes_store_u32(report + 32u, error);
    badger_bytes_store_u32(report + 36u, radio_ready);
    badger_bytes_store_u32(report + 40u, fault);
    badger_bytes_store_u32(report + 56u, PR_NODE);
    badger_bytes_store_u32(report + 60u, diag_crc32(report, 60u));
    live_usb_probe_finish(&usb, generation, report);
    live_usb_trace_finish(&usb, generation, trace);
}

#if PR_NODE != 1
static void screen(void) {
    if (screen_column == 320u) {
        if (now_us() - screen_at < 500000u || link.active || link.reply_pending) return;
        badger_gfx_clear(&canvas, 0x0863u);
        badger_gfx_text(&canvas, 8, 12, PR_NODE == 2 ? "CLEAN-RX BADGE A" :
                        "CLEAN-RX BADGE B", 2u, 0xffffu);
        badger_gfx_text(&canvas, 8, 44, "PICO + TWO BADGES / CH 6 / 6 MBPS", 1u, 0x9cf3u);
        badger_gfx_text(&canvas, 8, 68, radio_ready ? "RADIO READY" : "RADIO RESTARTING",
                        1u, radio_ready ? 0x5ecbu : 0xf9c7u);
        static const char *const names[] = {"TX REQUESTS", "RX REQUESTS", "CONFIRMED",
                                            "FAILED", "RADIO BOOTS", "LAST FAULT"};
        uint32_t counts[] = {tx_requests, link.requests, link.confirmed, link.failed, boots, fault};
        for (uint32_t i = 0u; i < 6u; ++i) {
            char number[12];
            badger_gfx_text(&canvas, 8, 94 + (int32_t)i * 18, names[i], 1u, 0xffffu);
            diag_decimal(number, (int32_t)counts[i]);
            badger_gfx_text(&canvas, 180, 94 + (int32_t)i * 18, number, 1u, 0xffffu);
        }
        badger_gfx_text(&canvas, 8, 216, "180 SECOND LAB TEST / UNENCRYPTED", 1u, 0xfda8u);
        screen_column = 0u;
    }
    badger_display_begin_region(screen_column, 0u, 4u, 240u);
    badger_display_write_pixels(canvas.pixels + screen_column * 240u, 4u * 240u);
    badger_display_end();
    screen_column += 4u;
    if (screen_column == 320u) screen_at = now_us();
}
#endif

void bare_main(void) {
    uint32_t id[2], random[4];
    char serial[17], half[9];
    pico2w_radio_stage = 1u;
    pico2w_radio_error = badger_schedule_bootsel(8000u);
    if (!pico2w_radio_error) pico2w_radio_error = nametag_clock_reference();
    if (!pico2w_radio_error) pico2w_radio_error = nametag_clock_frequency(9u, &sys_before);
#if PR_NODE == 1
    if (!pico2w_radio_error) pico2w_radio_error = pr_clock_sys();
#endif
    if (!pico2w_radio_error) pico2w_radio_error = nametag_clock_frequency(9u, &sys_after);
    if (!pico2w_radio_error) pico2w_radio_error = badger_rom_chip_id(id);
    if (!pico2w_radio_error) pico2w_radio_error = badger_rom_boot_random(random);
    if (pico2w_radio_error) while (1) __asm__ volatile ("wfi");
    uint8_t bytes[8];
    badger_bytes_store_u32(bytes, id[0]);
    badger_bytes_store_u32(bytes + 4u, id[1]);
    pr_init(&link, PR_NODE, bytes, random[0] ? random[0] : 1u);
    diag_hex(half, id[1], 8u);
    badger_bytes_copy((uint8_t *)serial, (uint8_t *)half, 8u);
    diag_hex(half, id[0], 8u);
    badger_bytes_copy((uint8_t *)serial + 8u, (uint8_t *)half, 8u);
    serial[16] = 0;
    live_usb_control_init(&usb, USB_REPORT_BUILD_ID, serial);
    pico2w_radio_stage = 2u;
    pico2w_radio_error = live_usb_hw_init();
    if (pico2w_radio_error) while (1) __asm__ volatile ("wfi");
    began_at = watchdog_at = now_us();
    /* Keep radio upload/PIO traffic out of the initial USB enumeration. */
    while (!usb.configured) {
        if (!service(0) || now_us() - began_at >= 10000000u) recover();
    }
#if PR_NODE != 1
    if (!badger_display_init_serviced(service, 0)) recover();
#endif
    radio_boot();
    for (;;) {
        if (!service(0)) recover();
        snapshot();
        uint32_t now = now_us();
        if (!radio_ready) {
            if (now - retry_at < 0x80000000u) radio_boot();
        } else {
            radio.started = radio.serviced_at = now;
            radio.elapsed_us = 0u;
            radio.budget_us = 2000000u;
            uint32_t size;
            if (!clean_receive(&radio, packet, &size)) radio_fault();
            else if (size) {
                ++raw_rx;
                int result = size == PR_PACKET && pr_metadata(packet) ?
                    pr_receive(&link, packet + 18u, PR_MPDU, now_us()) : 0;
                if (result == 1 || result == 3)
                    badger_bytes_copy(last_request, packet, sizeof(last_request));
                else if (result == 2)
                    badger_bytes_copy(last_reply, packet, sizeof(last_reply));
                else ++rejected;
            }
            pr_message_t message;
            if (radio_ready && pr_next(&link, now_us(), &message) && !transmit(&message))
                radio_fault();
        }
#if PR_NODE != 1
        screen();
#endif
    }
}

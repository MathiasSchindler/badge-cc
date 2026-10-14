#include "loader.h"
#include "build_id.h"
#include "frames.h"
#include "../../src/shared/bytes.h"
#include "../../src/usb/live_usb_hw.h"
#include "../../src/arch/rp2350/bootrom_reboot.h"
#include "../../src/platform/badger/wifi_board.h"
#include "../../src/tools/diagnostics_logic.h"
#include "../../src/radio/wifi_usb_report.h"

extern const uint8_t clean_image[], clean_image_end[];
static live_usb_control_t control;
static int32_t usb_error;
static uint32_t generation;
static uint8_t detail[WIFI_TRACE_SIZE];

static void beacon(const uint8_t *packet, uint32_t size) {
    uint8_t entry[40];
    if (size < 18u || !clean_beacon_info(packet + 18u, size - 18u, entry, packet[16] & 15u))
        return;
    uint32_t count = badger_bytes_load_u32(detail + 1632u);
    for (uint32_t i = 0u; i < count; ++i)
        if (badger_bytes_equal(detail + 1640u + 40u*i, entry, 6u)) return;
    if (count == 8u) return;
    badger_bytes_copy(detail + 1640u + 40u*count, entry, 40u);
    badger_bytes_store_u32(detail + 1632u, count + 1u);
}

static int receive_for(clean_loader_t *l, uint32_t duration, uint8_t after_tx) {
    uint32_t started = l->wire->now_us(l->wire->context);
    while (l->wire->now_us(l->wire->context) - started < duration) {
        uint8_t packet[544];
        uint32_t size;
        if (!clean_receive(l, packet, &size)) return 0;
        if (size != 0u) {
            uint32_t packets = badger_bytes_load_u32(detail + 1636u);
            int response = after_tx && size >= 18u + 40u &&
                packet[18] == 0x50u && packet[19] == 0u &&
                badger_bytes_equal(packet + 22u, detail + 1972u, 6u) &&
                clean_frame_valid(packet + 18u, size - 18u);
            if (packets == 0u || response) {
                badger_bytes_store_u32(detail + 1088u, size);
                badger_bytes_copy(detail + 1104u, packet, size);
            }
            if (response) {
                uint32_t responses = badger_bytes_load_u32(detail + 1960u);
                badger_bytes_store_u32(detail + 1960u, responses + 1u);
            }
            badger_bytes_store_u32(detail + 1636u, packets + 1u);
            beacon(packet, size);
        }
        uint32_t state;
        if (!clean_read(l, CLEAN_MBOX + 4u, &state)) return 0;
        if (state != 9u) {
            l->error = CLEAN_CONTROLLER;
            l->actual = state;
            return 0;
        }
    }
    return 1;
}

static int service(void *unused) {
    (void)unused;
    usb_error = live_usb_hw_poll(&control);
    return usb_error == 0 && !control.reboot_requested && control.configured &&
           control.probe_generation == generation;
}

static void run(const uint8_t nonce[16]) {
    clean_loader_t l;
    wifi_board_init();
    wifi_board_select_pio(1u);
    clean_loader_init(&l, wifi_board_wire(), service, 0);
    badger_bytes_zero(detail, sizeof(detail));
    uint32_t length = (uint32_t)(clean_image_end - clean_image);
    uint32_t channel = nonce[15];
    if (nonce[14] > 2u) l.error = CLEAN_INPUT;
    if (l.error == CLEAN_OK &&
        clean_boot(&l, clean_image, length, channel, badger_bytes_load_u32(nonce) | 1u, nonce[14] == 1u) &&
        clean_write(&l, 0x18002024u, 0xf0u) &&
        clean_write(&l, CLEAN_MBOX + 24u, 2u) && clean_wait_state(&l, 9u, 100000u)) {
        l.stage = 7u;
        (void)receive_for(&l, 700000u, 0u);
        if (l.error == CLEAN_OK && nonce[14] != 0u) {
            uint8_t frame[96];
            uint8_t ssid_length = detail[1646u];
            if (badger_bytes_load_u32(detail + 1632u) == 0u ||
                !clean_probe_request(frame, (uint16_t)badger_bytes_load_u32(nonce),
                                     detail + 1648u, ssid_length)) {
                l.error = CLEAN_INPUT;
            }
            /* Fresh local source MAC distinguishes an AP response from old traffic. */
            if (l.error == CLEAN_OK) {
                frame[16] = 0x02u;
                badger_bytes_copy(frame + 17u, nonce, 5u);
                badger_bytes_copy(detail + 1972u, frame + 16u, 6u);
                badger_bytes_store_u32(frame + 90u, diag_crc32(frame + 6u, 84u));
                badger_bytes_store_u32(detail + 1092u, 1u);
            }
            if (l.error == CLEAN_OK && clean_block(&l, 0x38400u, frame, sizeof(frame), 1u) &&
                clean_write(&l, CLEAN_MBOX + 24u, 5u | (94u << 8u) | (1u << 19u))) {
                uint32_t started_tx = l.wire->now_us(l.wire->context);
                uint32_t command = 0u;
                while (clean_read(&l, CLEAN_MBOX + 24u, &command)) {
                    if (command == 2u) {
                        badger_bytes_store_u32(detail + 1096u, 1u);
                        break;
                    }
                    uint32_t state;
                    if (!clean_read(&l, CLEAN_MBOX + 4u, &state)) break;
                    if (state == 255u) {
                        l.error = CLEAN_CONTROLLER;
                        l.actual = state;
                        break;
                    }
                    if (l.wire->now_us(l.wire->context) - started_tx >= 100000u) {
                        l.error = CLEAN_TIMEOUT;
                        break;
                    }
                }
                badger_bytes_store_u32(detail + 1100u, command);
                if (l.error == CLEAN_OK) (void)receive_for(&l, 200000u, 1u);
            }
        }
        if (l.error == CLEAN_OK && clean_write(&l, CLEAN_MBOX + 24u, 3u) &&
            clean_wait_state(&l, 10u, 200000u)) l.stage = 8u;
    }
    uint32_t saved_error = l.error, saved_addr = l.address, saved_actual = l.actual;
    /* Collect fault evidence even after a failed stage, still under a deadline. */
    l.error = CLEAN_OK;
    int snapshot = clean_block(&l, CLEAN_MBOX, detail + 32u, 448u, 0u) &&
                   clean_block(&l, CLEAN_MULTI, detail + 480u, 40u, 0u) &&
                   clean_block(&l, CLEAN_MULTI + 40u, detail + 520u, 568u, 0u);
    if (saved_error != CLEAN_OK) {
        l.error = saved_error;
        l.address = saved_addr;
        l.actual = saved_actual;
    } else if (!snapshot && l.error == CLEAN_OK) l.error = CLEAN_READBACK;
    uint32_t elapsed = l.wire->now_us(l.wire->context) - l.started;
    wifi_wire_power_off(l.wire);
    badger_bytes_store_u32(detail, CLEAN_DETAIL_MAGIC);
    badger_bytes_store_u32(detail + 4u, l.stage);
    badger_bytes_store_u32(detail + 8u, l.error);
    badger_bytes_store_u32(detail + 12u, l.address);
    badger_bytes_store_u32(detail + 16u, l.actual);
    badger_bytes_store_u32(detail + 20u, l.verified);
    badger_bytes_store_u32(detail + 24u, elapsed);
    badger_bytes_store_u32(detail + 28u, channel);
    wifi_probe_report_t report = {
        .stage = WIFI_PROBE_RESET, .result = WIFI_BUS_BACKPLANE_INVALID,
        .last_value = l.error, .elapsed_us = elapsed
    };
    uint8_t wire_report[64];
    wifi_usb_report_encode(wire_report, USB_REPORT_BUILD_ID, nonce, &report);
    live_usb_probe_finish(&control, generation, wire_report);
    live_usb_trace_finish(&control, generation, detail);
}

void bare_main(void) {
    int32_t recovery = badger_schedule_bootsel(8000u);
    uint32_t id[2];
    char serial[17], half[9];
    if (recovery != 0 || badger_rom_chip_id(id) != 0) while (1) __asm__ volatile("wfi");
    diag_hex(half, id[1], 8u);
    badger_bytes_copy((uint8_t *)serial, (const uint8_t *)half, 8u);
    diag_hex(half, id[0], 8u);
    badger_bytes_copy((uint8_t *)serial + 8u, (const uint8_t *)half, 8u);
    serial[16] = '\0';
    live_usb_control_init(&control, USB_REPORT_BUILD_ID, serial);
    if (live_usb_hw_init() != 0) while (1) __asm__ volatile("wfi");
    while (1) {
        usb_error = live_usb_hw_poll(&control);
        uint8_t nonce[16];
        generation = live_usb_probe_take(&control, nonce);
        if (generation != 0u && usb_error == 0) run(nonce);
        if (usb_error != 0 || control.reboot_requested) {
            wifi_wire_power_off(wifi_board_wire());
            /* Let EP0 complete the BOOTSEL request before disconnecting. */
            uint32_t start = *(volatile uint32_t *)0x400b000cu;
            while (*(volatile uint32_t *)0x400b000cu - start < 10000u)
                (void)live_usb_hw_poll(&control);
            live_usb_hw_disconnect();
            (void)badger_schedule_bootsel(10u);
            while (1) __asm__ volatile("wfi");
        }
    }
}

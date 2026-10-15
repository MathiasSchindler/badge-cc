#include "micro-signal.h"
#include "../../clean-rx/badge/loader.h"
#include "../arch/rp2350/bootrom_reboot.h"
#include "../platform/badger/display.h"
#include "../platform/badger/graphics.h"
#include "../platform/badger/home.h"
#include "../platform/badger/wifi_board.h"

#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))

extern const uint8_t clean_image[], clean_image_end[];
static badger_canvas_t canvas;
static clean_loader_t loader;
static uint32_t home_started;
static uint8_t home_held;
static uint8_t recover;
static const uint8_t button_pins[5] = {7u,9u,10u,11u,6u};

#ifdef MICRO_SIGNAL_SELF_TEST
#include "../usb/live_usb_hw.h"
#include "../radio/wifi_usb_report.h"
static uint32_t evidence[16];
static live_usb_control_t test_control;
static uint8_t test_usb_ready;
static uint32_t test_started, test_requests;
#endif

static uint32_t now(void) {
    return REG(0x400b000cu);
}

static void bootsel(void);

static int service(void *context) {
    (void)context;
#ifdef MICRO_SIGNAL_SELF_TEST
    if (test_usb_ready) {
        for (uint32_t i = 0u; i < 15u; ++i)
            badger_bytes_store_u32(test_control.report + i * 4u, evidence[i]);
        badger_bytes_store_u32(test_control.report + 60u,
                              diag_crc32(test_control.report, 60u));
        test_control.report_valid = 1u;
        test_control.report_test = WIFI_USB_TEST_ID;
        test_control.trace_valid = 1u;
        int32_t result = live_usb_hw_poll(&test_control);
        if (result != 0) {
            evidence[13] = (uint32_t)result;
            return 0;
        }
        if (live_usb_hw_stats()->setups != test_requests && now() - test_started < 600000000u) {
            test_requests = live_usb_hw_stats()->setups;
            /* Keep the test alive only while the collector is polling it. */
            REG(0x400d8004u) = 8000000u;
        }
        if (test_control.reboot_requested) recover = 1u;
    }
#endif
    if (!badger_home_pressed()) home_held = 0u;
    else if (!home_held) {
        home_held = 1u;
        home_started = now();
    } else if (now() - home_started >= 1500000u) recover = 1u;
    return !recover;
}

static void line(uint32_t y, const char *text, uint16_t color) {
    badger_gfx_rect(&canvas, 0, (int32_t)y, 320, 24, 0x0863u);
    badger_gfx_text(&canvas, 8, (int32_t)y + 3, text, 2u, color);
    badger_display_begin_region(0u, y, 320u, 24u);
    for (uint32_t x = 0u; x < 320u; ++x)
        badger_display_write_pixels(canvas.pixels + x * 240u + y, 24u);
    badger_display_end();
}

static void bootsel(void) {
    wifi_wire_power_off(wifi_board_wire());
    int32_t result = badger_reboot_to_bootsel();
    char number[12];
    line(52u, "BOOTSEL FAILED", 0xf9c7u);
    diag_decimal(number, result);
    line(76u, number, 0xf9c7u);
    while (1) __asm__ volatile("wfi");
}

static void counts(uint32_t sent, uint32_t received) {
    char text[32];
    badger_bytes_copy((uint8_t *)text, (const uint8_t *)"TX ", 3u);
    char number[12];
    diag_decimal(number, (int32_t)(sent % 1000000u));
    uint32_t at = 3u;
    for (uint32_t i = 0u; number[i]; ++i) text[at++] = number[i];
    text[at++] = ' '; text[at++] = 'R'; text[at++] = 'X'; text[at++] = ' ';
    diag_decimal(number, (int32_t)(received % 1000000u));
    for (uint32_t i = 0u; number[i]; ++i) text[at++] = number[i];
    text[at] = 0;
    line(148u, text, 0x9cf3u);
}

static void receive_counts(uint32_t dropped) {
    char text[32], number[12];
    badger_bytes_copy((uint8_t *)text, (const uint8_t *)"RX D ", 5u);
    uint32_t at = 5u;
    diag_decimal(number, (int32_t)dropped);
    for (uint32_t i = 0u; number[i]; ++i) text[at++] = number[i];
    text[at++] = ' '; text[at++] = 'R'; text[at++] = ' ';
    diag_decimal(number, (int32_t)(loader.rx_resyncs % 1000000u));
    for (uint32_t i = 0u; number[i]; ++i) text[at++] = number[i];
    text[at] = 0;
    line(172u, text, 0x9cf3u);
}

static void radio_error(void) {
    if (recover) bootsel();
    uint32_t error = loader.error, address = loader.address;
#ifdef MICRO_SIGNAL_SELF_TEST
    uint32_t actual = loader.actual;
    badger_bytes_store_u32(test_control.trace + 56u, actual);
    if (error == CLEAN_CONTROLLER) {
        loader.error = CLEAN_OK;
        loader.started = now();
        for (uint32_t i = 0u; i < 3u; ++i) {
            uint32_t value;
            if (!clean_read(&loader, CLEAN_MBOX + 12u + i * 4u, &value)) {
                badger_bytes_store_u32(test_control.trace + 60u, loader.error);
                break;
            }
            badger_bytes_store_u32(test_control.trace + 44u + i * 4u, value);
        }
        if (!clean_block(&loader, CLEAN_MBOX + 72u, test_control.trace + 64u, 96u, 0u) ||
            !clean_block(&loader, CLEAN_MBOX + 60u, test_control.trace + 160u, 12u, 0u))
            badger_bytes_store_u32(test_control.trace + 60u, loader.error);
        loader.error = error;
        loader.address = address;
        loader.actual = actual;
    }
    if (error == CLEAN_READBACK && address >= 0x38400u && address < 0x38460u) {
        uint8_t fresh[96];
        loader.error = CLEAN_OK;
        loader.started = now();
        if (clean_block(&loader, 0x38400u, fresh, sizeof(fresh), 0u))
            badger_bytes_store_u32(test_control.trace + 52u, diag_crc32(fresh, sizeof(fresh)));
        else
            badger_bytes_store_u32(test_control.trace + 60u, loader.error);
        loader.error = error;
        loader.address = address;
        loader.actual = actual;
    }
#endif
    char number[12];
    wifi_wire_power_off(wifi_board_wire());
    line(52u, "RADIO ERROR", 0xf9c7u);
    diag_decimal(number, (int32_t)error);
    line(76u, number, 0xf9c7u);
    diag_hex(number, address, 8u);
    line(100u, number, 0xf9c7u);
    diag_hex(number, loader.actual, 8u);
    line(124u, number, 0xf9c7u);
    line(172u, "RESET TO RETRY", 0x9cf3u);
#ifdef MICRO_SIGNAL_SELF_TEST
    evidence[1] = error;
    evidence[2] = address;
#endif
    while (1) if (!service(0)) bootsel();
}

static uint8_t buttons(void) {
    uint8_t mask = 0u;
    for (uint32_t i = 0u; i < 5u; ++i)
        if ((REG(0x40028000u + 8u * button_pins[i]) & (1u << 17u)) == 0u)
            mask |= (uint8_t)(1u << i);
    return mask;
}

static int transmit(uint8_t frame[96]) {
    uint8_t checked[96];
    if (!clean_block(&loader, 0x38400u, frame, 96u, 1u) ||
        !clean_block(&loader, 0x38400u, checked, 96u, 0u)) return 0;
    if (!badger_bytes_equal(frame, checked, 96u)) {
        loader.error = CLEAN_READBACK;
        for (uint32_t i = 0u; i < 96u; i += 4u) {
            if (badger_bytes_load_u32(frame + i) == badger_bytes_load_u32(checked + i)) continue;
            loader.address = 0x38400u + i;
            loader.actual = badger_bytes_load_u32(checked + i);
            break;
        }
#ifdef MICRO_SIGNAL_SELF_TEST
        badger_bytes_copy(test_control.trace + 64u, frame, 96u);
        badger_bytes_copy(test_control.trace + 160u, checked, 96u);
        badger_bytes_store_u32(test_control.trace + 44u, diag_crc32(frame, 96u));
        badger_bytes_store_u32(test_control.trace + 48u, diag_crc32(checked, 96u));
#endif
        return 0;
    }
    if (!clean_write(&loader, CLEAN_MBOX + 24u,
                     5u | (MICRO_SIGNAL_FRAME_BYTES << 8u) | (1u << 19u))) return 0;
    uint32_t start = now();
    while (1) {
        uint32_t command, state;
        if (!clean_read(&loader, CLEAN_MBOX + 24u, &command) ||
            !clean_read(&loader, CLEAN_MBOX + 4u, &state)) return 0;
        if (state != 9u) {
            loader.error = CLEAN_CONTROLLER;
            loader.actual = state;
            return 0;
        }
        if (command == 2u) break;
        if (now() - start >= 100000u) {
            loader.error = CLEAN_TIMEOUT;
            return 0;
        }
    }
    uint32_t conditions;
    if (!clean_read(&loader, CLEAN_MBOX + 8u, &conditions)) return 0;
    if (conditions != 7u) {
        loader.error = CLEAN_CONTROLLER;
        loader.actual = conditions;
        return 0;
    }
    return 1;
}

void bare_main(void) {
    badger_home_init();
    wifi_board_init();
    wifi_board_select_pio(1u);
    for (uint32_t i = 0u; i < 5u; ++i) {
        REG(0xd0000040u) = 1u << button_pins[i];
        REG(0x40028004u + 8u * button_pins[i]) = 5u;
        REG(0x40038004u + 4u * button_pins[i]) = 0xcau;
    }
    badger_display_init();
    badger_gfx_clear(&canvas, 0x0863u);
    badger_gfx_text(&canvas, 8, 12, "MICRO SIGNAL", 3u, 0xffffu);
    badger_gfx_text(&canvas, 8, 200, "CH 6 / 11 MBPS / NO AP", 1u, 0x9cf3u);
    badger_gfx_text(&canvas, 8, 216, "HOLD HOME FOR BOOTSEL", 1u, 0x9cf3u);
    badger_display_begin();
    badger_display_write_pixels(canvas.pixels, 320u * 240u);
    badger_display_end();
    line(52u, "LOADING RADIO", 0xffffu);

#ifdef MICRO_SIGNAL_SELF_TEST
    for (uint32_t i = 0u; i < 16u; ++i) evidence[i] = 0u;
    evidence[0] = 0x5447534du;
    if (badger_schedule_bootsel(8000u) != 0) {
        loader.error = CLEAN_INPUT;
        radio_error();
    }
#endif
    uint32_t id[2], random[4];
    clean_loader_init(&loader, wifi_board_wire(), service, 0);
    int32_t rom_result = badger_rom_chip_id(id);
    if (rom_result == 0) rom_result = badger_rom_boot_random(random);
    if (rom_result != 0) {
        loader.error = CLEAN_INPUT;
        loader.actual = (uint32_t)rom_result;
        radio_error();
    }
    static micro_signal_event_t outgoing;
    badger_bytes_store_u32(outgoing.sender, id[0]);
    badger_bytes_store_u32(outgoing.sender + 4u, id[1]);
    outgoing.session = diag_crc32((const uint8_t *)random, sizeof(random));
#ifdef MICRO_SIGNAL_SELF_TEST
    evidence[3] = id[0]; evidence[4] = id[1];
    evidence[5] = outgoing.session;
    char serial[17], half[9];
    diag_hex(half, id[1], 8u);
    badger_bytes_copy((uint8_t *)serial, (const uint8_t *)half, 8u);
    diag_hex(half, id[0], 8u);
    badger_bytes_copy((uint8_t *)serial + 8u, (const uint8_t *)half, 8u);
    serial[16] = 0;
    live_usb_control_init(&test_control, 0x4d534731u, serial);
    int32_t usb_result = live_usb_hw_init();
    if (usb_result != 0) {
        evidence[13] = (uint32_t)usb_result;
        loader.error = CLEAN_INPUT;
        radio_error();
    }
    test_usb_ready = 1u;
    test_started = now();
#endif
    if (!clean_boot(&loader, clean_image, (uint32_t)(clean_image_end - clean_image),
                    MICRO_SIGNAL_CHANNEL, outgoing.session | 1u, 1u) ||
        !clean_write(&loader, 0x18002024u, 0xf0u) ||
        !clean_write(&loader, CLEAN_MBOX + 24u, 2u) ||
        !clean_wait_state(&loader, 9u, 100000u)) {
        if (recover) bootsel();
        radio_error();
    }
    line(52u, "READY - BUTTONS 1 TO 5", 0x5ecbu);
    line(100u, "WAITING FOR PEER", 0xffffu);
    uint32_t sent = 0u, received = 0u, due = now(), state_due = now(), dropped = 0u;
    uint32_t resyncs = 0u;
    uint8_t queue[8], head = 0u, queued = 0u, repeats = 0u;
    uint8_t frame[96];
    static diag_buttons_t debounce;
    static micro_signal_peers_t peers;
    counts(sent, received);
#ifdef MICRO_SIGNAL_SELF_TEST
    uint32_t test_start = now();
    uint32_t test_sent = 0u;
#endif
    while (1) {
        /* Each loop is a new bounded operation, not an unbounded boot deadline. */
        loader.started = now();
        if (!service(0)) bootsel();
        uint8_t pressed = diag_buttons_update(&debounce, buttons(), now());
#ifdef MICRO_SIGNAL_SELF_TEST
        if (!test_sent && now() - test_start >= 400000u + (random[0] & 0x3ffffu)) {
            pressed |= 1u;
            test_sent = 1u;
        } else if (test_sent == 1u && now() - test_start >= 1200000u + (random[3] & 0x3ffffu)) {
            pressed |= 16u;
            test_sent = 2u;
        } else if (test_sent >= 2u && now() - test_start >=
                   2000000u + (test_sent - 2u) * 300000u + (random[3] & 0x3ffffu)) {
            pressed |= (uint8_t)(1u << (test_sent % 5u));
            ++test_sent;
        }
        evidence[14] = now() - test_start;
#endif
        for (uint32_t i = 0u; i < 5u; ++i) {
            if (!(pressed & (1u << i))) continue;
            if (queued == 8u) line(52u, "INPUT QUEUE FULL", 0xf9c7u);
            else {
                queue[(head + queued) & 7u] = (uint8_t)(i + 1u);
                ++queued;
            }
        }
        uint8_t packet[544];
        uint32_t size;
        if (!clean_receive(&loader, packet, &size)) {
            if (recover) bootsel();
            radio_error();
        }
#ifdef MICRO_SIGNAL_SELF_TEST
        if (size != 0u) {
            uint32_t packets = badger_bytes_load_u32(test_control.trace + 24u);
            badger_bytes_store_u32(test_control.trace + 24u, packets + 1u);
            badger_bytes_store_u32(test_control.trace + 28u,
                                  (uint32_t)packet[4] |
                                  (size > 18u ? (uint32_t)packet[18] << 8u : 0u));
        }
#endif
        micro_signal_event_t event;
        if (size >= 18u && micro_signal_decode(packet + 18u, size - 18u, &event) &&
            micro_signal_accept(&peers, &event, outgoing.sender)) {
            char text[] = "BUTTON 1 PRESSED";
            text[7] = (char)('0' + event.button);
            line(100u, text, 0x5ecbu);
            ++received;
            counts(sent, received);
#ifdef MICRO_SIGNAL_SELF_TEST
            evidence[7] = received;
            evidence[8] = badger_bytes_load_u32(event.sender);
            evidence[9] = badger_bytes_load_u32(event.sender + 4u);
            evidence[10] = event.session;
            evidence[11] = event.sequence;
            evidence[12] = event.button;
#endif
        }
        if (repeats == 0u && queued != 0u) {
            outgoing.button = queue[head];
            head = (uint8_t)((head + 1u) & 7u);
            --queued;
            ++outgoing.sequence;
            micro_signal_frame(frame, &outgoing);
            repeats = 3u;
            due = now() + (random[1] & 0x3fffu);
        }
        if (repeats && (uint32_t)(now() - due) < 0x80000000u) {
            if (!transmit(frame)) {
                if (recover) bootsel();
                radio_error();
            }
#ifdef MICRO_SIGNAL_SELF_TEST
            ++evidence[6];
#endif
            --repeats;
            due = now() + 30000u + ((random[2] ^ now()) & 0x3fffu);
            if (!repeats) {
                char text[] = "BUTTON 1 SENT";
                text[7] = (char)('0' + outgoing.button);
                line(52u, text, 0xffffu);
                ++sent;
                counts(sent, received);
            }
        }
        if (now() - state_due >= 100000u) {
            uint32_t state;
            if (!clean_read(&loader, CLEAN_MBOX + 4u, &state)) radio_error();
            if (state != 9u) {
                loader.error = CLEAN_CONTROLLER;
                loader.actual = state;
                radio_error();
            }
            uint32_t f2_counts;
            if (!clean_read(&loader, CLEAN_MBOX + 444u, &f2_counts)) radio_error();
#ifdef MICRO_SIGNAL_SELF_TEST
            evidence[2] = f2_counts;
            static const uint32_t registers[5] = {
                0x18002210u,0x18002204u,0x18002214u,0x18002200u,0x18002020u
            };
            for (uint32_t i = 0u; i < 5u; ++i) {
                uint32_t value;
                if (!clean_read(&loader, registers[i], &value)) radio_error();
                badger_bytes_store_u32(test_control.trace + 4u * i, value);
            }
            uint32_t spi_status;
            wifi_bus_result_t bus_result = wifi_gspi_read(loader.wire, 0u, 8u, 4u,
                                                         WIFI_ORDER_NORMAL32, &spi_status);
            if (bus_result != WIFI_BUS_OK) {
                loader.error = CLEAN_BUS;
                loader.actual = (uint32_t)bus_result;
                radio_error();
            }
            badger_bytes_store_u32(test_control.trace + 20u, spi_status);
            if (wifi_gspi_read(loader.wire, 0u, 4u, 2u, WIFI_ORDER_NORMAL32, &spi_status) != WIFI_BUS_OK) {
                loader.error = CLEAN_BUS;
                radio_error();
            }
            badger_bytes_store_u32(test_control.trace + 32u, spi_status);
            if (wifi_gspi_read(loader.wire, 0u, 0u, 4u, WIFI_ORDER_NORMAL32, &spi_status) != WIFI_BUS_OK) {
                loader.error = CLEAN_BUS;
                radio_error();
            }
            badger_bytes_store_u32(test_control.trace + 36u, spi_status);
            badger_bytes_store_u32(test_control.trace + 40u, loader.rx_resyncs);
            if (!clean_block(&loader, CLEAN_MBOX + 72u, test_control.trace + 64u, 96u, 0u) ||
                !clean_block(&loader, CLEAN_MBOX + 60u, test_control.trace + 160u, 12u, 0u))
                radio_error();
#endif
            if ((f2_counts >> 16u) != dropped || loader.rx_resyncs != resyncs) {
                dropped = f2_counts >> 16u;
                resyncs = loader.rx_resyncs;
                receive_counts(dropped);
            }
            state_due = now();
        }
    }
}

#include "protocol.h"
#include "frame.h"
#include "build_id.h"
#include "../../src/usb/live_usb_hw.h"
#include "../../src/arch/rp2350/bootrom_reboot.h"
#include "../../src/platform/badger/wifi_board.h"
#include "../../src/radio/wifi_firmware.h"
#include "../../src/radio/wifi_assets.h"
#include "../../src/radio/wifi_link.h"
#include "../../src/shared/bytes.h"

static live_usb_control_t control;
static wifi_fw_report_t firmware;
static uint32_t generation;
static uint32_t run_started, recovery_at;
static uint8_t session_active;
static int32_t usb_error;
static uint8_t evidence[CLEAN_PEER_TRACE_BYTES];
static wifi_link_t link;
static int service(void *context, const wifi_fw_report_t *report);

static void recovery_service(void) {
    if (!session_active) return;
    uint32_t now = *(volatile uint32_t *)0x400b000cu;
    if (now - run_started >= 16000000u) usb_error = -101;
    if (!usb_error && now - recovery_at >= 4000000u) {
        usb_error = badger_schedule_bootsel(8000u);
        recovery_at = now;
    }
}

static int link_control(void *context, uint32_t command, uint8_t set, uint8_t interface,
                         const uint8_t *request, uint32_t size, uint8_t *reply,
                         uint32_t capacity, uint32_t *received) {
    (void)context;
    int ok = wifi_firmware_control(command, set, interface, request, size,
                                    reply, capacity, received);
    link.report.controller_status = firmware.controller_status;
    return ok;
}

static int multicast_rate(uint32_t rate, uint8_t set, uint32_t *readback) {
    uint8_t request[32], reply[32];
    badger_bytes_zero(request, sizeof(request));
    static const char name[] = "2g_mrate";
    for (uint32_t i = 0u; i < sizeof(name); ++i) request[i] = (uint8_t)name[i];
    badger_bytes_store_u32(request + sizeof(name), rate);
    uint32_t received;
    int ok = wifi_firmware_control(set ? 263u : 262u, set, 1u, request,
        set ? sizeof(name) + 4u : sizeof(request), reply, sizeof(reply), &received);
    if (ok && !set) {
        if (received < 4u) {
            firmware.error = WIFI_FW_PROTOCOL;
            return 0;
        }
        *readback = usb_report_u32(reply);
    }
    return ok;
}

static void transmit(const uint8_t nonce[16]) {
    uint8_t mac[6];
    for (uint32_t i = 0u; i < 6u; ++i) mac[i] = clean_peer_mac_byte(nonce, i);
    wifi_link_ops_t ops = {.context = 0, .control = link_control};
    wifi_link_init(&link, &ops, WIFI_LINK_OPEN_AP, nonce, mac);
    uint32_t stage = 1u, sent = 0u, readback = 0u;
    uint32_t requested = clean_peer_ht_rate(nonce[13]);
    int ok = nonce[13] < 8u && wifi_link_configure(&link);
    if (ok) {
        stage = 2u;
        ok = multicast_rate(requested, 1u, &readback);
    }
    if (ok) {
        stage = 3u;
        ok = multicast_rate(0u, 0u, &readback);
    }
    if (ok && !clean_peer_rate_matches(readback, nonce[13])) {
        firmware.error = WIFI_FW_PROTOCOL;
        ok = 0;
    }
    if (ok) {
        stage = 4u;
        ok = wifi_firmware_data_enable();
    }
    const wifi_wire_t *wire = wifi_board_wire();
    uint32_t started = wire->now_us(wire->context);
    while (ok && sent < CLEAN_PEER_FRAME_COUNT &&
           wire->now_us(wire->context) - started < 1800000u) {
        if (!service(0, 0)) { ok = 0; break; }
        if (wire->now_us(wire->context) - started < 300000u + sent * 100000u) continue;
        uint8_t frame[CLEAN_PEER_ETHERNET_BYTES];
        clean_peer_ethernet(frame, nonce, (uint8_t)sent);
        ok = wifi_firmware_send_frame(1u, frame, sizeof(frame));
        if (ok) ++sent;
    }
    if (ok && sent != CLEAN_PEER_FRAME_COUNT) {
        firmware.error = WIFI_FW_TIMEOUT;
        ok = 0;
    }
    if (ok) {
        uint32_t drained = wire->now_us(wire->context);
        while (wire->now_us(wire->context) - drained < 300000u && service(0, 0)) {}
        ok = service(0, 0);
    }
    uint8_t *t = evidence + CLEAN_PEER_TX_DETAIL;
    badger_bytes_store_u32(t, ok ? 5u : stage);
    badger_bytes_store_u32(t + 4u, requested);
    badger_bytes_store_u32(t + 8u, readback);
    badger_bytes_store_u32(t + 12u, sent);
    badger_bytes_store_u32(t + 16u, firmware.error);
    badger_bytes_store_u32(t + 20u, firmware.controller_status);
    badger_bytes_store_u32(t + 24u, link.report.last_command);
    badger_bytes_store_u32(t + 28u, link.report.error);
    badger_bytes_store_u32(t + 32u, 1u);
}

static int service(void *context, const wifi_fw_report_t *report) {
    (void)context;
    (void)report;
    recovery_service();
    if (!usb_error) usb_error = live_usb_hw_poll(&control);
    return !usb_error && !control.reboot_requested && control.configured &&
           generation == control.probe_generation;
}

static int measure_clock(void) {
    volatile uint32_t *sof = (volatile uint32_t *)0x50110048u;
    volatile uint32_t *timer = (volatile uint32_t *)0x400b000cu;
    uint32_t previous = *sof & 0x7ffu, started = *timer, first = 0u, frames = 0u;
    uint8_t aligned = 0u;
    while (*timer - started < 100000u) {
        if (!service(0, 0)) return 0;
        uint32_t now = *timer, frame = *sof & 0x7ffu;
        if (frame == previous) continue;
        if (!aligned) { first = now; aligned = 1u; }
        else frames += (frame - previous) & 0x7ffu;
        previous = frame;
        if (frames >= 32u) {
            wifi_board_trace_clock_sample(now - first, frames);
            return 1;
        }
    }
    usb_error = -100;
    return 0;
}

static void probe(const uint8_t nonce[16]) {
    const wifi_fw_assets_t *assets = wifi_firmware_assets();
    badger_bytes_zero(evidence, sizeof(evidence));
    badger_bytes_store_u32(evidence, CLEAN_PEER_MAGIC);
    badger_bytes_store_u32(evidence + 4u, assets->firmware_crc);
    badger_bytes_store_u32(evidence + 8u, assets->nvram_crc);
    badger_bytes_store_u32(evidence + 12u, assets->clm_crc);
    badger_bytes_store_u32(evidence + 16u, CLEAN_PEER_RECORDS);
    badger_bytes_store_u32(evidence + 20u, nonce[14] == 2u ? 2u : 1u);
    int ready = wifi_firmware_begin(wifi_board_wire(), assets, &firmware, service, 0);
    uint8_t report[64];
    wifi_firmware_report(report, USB_REPORT_BUILD_ID, nonce, &firmware);
    if (ready && nonce[14] == 1u) {
        transmit(nonce);
    } else if (ready) {
        for (uint32_t i = 0u; i < 60u; ++i)
            evidence[28u + i] = (uint8_t)firmware.version[i];
        for (uint32_t i = 0u; i < CLEAN_PEER_RECORDS && service(0, 0); ++i) {
            uint8_t request[CLEAN_PEER_CONTROL_BYTES], reply[CLEAN_PEER_CONTROL_BYTES], set;
            uint32_t command, received = 0u;
            uint32_t size = nonce[14] == 2u ?
                clean_peer_prepare_monitor(i, request, &command, &set) :
                clean_peer_prepare_query(i, request, &command, &set);
            int ok = wifi_firmware_control(command, set, 0u, request, size,
                                            reply, sizeof(reply), &received);
            uint8_t *r = evidence + CLEAN_PEER_RECORD_BASE + i * CLEAN_PEER_RECORD_BYTES;
            badger_bytes_store_u32(r, i);
            badger_bytes_store_u32(r + 4u, (uint32_t)ok);
            badger_bytes_store_u32(r + 8u, firmware.error);
            badger_bytes_store_u32(r + 12u, firmware.controller_status);
            badger_bytes_store_u32(r + 16u, received);
            badger_bytes_store_u32(r + 20u, received >= 4u ? usb_report_u32(reply) : 0u);
            badger_bytes_store_u32(evidence + 24u, i + 1u);
            if (!ok && firmware.error != WIFI_FW_CONTROLLER_ERROR) break;
        }
    }
    wifi_firmware_stop();
    if (!usb_error && !control.reboot_requested) {
        live_usb_probe_finish(&control, generation, report);
        live_usb_trace_finish(&control, generation, evidence);
    }
}

void bare_main(void) {
    if (badger_schedule_bootsel(8000u)) while (1) __asm__ volatile ("wfi");
    uint32_t id[2];
    char serial[17], half[9];
    if (badger_rom_chip_id(id)) while (1) __asm__ volatile ("wfi");
    diag_hex(half, id[1], 8u);
    for (uint32_t i = 0u; i < 8u; ++i) serial[i] = half[i];
    diag_hex(half, id[0], 8u);
    for (uint32_t i = 0u; i < 8u; ++i) serial[8u + i] = half[i];
    serial[16] = '\0';
    live_usb_control_init(&control, USB_REPORT_BUILD_ID, serial);
    usb_error = live_usb_hw_init();
    while (!usb_error && !control.reboot_requested) {
        recovery_service();
        if (usb_error) break;
        usb_error = live_usb_hw_poll(&control);
        uint8_t nonce[16];
        generation = live_usb_probe_take(&control, nonce);
        if (!usb_error && generation) {
            run_started = recovery_at = *(volatile uint32_t *)0x400b000cu;
            session_active = 1u;
            wifi_board_init();
            wifi_board_select_pio(1u);
            if (measure_clock()) probe(nonce);
        }
    }
    wifi_firmware_stop();
    if (!usb_error && control.reboot_requested) {
        uint32_t started = *(volatile uint32_t *)0x400b000cu;
        while (!usb_error && *(volatile uint32_t *)0x400b000cu - started < 10000u)
            usb_error = live_usb_hw_poll(&control);
    }
    live_usb_hw_disconnect();
    (void)badger_schedule_bootsel(10u);
    while (1) __asm__ volatile ("wfi");
}

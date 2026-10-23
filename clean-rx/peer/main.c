#include "protocol.h"
#include "frame.h"
#include "lifecycle.h"
#include "lifecycle_usb.h"
#include "network.h"
#include "build_id.h"
#include "../../src/usb/live_usb_hw.h"
#include "../../src/arch/rp2350/bootrom_reboot.h"
#include "../../src/arch/rp2350/nametag_clock.h"
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
static uint32_t run_limit = 16000000u;
static uint8_t evidence[CLEAN_PEER_TRACE_BYTES];
static wifi_link_t link;
static clean_network_t network;
static int service(void *context, const wifi_fw_report_t *report);

static void recovery_service(void) {
    if (!session_active) return;
    uint32_t now = *(volatile uint32_t *)0x400b000cu;
    if (now - run_started >= run_limit) usb_error = -101;
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

static void network_ap(const uint8_t nonce[16], const uint8_t report[64]) {
    uint8_t mac[6], empty[6] = {0};
    for (uint32_t i = 0u; i < 6u; ++i) mac[i] = clean_peer_mac_byte(nonce, i);
    const wifi_wire_t *wire = wifi_board_wire();
    uint32_t seconds = nonce[12] | (uint32_t)nonce[13] << 8u;
    uint32_t started = wire->now_us(wire->context), published = started;
    wifi_link_ops_t ops = {.context = 0, .control = link_control};
    wifi_link_init(&link, &ops, WIFI_LINK_OPEN_AP, nonce, mac);
    int ok = wifi_link_configure(&link) && wifi_firmware_data_enable();
    clean_network_init(&network, 0u, mac, empty, empty, 0u, seconds, started);
    badger_bytes_store_u32(evidence + 20u, CLEAN_NETWORK_AP_MODE);
    badger_bytes_copy(evidence + CLEAN_NETWORK_AP_DETAIL,
        (const uint8_t *)&network.report, sizeof(network.report));
    if (ok) ok = clean_lifecycle_snapshot(&control, generation, report, evidence, 0u);
    while (ok && !network.report.error &&
           wire->now_us(wire->context) - started < (seconds + 15u) * 1000000u) {
        if (!service(0, 0)) { ok = 0; break; }
        uint32_t now = wire->now_us(wire->context);
        if (network.report.bound && now - network.started >= (seconds + 2u) * 1000000u) break;
        if (now - published >= 1000000u) {
            badger_bytes_copy(evidence + CLEAN_NETWORK_AP_DETAIL,
                (const uint8_t *)&network.report, sizeof(network.report));
            if (!clean_lifecycle_snapshot(&control, generation, report, evidence, 1u)) {
                ok = 0; break;
            }
            published = now;
        }
        static uint8_t frame[CLEAN_NETWORK_MTU];
        uint8_t interface;
        uint32_t length;
        int received = wifi_firmware_read_frame(frame, &length, &interface);
        if (received < 0) { ok = 0; break; }
        if (received && interface == 1u)
            (void)clean_network_input(&network, frame, length);
        uint8_t bound = network.report.bound;
        clean_network_step(&network, now);
        if (!bound && network.report.bound &&
            (!wifi_link_poll(&link) || !link.report.associated ||
             !badger_bytes_equal(link.report.peer, network.report.peer, 6u))) {
            ok = 0; break;
        }
        const uint8_t *out = clean_network_output(&network, &length);
        if (out) {
            if (!wifi_firmware_send_frame(1u, out, length)) { ok = 0; break; }
            clean_network_sent(&network);
        }
    }
    if (!network.report.error && (!ok || usb_error || !network.report.bound))
        network.report.error = firmware.error ? firmware.error : 0x100u;
    network.report.state = network.report.error ? 3u : 2u;
    network.report.abandoned += !!network.pending + network.tx.count + network.rx.count;
    badger_bytes_copy(evidence + CLEAN_NETWORK_AP_DETAIL,
        (const uint8_t *)&network.report, sizeof(network.report));
}

static void lifecycle_ap(const uint8_t nonce[16], const uint8_t report[64]) {
    clean_lifecycle_ap_report_t ap;
    badger_bytes_zero((uint8_t *)&ap, sizeof(ap));
    ap.magic = CLEAN_LIFECYCLE_AP_MAGIC; ap.channel = 1u;
    for (uint32_t i = 0u; i < 6u; ++i) ap.mac[i] = clean_peer_mac_byte(nonce, i);
    badger_bytes_store_u32(evidence + 20u, CLEAN_LIFECYCLE_AP_MODE);
    const wifi_wire_t *wire = wifi_board_wire();
    uint32_t started = wire->now_us(wire->context), phase_at = started, published_at = started;
    ap.startup_us = started - run_started;
    uint8_t up = 0u, ready = 0u;
    int ok = nonce[13] >= 8u && nonce[13] <= 120u && nonce[12] <= 5u;
    wifi_link_ops_t ops = {.context = 0, .control = link_control};
    while (ok && service(0, 0) &&
           wire->now_us(wire->context) - started < (uint32_t)nonce[13] * 1000000u) {
        uint32_t now = wire->now_us(wire->context);
        if (ready && now - published_at >= 1000000u) {
            ap.elapsed_us = now - started;
            badger_bytes_copy(evidence + CLEAN_LIFECYCLE_AP_DETAIL,
                (const uint8_t *)&ap, sizeof(ap));
            if (!clean_lifecycle_snapshot(&control, generation, report, evidence, 1u)) {
                ok = 0; break;
            }
            published_at = now;
        }
        if (!up && (!ready || now - phase_at >= 3000000u)) {
            ap.operation = 1u;
            if (ready && !wifi_firmware_begin(wire, wifi_firmware_assets(), &firmware, service, 0)) {
                ok = 0; break;
            }
            wifi_link_init(&link, &ops, WIFI_LINK_OPEN_AP, nonce, ap.mac);
            ap.operation = 2u;
            if (!wifi_link_configure(&link) || !wifi_firmware_data_enable()) {
                ok = 0; break;
            }
            up = 1u; phase_at = wire->now_us(wire->context);
            if (!ready) {
                ap.state = 1u;
                badger_bytes_copy(evidence + CLEAN_LIFECYCLE_AP_DETAIL,
                    (const uint8_t *)&ap, sizeof(ap));
                if (!clean_lifecycle_snapshot(&control, generation, report, evidence, 0u)) {
                    ok = 0; break;
                }
                ready = 1u;
            }
            now = wire->now_us(wire->context);
        }
        if (up && ap.cycles < nonce[12] && now - phase_at >= 8000000u) {
            wifi_firmware_stop();
            up = 0u; phase_at = wire->now_us(wire->context);
            ++ap.cycles;
            continue;
        }
        if (!up) continue;
        uint8_t frame[1514], interface;
        uint32_t length;
        ap.operation = 3u;
        int received = wifi_firmware_read_frame(frame, &length, &interface);
        if (received < 0) { ok = 0; break; }
        if (!received || interface != 1u || length != CLEAN_LIFECYCLE_ETHERNET_BYTES ||
            !badger_bytes_equal(frame, ap.mac, 6u) || frame[12] != 0x88u ||
            frame[13] != 0xb5u) continue;
        uint8_t *p = frame + 14u;
        uint32_t epoch = badger_bytes_load_u32(p + 14u);
        uint32_t sequence = badger_bytes_load_u32(p + 18u);
        if (!clean_lifecycle_payload_valid(p, CLEAN_LIFECYCLE_PAYLOAD_BYTES, p + 8u,
                                           epoch, sequence, 0u)) continue;
        ap.operation = 4u;
        if (!wifi_link_poll(&link) || !link.report.associated ||
            !badger_bytes_equal(link.report.peer, frame + 6u, 6u)) {
            ok = 0; break;
        }
        if (ap.received && (!badger_bytes_equal(ap.peer, frame + 6u, 6u) ||
                            !badger_bytes_equal(ap.session, p + 8u, 6u))) continue;
        badger_bytes_copy(ap.peer, frame + 6u, 6u);
        badger_bytes_copy(ap.session, p + 8u, 6u);
        uint32_t i;
        for (i = 0u; i < ap.associations; ++i) if (ap.epochs[i] == epoch) break;
        if (i == ap.associations) {
            if (i >= sizeof(ap.epochs) / sizeof(ap.epochs[0])) { ok = 0; break; }
            ap.epochs[ap.associations++] = epoch;
        }
        ++ap.received; ap.last_epoch = epoch; ap.last_sequence = sequence;
        badger_bytes_copy(frame, ap.peer, 6u);
        badger_bytes_copy(frame + 6u, ap.mac, 6u);
        p[22] = 1u;
        ap.operation = 5u;
        if (!wifi_firmware_send_frame(1u, frame, length)) { ok = 0; break; }
        ++ap.echoed;
    }
    ap.state = ok && ready && !usb_error ? 2u : 3u;
    ap.error = ap.state == 2u ? 0u : firmware.error ? firmware.error : 0x100u | link.report.error;
    ap.firmware_stage = firmware.stage; ap.address = firmware.address;
    ap.expected = firmware.expected; ap.actual = firmware.actual;
    ap.link_error = link.report.error; ap.command = link.report.last_command;
    ap.controller_status = link.report.controller_status;
    ap.rx_header = badger_bytes_load_u32(firmware.last_rx_prefix + 4u);
    ap.elapsed_us = wire->now_us(wire->context) - started;
    badger_bytes_copy(evidence + CLEAN_LIFECYCLE_AP_DETAIL, (const uint8_t *)&ap, sizeof(ap));
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

static int measure_clock(uint8_t calibrated) {
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
            if (calibrated && (now - first < frames * 990u ||
                               now - first > frames * 1010u)) {
                usb_error = -103;
                return 0;
            }
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
    run_limit = nonce[14] == CLEAN_NETWORK_AP_MODE ?
        ((uint32_t)(nonce[12] | (uint32_t)nonce[13] << 8u) + 25u) * 1000000u :
        nonce[14] == CLEAN_LIFECYCLE_AP_MODE ? ((uint32_t)nonce[13] + 20u) * 1000000u : 16000000u;
    int ready = wifi_firmware_begin(wifi_board_wire(), assets, &firmware, service, 0);
    uint8_t report[64];
    wifi_firmware_report(report, USB_REPORT_BUILD_ID, nonce, &firmware);
    if (ready && nonce[14] == CLEAN_NETWORK_AP_MODE) {
        network_ap(nonce, report);
    } else if (ready && nonce[14] == CLEAN_LIFECYCLE_AP_MODE) {
        lifecycle_ap(nonce, report);
    } else if (ready && nonce[14] == 1u) {
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
        if (nonce[14] == CLEAN_LIFECYCLE_AP_MODE || nonce[14] == CLEAN_NETWORK_AP_MODE) {
            if (!clean_lifecycle_snapshot(&control, generation, report, evidence, 1u))
                usb_error = -102;
        } else {
            live_usb_probe_finish(&control, generation, report);
            live_usb_trace_finish(&control, generation, evidence);
        }
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
            if (nonce[14] == CLEAN_LIFECYCLE_AP_MODE || nonce[14] == CLEAN_NETWORK_AP_MODE)
                usb_error = nametag_clock_reference();
            if (usb_error) break;
            run_started = recovery_at = *(volatile uint32_t *)0x400b000cu;
            session_active = 1u;
            wifi_board_init();
            wifi_board_select_pio(1u);
            if (measure_clock(nonce[14] == CLEAN_LIFECYCLE_AP_MODE ||
                              nonce[14] == CLEAN_NETWORK_AP_MODE)) probe(nonce);
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

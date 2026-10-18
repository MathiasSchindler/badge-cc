#include "loader.h"
#include "build_id.h"
#include "frames.h"
#include "station.h"
#include "station_rx.h"
#include "wpa2.h"
#include "../src/unicast.h"
#include "../src/tx_diag.h"
#include "../src/mac_soak.h"
#include "../src/ht20.h"
#include "../peer/frame.h"
#include "../../src/shared/bytes.h"
#include "../../src/usb/live_usb_hw.h"
#include "../../src/arch/rp2350/bootrom_reboot.h"
#include "../../src/platform/badger/wifi_board.h"
#include "../../src/tools/diagnostics_logic.h"
#include "../../src/radio/wifi_usb_report.h"
#include "../../src/platform/badger/display.h"
#include "../../src/platform/badger/graphics.h"

_Static_assert(CLEAN_STATION_TRACE_DETAIL + sizeof(clean_mac_trace_status_t) <=
               CLEAN_SOAK_FAULT_DETAIL, "MAC trace must not overlap soak fault evidence");

extern const uint8_t clean_image[], clean_image_end[];
static live_usb_control_t control;
static int32_t usb_error;
static uint32_t generation;
static uint8_t detail[WIFI_TRACE_SIZE];
static badger_canvas_t canvas;
static uint8_t peer_challenge[14];
static clean_station_ap_t station_ap;
static uint8_t station_target[6];
static int station_ap_status;
static clean_wpa_t wpa;
static clean_mac_rx_t station_rx;
static clean_qos_rx_t station_qos;
static uint32_t station_wmm_updates;
static uint8_t station_wmm_offered;
static uint8_t wpa_secret[64];
/* RX consumers run synchronously; share a packet outside the 8 KiB stack. */
static uint8_t rx_packet[CLEAN_RX_PACKET_BYTES];
static uint8_t soak_tx[CLEAN_TX_STORAGE_BYTES];
static uint32_t soak_watchdog_at;
static int station_receive(clean_loader_t *l, const uint8_t mac[6], uint32_t *size);
static int station_command(clean_loader_t *l, uint8_t *bytes, uint32_t size, uint32_t cmd);

static int station_mode(void) { return detail[2000u]==16u || detail[2000u]==18u; }
static int soak_mode(void) { return detail[2000u]==19u || detail[2000u]==20u; }
static int ht_probe_mode(void) { return detail[2000u]>=21u && detail[2000u]<=31u; }

static void screen(const char *status, uint32_t packets, uint32_t error) {
    char number[12];
    badger_gfx_clear(&canvas, 0x0863u);
    badger_gfx_text(&canvas, 8, 12, "SOURCE WIFI TEST", 2u, 0xffffu);
    badger_gfx_text(&canvas, 8, 52, status, 1u, error ? 0xf9c7u : 0x5ecbu);
    badger_gfx_text(&canvas, 8, 88, "RECEIVED RADIO PACKETS", 1u, 0xffffu);
    diag_decimal(number, (int32_t)packets);
    badger_gfx_text(&canvas, 8, 110, number, 2u, 0xffffu);
    badger_gfx_text(&canvas, 8, 148, "NO SDK / NO LIBC", 1u, 0x9cf3u);
    badger_gfx_text(&canvas, 8, 176, "AUTOMATIC BOOTSEL RECOVERY", 1u, 0xfda8u);
    if (error) {
        diag_decimal(number, (int32_t)error);
        badger_gfx_text(&canvas, 8, 200, number, 1u, 0xf9c7u);
    }
    badger_display_begin();
    for (uint32_t i = 0u; i < BADGER_GFX_WIDTH * BADGER_GFX_HEIGHT; ++i)
        badger_display_write(canvas.pixels[i]);
    badger_display_end();
}

static void beacon(const uint8_t *packet, uint32_t size) {
    if (station_mode())
        clean_station_discover(packet, size, station_target, &station_ap, &station_ap_status);
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
        uint8_t *packet=rx_packet;
        uint32_t size;
        if (station_mode() && badger_bytes_load_u32(detail + 524u)) {
            if (!station_receive(l, detail + 1972u, &size)) return 0;
        } else if (!clean_receive(l, packet, &size)) return 0;
        if (size != 0u) {
            if (detail[2000u] == 17u)
                clean_station_monitor(detail, packet, size, station_target,
                    l->wire->now_us(l->wire->context) - started);
            if (station_mode() && size >= 18u &&
                badger_bytes_load_u32(detail + 524u) == 4u) {
                int reason = clean_station_departure(packet + 18u, size - 18u,
                                                     detail + 1972u, detail + 1980u);
                if (reason >= 0) {
                    clean_station_forget_association(&station_ap);
                    badger_bytes_store_u32(detail + 524u, 5u);
                    badger_bytes_store_u32(detail + 568u, (uint32_t)reason);
                    badger_bytes_store_u32(detail + 536u, 0u);
                }
                if (station_ap.secured && size <= CLEAN_DETAIL_PACKET_BYTES &&
                    clean_station_eapol_m1(packet + 18u, size - 18u,
                                           detail + 1972u, detail + 1980u)) {
                    badger_bytes_store_u32(detail + 576u,
                        badger_bytes_load_u32(detail + 576u) + 1u);
                    badger_bytes_store_u32(detail + 1088u, size);
                    badger_bytes_copy(detail + 1104u, packet, size);
                }
            }
            uint32_t packets = badger_bytes_load_u32(detail + 1636u);
            int peer = size >= 18u && (detail[2000u] == 13u ?
                clean_peer_reply(packet + 18u, size - 18u, peer_challenge) :
                detail[2000u] == 22u ?
                clean_ht_probe_match(packet + 18u, size - 18u, peer_challenge) :
                detail[2000u] == 25u ?
                clean_ht_rx_metadata(packet[15], packet[16], packet[17], size - 18u,
                    detail[2001u], peer_challenge[13]) &&
                clean_ht_data_match(packet + 18u, size - 18u, peer_challenge) :
                detail[2000u] == 27u ?
                clean_ht_rx_metadata(packet[15], packet[16], packet[17], size - 18u,
                    detail[2001u], peer_challenge[13]) &&
                clean_ht_ack_match(packet + 18u, size - 18u, peer_challenge) : 0);
            if (detail[2000u] == CLEAN_PEER_RX_MODE && size >= 18u &&
                clean_ht_rx_metadata(packet[15], packet[16], packet[17], size - 18u,
                                     detail[2001u], peer_challenge[13])) {
                int sequence = clean_peer_mpdu_sequence(packet + 18u, size - 18u, peer_challenge);
                if (sequence >= 0) {
                    uint32_t mask = badger_bytes_load_u32(detail + CLEAN_PEER_RX_DETAIL + 8u);
                    uint32_t bit = 1u << (uint32_t)sequence;
                    badger_bytes_store_u32(detail + CLEAN_PEER_RX_DETAIL, CLEAN_PEER_MAGIC);
                    badger_bytes_store_u32(detail + CLEAN_PEER_RX_DETAIL + 8u, mask | bit);
                    if (mask & bit)
                        badger_bytes_store_u32(detail + CLEAN_PEER_RX_DETAIL + 12u,
                            badger_bytes_load_u32(detail + CLEAN_PEER_RX_DETAIL + 12u) + 1u);
                    peer = 1;
                }
            }
            if (peer && (detail[2000u] == 25u || detail[2000u] == 27u)) {
                badger_bytes_store_u32(detail + 680u, 0x31524448u);
                badger_bytes_store_u32(detail + 684u, size - 18u);
                badger_bytes_store_u32(detail + 688u, peer_challenge[13]);
                badger_bytes_store_u32(detail + 692u, badger_bytes_load_u32(packet + size - 4u));
            }
            if (peer)
                badger_bytes_store_u32(detail + 1996u,
                    badger_bytes_load_u32(detail + 1996u) + 1u);
            if (clean_rx_phase(packet, size) >= 0)
                badger_bytes_store_u32(detail + 2044u, 1u);
            if (after_tx) clean_record_post_tx(detail, packet, size,
                l->wire->now_us(l->wire->context) - started);
            int response = after_tx && size >= 18u &&
                clean_ap_reply(packet + 18u, size - 18u, detail + 1972u,
                               detail + 1980u,
                               (after_tx >= 3u && after_tx <= 6u) ||
                               (after_tx >= 8u && after_tx <= 10u) || after_tx == 15u);
            int ack = after_tx && size >= 18u &&
                clean_ack_reply(packet + 18u, size - 18u, detail + 1972u);
            int candidate = after_tx && size >= 32u &&
                clean_frame_valid(packet + 18u, size - 18u) &&
                badger_bytes_equal(packet + 22u, detail + 1972u, 6u);
            if (candidate) {
                uint32_t candidates = badger_bytes_load_u32(detail + 1964u);
                badger_bytes_store_u32(detail + 1964u, candidates + 1u);
                badger_bytes_store_u32(detail + 1968u,
                    (uint32_t)packet[18] | ((uint32_t)packet[19] << 8u));
            }
            if (packets == 0u || response ||
                (ack && !(after_tx == 15u && badger_bytes_load_u32(detail + 1960u))) || peer ||
                (candidate && !badger_bytes_load_u32(detail + 1960u) &&
                 !badger_bytes_load_u32(detail + 1992u))) {
                badger_bytes_store_u32(detail + 1088u, size);
                uint32_t saved = size < CLEAN_DETAIL_PACKET_BYTES ? size : CLEAN_DETAIL_PACKET_BYTES;
                badger_bytes_zero(detail + 1104u, CLEAN_DETAIL_PACKET_BYTES);
                badger_bytes_copy(detail + 1104u, packet, saved);
            }
            if (response) {
                uint32_t responses = badger_bytes_load_u32(detail + 1960u);
                badger_bytes_store_u32(detail + 1960u, responses + 1u);
            }
            if (ack) {
                uint32_t acks = badger_bytes_load_u32(detail + 1992u);
                badger_bytes_store_u32(detail + 1992u, acks + 1u);
            }
            badger_bytes_store_u32(detail + 1636u, packets + 1u);
            if (!station_mode() || !badger_bytes_load_u32(detail+524u)) beacon(packet, size);
            if (peer && (detail[2000u] == 22u || detail[2000u] == 25u)) return 1;
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
    if (!usb_error && (soak_mode() || detail[2000u] == CLEAN_PEER_RX_MODE)) {
        const wifi_wire_t *wire = wifi_board_wire();
        uint32_t now = wire->now_us(wire->context);
        if (now - soak_watchdog_at >= 4000000u) {
            usb_error = badger_schedule_bootsel(8000u);
            if (!usb_error) soak_watchdog_at = now;
        }
    }
    return usb_error == 0 && !control.reboot_requested && control.configured &&
           control.probe_generation == generation;
}

static int station_command(clean_loader_t *l, uint8_t *bytes, uint32_t size, uint32_t cmd) {
    uint8_t readback[64], staged[64];
    if (!size || size > ((cmd & 255u) == CLEAN_HT_DATA_CMD ?
                        CLEAN_HT_DATA_STORAGE : CLEAN_TX_STORAGE_BYTES)) {
        l->error=CLEAN_INPUT; return 0;
    }
    if ((cmd & 255u) == 5u && !soak_mode() &&
        (clean_tx_length(cmd) > size ||
         !clean_station_tx_prepare(bytes,clean_tx_length(cmd),&station_ap,&cmd))) {
        l->error=CLEAN_INPUT; l->actual=cmd; return 0;
    }
    for (uint32_t at=0u;at<size;at+=64u) {
        uint32_t count=size-at<64u?size-at:64u;
        uint32_t padded=(count+3u)&~3u;
        badger_bytes_zero(staged,padded);
        badger_bytes_copy(staged,bytes+at,count);
        if (!clean_block(l,CLEAN_TX_BUFFER_ADDR+at,staged,padded,1u) ||
            !clean_block(l,CLEAN_TX_BUFFER_ADDR+at,readback,padded,0u)) return 0;
        if (!badger_bytes_equal(staged,readback,padded)) {
            l->error = CLEAN_READBACK;
            l->address = CLEAN_TX_BUFFER_ADDR+at;
            return 0;
        }
    }
    if (!clean_write(l, CLEAN_MBOX + 24u, cmd)) return 0;
    uint32_t started = l->wire->now_us(l->wire->context), command, state;
    do {
        if (!clean_read(l, CLEAN_MBOX + 24u, &command) ||
            !clean_read(l, CLEAN_MBOX + 4u, &state)) return 0;
        if (state != 9u) {
            l->error = CLEAN_CONTROLLER; l->actual = state;
            return 0;
        }
        if (command == 2u) return 1;
        if (l->wire->now_us(l->wire->context) - started >= 100000u) {
            l->error = CLEAN_TIMEOUT; l->address = CLEAN_MBOX + 24u;
            return 0;
        }
    } while (1);
}

static int soak_receive(clean_loader_t *l, clean_soak_status_t *s,
                         const uint8_t nonce[14], uint8_t role) {
    uint32_t size;
    if (!clean_receive(l, rx_packet, &size)) return 0;
    if (size < 18u) return 1;
    uint8_t own[6], peer[6];
    clean_soak_address(own, nonce, role);
    clean_soak_address(peer, nonce, (uint8_t)(3u - role));
    int mac = clean_station_rx(&station_rx, rx_packet, &size, own, peer,
                               l->wire->now_us(l->wire->context));
    if (mac < 0 && mac != CLEAN_MAC_PROTECTED) {
        l->error = CLEAN_INPUT; l->actual = (uint32_t)mac;
        return 0;
    }
    if (clean_soak_receive_packet(s, rx_packet, size, nonce, role) < 0) {
        l->error = CLEAN_INPUT; l->actual = s->failure;
        return 0;
    }
    return 1;
}

static int soak_send(clean_loader_t *l, clean_soak_status_t *s,
                      const uint8_t nonce[14], uint8_t role, uint32_t index) {
    uint32_t length = clean_soak_frame(soak_tx, nonce, role, index);
    if (!length) { l->error = CLEAN_INPUT; l->actual = index; return 0; }
    uint32_t started = l->wire->now_us(l->wire->context), attempts = 0u;
    while (attempts < 8u) {
        uint32_t command = clean_soak_short(nonce) ? CLEAN_TX_CCK_SHORT : 5u;
        if (!station_command(l, soak_tx, length,
            command | clean_tx_length_bits(length) |
            (clean_soak_rate(nonce) == 4u ? 1u << 18u : 0u) |
            (1u << 19u) | CLEAN_TX_ACK_REQUIRED))
            return 0;
        clean_unicast_status_t status;
        if (!clean_block(l, CLEAN_UNICAST_ADDR, (uint8_t *)&status, sizeof(status), 0u)) return 0;
        if (status.magic != CLEAN_UNICAST_MAGIC || status.attempts > 1u ||
            (status.result == CLEAN_UNICAST_ACKED && (!status.attempts || !status.ack_received))) {
            l->error = CLEAN_READBACK; l->actual = status.result; return 0;
        }
        attempts += status.attempts;
        s->tx_attempts += status.attempts;
        if (status.result == CLEAN_UNICAST_ACKED) { ++s->tx_acked; return 1; }
        if (status.result == CLEAN_UNICAST_BUSY) ++s->deferrals;
        else if (status.result == CLEAN_UNICAST_TIMEOUT) ++s->tx_timeouts;
        else { l->error = CLEAN_CONTROLLER; l->actual = status.result; return 0; }
        if (status.attempts && !clean_mac_retry(soak_tx + 6u, length - 6u)) {
            l->error = CLEAN_INPUT; l->actual = index; return 0;
        }
        if (!soak_receive(l, s, nonce, role)) return 0;
        if (l->wire->now_us(l->wire->context) - started >= 2000000u) break;
    }
    s->failure = 3u; l->error = CLEAN_TIMEOUT; l->actual = index;
    return 0;
}

static void mac_soak_run(clean_loader_t *l, const uint8_t nonce[16]) {
    clean_soak_status_t s;
    badger_bytes_zero((uint8_t *)&s, sizeof(s));
    s.magic = CLEAN_SOAK_STATUS_MAGIC;
    s.target = badger_bytes_load_u32(nonce + 8u);
    l->budget_us = CLEAN_SOAK_MAX_US + 5000000u;
    uint8_t role = nonce[14] == 19u ? 1u : 2u;
    uint8_t config[16] = {0};
    clean_soak_address(config, nonce, role);
    clean_soak_address(config + 6u, nonce, (uint8_t)(3u - role));
    config[12] = 0x50u; config[13] = 1u; config[15] = 3u;
    if (clean_soak_phy(nonce)) { config[12] = 15u; config[13] = 0u; }
    uint64_t started = l->elapsed_us;
    if (!s.target || s.target > CLEAN_SOAK_MAX_FRAMES || nonce[12] > CLEAN_TEST_ACK_MAX ||
        nonce[13] >= CLEAN_SOAK_PHYS * CLEAN_SOAK_PAYLOADS) {
        l->error = CLEAN_INPUT;
        l->actual = nonce[13] >= CLEAN_SOAK_PHYS * CLEAN_SOAK_PAYLOADS ? nonce[13] :
            nonce[12] > CLEAN_TEST_ACK_MAX ? nonce[12] : s.target;
    } else if (station_command(l, config, sizeof(config), 7u) &&
               (!nonce[12] || station_command(l, config, sizeof(config),
                   9u | ((uint32_t)nonce[12] << 8u)))) {
        for (uint32_t index = 0u; index < s.target && l->error == CLEAN_OK; ++index) {
            if (!clean_service(l)) break;
            if (l->elapsed_us - started >= CLEAN_SOAK_MAX_US) {
                s.failure = 5u; l->error = CLEAN_TIMEOUT; l->actual = index;
                break;
            }
            if (role == 1u && !soak_send(l, &s, nonce, role, index)) break;
            uint32_t waiting = l->wire->now_us(l->wire->context);
            while (s.received <= index && l->error == CLEAN_OK) {
                if (!soak_receive(l, &s, nonce, role)) break;
                if (l->wire->now_us(l->wire->context) - waiting >= 3000000u) {
                    s.failure = 4u; l->error = CLEAN_TIMEOUT; l->actual = index;
                }
            }
            if (l->error == CLEAN_OK && role == 2u && !soak_send(l, &s, nonce, role, index))
                break;
        }
    }
    uint64_t elapsed = l->elapsed_us - started;
    s.elapsed_us = (uint32_t)elapsed;
    s.elapsed_hi = (uint32_t)(elapsed >> 32u);
    badger_bytes_copy(detail + CLEAN_SOAK_DETAIL, (const uint8_t *)&s, sizeof(s));
}

static int station_receive(clean_loader_t *l, const uint8_t mac[6], uint32_t *size) {
    if (wpa.installed) clean_wpa_rx_expire(&wpa, l->wire->now_us(l->wire->context));
    if (CLEAN_RX_WMM) clean_qos_rx_expire(&station_qos,
                                        l->wire->now_us(l->wire->context));
    if (!clean_receive(l, rx_packet, size)) return 0;
    if (*size >= 58u) {
        clean_station_ap_t next;
        badger_bytes_copy((uint8_t *)&next, (const uint8_t *)&station_ap, sizeof(next));
        int changed = clean_station_refresh(&next, rx_packet + 18u, *size - 18u);
        if (changed < 0) {
            l->error = CLEAN_INPUT; l->actual = (uint32_t)changed;
            return 0;
        }
        if (changed) {
            if (CLEAN_RX_WMM && next.association.wmm) {
                uint8_t previous[CLEAN_WMM_POLICY_BYTES], config[CLEAN_WMM_POLICY_BYTES];
                if (clean_station_wmm_policy(previous, mac, &station_ap) != 1 ||
                    clean_station_wmm_policy(config, mac, &next) != 1) {
                    l->error = CLEAN_INPUT; l->actual = CLEAN_WMM_POLICY_CMD;
                    return 0;
                }
                if (!badger_bytes_equal(previous, config, sizeof(config))) {
                    if (!station_command(l, config, sizeof(config), CLEAN_WMM_POLICY_CMD))
                        return 0;
                    if (station_wmm_updates != 0xffffffffu) ++station_wmm_updates;
                }
            } else if (next.basic_rates != station_ap.basic_rates ||
                ((next.capability ^ station_ap.capability) & 1024u)) {
                uint8_t config[16];
                badger_bytes_copy(config, mac, 6u);
                badger_bytes_copy(config + 6u, next.bssid, 6u);
                config[12] = (uint8_t)next.basic_rates;
                config[13] = (uint8_t)(next.basic_rates >> 8u);
                config[14] = (uint8_t)!!(next.capability & 1024u);
                config[15] = 3u;
                if (!station_command(l, config, sizeof(config), 8u)) return 0;
            }
            badger_bytes_copy((uint8_t *)&station_ap, (const uint8_t *)&next, sizeof(next));
        }
    }
    int result = clean_station_qos_rx(&station_rx,
        CLEAN_RX_WMM && station_ap.association.wmm ? &station_qos : 0,
        rx_packet, size, mac, station_ap.bssid, l->wire->now_us(l->wire->context));
    if (result == CLEAN_MAC_UNSUPPORTED) {
        l->error = CLEAN_INPUT;
        l->actual = (uint32_t)result;
        return 0;
    }
    return 1;
}

static int station_wait(clean_loader_t *l, const uint8_t mac[6], uint8_t association,
                         uint16_t *aid, uint32_t duration) {
    uint32_t started = l->wire->now_us(l->wire->context);
    while (l->wire->now_us(l->wire->context) - started < duration) {
        uint8_t *packet=rx_packet;
        uint32_t size;
        if (!station_receive(l, mac, &size)) return -1;
        if (size < 18u) continue;
        clean_record_post_tx(detail, packet, size,
            l->wire->now_us(l->wire->context) - started);
        const uint8_t *m = packet + 18u;
        uint32_t n = size - 18u;
        if (n >= 24u && badger_bytes_equal(m + 4u, mac, 6u)) {
            uint32_t candidates = badger_bytes_load_u32(detail + 1096u);
            badger_bytes_store_u32(detail + 1040u + 4u * (candidates % 12u),
                ((uint32_t)clean_frame_valid(m, n) << 31u) | (n << 8u) | m[0]);
            badger_bytes_store_u32(detail + 1096u, candidates + 1u);
            if (association && m[0] == 0x10u) {
                uint32_t saved = n < 192u ? n : 192u;
                badger_bytes_store_u32(detail + 796u, saved);
                badger_bytes_copy(detail + 800u, m, saved);
            }
        }
        if (n >= 14u && clean_frame_valid(m, n) &&
            badger_bytes_equal(m + 4u, mac, 6u)) {
            badger_bytes_store_u32(detail + 1088u, size);
            badger_bytes_copy(detail + 1104u, packet,
                size < CLEAN_DETAIL_PACKET_BYTES ? size : CLEAN_DETAIL_PACKET_BYTES);
        }
        int reason = clean_station_departure(m, n, mac, station_ap.bssid);
        if (reason >= 0) {
            clean_station_forget_association(&station_ap);
            badger_bytes_store_u32(detail + 568u, (uint32_t)reason);
            return -2;
        }
        clean_station_association_t profile;
        int status = clean_station_response_profile(m, n, mac, station_ap.bssid,
                                                     association, aid, &profile);
        if (status < 0) continue;
        if (association && !status) {
            int wmm = clean_station_wmm_accept(&profile, station_wmm_offered);
            if (wmm < 0) {
                l->error = CLEAN_INPUT; l->actual = CLEAN_WMM_POLICY_CMD;
                return -1;
            }
            profile.wmm = (uint8_t)wmm;
            badger_bytes_copy((uint8_t *)&station_ap.association,
                              (const uint8_t *)&profile, sizeof(profile));
            station_ap.ht_updates = 0u;
        }
        uint32_t at = association ? 800u : 600u;
        uint32_t saved = n < 192u ? n : 192u;
        badger_bytes_store_u32(detail + at - 4u, saved);
        badger_bytes_copy(detail + at, m, saved);
        return status;
    }
    return -1;
}

static void station_wpa(clean_loader_t *l, const uint8_t mac[6]) {
    clean_wpa_init(&wpa,wpa_secret,mac,station_ap.bssid,station_ap.rsn,station_ap.rsn_length);
    wpa.tx_qos = station_ap.association.wmm;
    wpa.rx_amsdu = (uint8_t)(CLEAN_RX_HT_RX && station_ap.association.wmm);
    wpa_wipe(wpa_secret,sizeof(wpa_secret));
    uint32_t started=l->wire->now_us(l->wire->context), last_probe=started;
    uint8_t *packet=rx_packet;
    uint8_t tx[512];
    uint32_t pending = 0u;
    uint8_t pending_key = 0u;
    while (!l->error && !wpa.error && l->wire->now_us(l->wire->context)-started<3000000u) {
        uint32_t size;
        if (!station_receive(l,mac,&size)) break;
        if (size>=18u) {
            const uint8_t *m=packet+18u;
            if (size<=CLEAN_DETAIL_PACKET_BYTES && size>=42u && !(m[1]&64u) &&
                wpa_equal(m+4u,mac,6u) && clean_frame_valid(m,size-18u)) {
                badger_bytes_store_u32(detail+1088u,size);
                badger_bytes_copy(detail+1104u,packet,size);
            }
            int reason=clean_station_departure(m,size-18u,mac,station_ap.bssid);
            if (reason>=0) {
                clean_station_forget_association(&station_ap);
                badger_bytes_store_u32(detail+568u,(uint32_t)reason);
                badger_bytes_store_u32(detail+524u,5u);
                wpa.error=WPA_FORMAT; break;
            }
            uint32_t length;
            int result=clean_wpa_receive(&wpa,m,size-18u,tx,&length);
            if (result<0) break;
            if (result) { pending=length; pending_key=1u; }
            else if (wpa.installed && clean_wpa_protected(&wpa,m,size-18u,
                l->wire->now_us(l->wire->context))>0) break;
        }
        if (pending) {
            if (!station_command(l,tx,pending,5u|clean_tx_length_bits(pending)|
                (1u<<18u)|(4u<<19u)|CLEAN_TX_ACK_REQUIRED)) break;
            uint8_t status[sizeof(clean_unicast_status_t)];
            if (!clean_block(l,CLEAN_UNICAST_ADDR,status,sizeof(status),0u)) break;
            if (badger_bytes_load_u32(status)!=CLEAN_UNICAST_MAGIC) {
                l->error=CLEAN_READBACK; l->address=CLEAN_UNICAST_ADDR; break;
            }
            if (badger_bytes_load_u32(status+8u)) {
                if (pending_key) {
                    if (wpa.installed) ++wpa.m4; else ++wpa.m2;
                    if (wpa.stage==WPA_KEYS) wpa.stage=WPA_M4_SENT;
                }
                pending=0u;
            }
        }
        uint32_t tick=l->wire->now_us(l->wire->context);
        if (!pending && wpa.stage>=WPA_M4_SENT && tick-last_probe>=100000u) {
            uint32_t length=clean_wpa_probe(&wpa,tx);
            if (!length) break;
            pending=length;
            pending_key=0u;
            last_probe=tick;
        }
    }
    if (!wpa.error && wpa.stage!=WPA_PROTECTED) wpa.error=l->error?WPA_TRANSPORT:WPA_TIMEOUT;
    badger_bytes_store_u32(detail+1640u,0x32575043u);
    badger_bytes_store_u32(detail+1644u,wpa.stage);
    badger_bytes_store_u32(detail+1648u,wpa.error);
    badger_bytes_store_u32(detail+1652u,wpa.m1);
    badger_bytes_store_u32(detail+1656u,wpa.m2);
    badger_bytes_store_u32(detail+1660u,wpa.m3);
    badger_bytes_store_u32(detail+1664u,wpa.m4);
    badger_bytes_store_u32(detail+1668u,wpa.protected_rx);
    badger_bytes_store_u32(detail+1672u,wpa.gtk_info);
    badger_bytes_store_u32(detail+CLEAN_WMM_DETAIL+16u,wpa.amsdu_rx);
    badger_bytes_store_u32(detail+CLEAN_WMM_DETAIL+20u,wpa.amsdu_subframes);
    clean_station_qos_report_at(&wpa.rx_mac,&wpa.rx_qos,detail,CLEAN_STATION_PROTECTED_DETAIL);
    wpa_wipe(&wpa,sizeof(wpa));
}

static void station_run(clean_loader_t *l, const uint8_t nonce[16]) {
    if (nonce[14]==18u) {
        badger_bytes_zero(detail+1640u,36u);
        badger_bytes_store_u32(detail+1640u,0x32575043u);
        badger_bytes_store_u32(detail+1648u,WPA_ASSOCIATION);
    }
    badger_bytes_store_u32(detail + 520u, 0x31545341u);
    badger_bytes_store_u32(detail + 528u, 0xffffffffu);
    badger_bytes_store_u32(detail + 532u, 0xffffffffu);
    badger_bytes_store_u32(detail + 572u, (uint32_t)station_ap_status);
    if (station_ap_status <= 0) {
        l->error = CLEAN_INPUT; l->actual = (uint32_t)station_ap_status;
        return;
    }
    uint8_t mac[6] = {2u, nonce[6], nonce[7], nonce[8], nonce[9], nonce[10]};
    badger_bytes_copy(detail + 1972u, mac, 6u);
    badger_bytes_copy(detail + 1980u, station_ap.bssid, 6u);
    uint8_t config[16] = {0};
    badger_bytes_copy(config, mac, 6u);
    badger_bytes_copy(config + 6u, station_ap.bssid, 6u);
    config[12] = (uint8_t)station_ap.basic_rates;
    config[13] = (uint8_t)(station_ap.basic_rates >> 8u);
    config[14] = (uint8_t)!!(station_ap.capability & 1024u);
    config[15] = 3u;
    if (!station_command(l, config, sizeof(config), 7u)) return;
    badger_bytes_store_u32(detail + 524u, 1u);
    uint16_t aid = 0u;
    clean_station_request_t request = {.sequence = clean_station_u16(nonce + 11u)};
    for (uint8_t association = 0u; association < 2u; ++association) {
        if (association && !receive_for(l,10000u,0u)) return;
        if (association) {
            station_wmm_offered = (uint8_t)clean_station_wmm_offer(&station_ap);
            if (station_wmm_offered) {
                uint8_t unused[4] = {0};
                if (!station_command(l, unused, sizeof(unused),
                                     CLEAN_HT_RESPONSE_CMD | (1u << 8u))) return;
            }
        }
        uint8_t frame[160];
        request.pending = 0u;
        badger_bytes_store_u32(detail + 1092u, 1u);
        int status = -1;
        uint32_t sent = 0u;
        uint32_t phase_started = l->wire->now_us(l->wire->context);
        while (sent < 4u &&
               l->wire->now_us(l->wire->context) - phase_started < 1600000u) {
            uint32_t length = clean_station_request_prepare(&request, frame, mac,
                                                            &station_ap, association);
            if (!length) {
                l->error = CLEAN_INPUT; l->actual = request.length; return;
            }
            if (!station_command(l, frame, sizeof(frame), 5u | clean_tx_length_bits(length) |
                (1u << 18u) | (1u << 19u) | CLEAN_TX_ACK_REQUIRED)) return;
            uint8_t status_bytes[sizeof(clean_unicast_status_t)];
            if (!clean_block(l, CLEAN_UNICAST_ADDR, status_bytes, sizeof(status_bytes), 0u)) return;
            if (badger_bytes_load_u32(status_bytes) != CLEAN_UNICAST_MAGIC) {
                l->error = CLEAN_READBACK; l->address = CLEAN_UNICAST_ADDR;
                return;
            }
            uint32_t at = 540u + 4u * association;
            uint32_t attempts = badger_bytes_load_u32(status_bytes + 8u);
            if (!attempts && badger_bytes_load_u32(status_bytes + 4u) != CLEAN_UNICAST_BUSY) {
                l->error = CLEAN_CONTROLLER;
                l->actual = badger_bytes_load_u32(status_bytes + 4u);
                return;
            }
            sent += attempts;
            badger_bytes_store_u32(detail + at, badger_bytes_load_u32(detail + at) +
                                  attempts);
            uint32_t ack = badger_bytes_load_u32(status_bytes + 12u);
            clean_station_request_sent(&request, attempts, ack);
            badger_bytes_store_u32(detail + 548u, badger_bytes_load_u32(detail + 548u) + ack);
            /* A held capture or busy medium must not spend a radio retry.
             * Drain RX on a short deadline before trying the same MPDU again. */
            uint32_t wait = !attempts ? 10000u : ack ? 400000u : 100000u;
            uint32_t elapsed = l->wire->now_us(l->wire->context) - phase_started;
            if (elapsed >= 1600000u) break;
            if (wait > 1600000u - elapsed) wait = 1600000u - elapsed;
            status = station_wait(l, mac, association, &aid, wait);
            if (status != -1 || l->error) break;
        }
        badger_bytes_store_u32(detail + 528u + 4u*association, (uint32_t)status);
        badger_bytes_store_u32(detail + 524u, association ? 3u : 2u);
        if (status != 0 || l->error) return;
    }
    if (station_ap.association.wmm) {
        uint8_t policy[CLEAN_WMM_POLICY_BYTES];
        if (clean_station_wmm_policy(policy, mac, &station_ap) != 1) {
            l->error = CLEAN_INPUT; l->actual = CLEAN_WMM_POLICY_CMD;
            return;
        }
        if (!station_command(l, policy, sizeof(policy), CLEAN_WMM_POLICY_CMD)) return;
        ++station_wmm_updates;
    }
    badger_bytes_store_u32(detail + 536u, aid);
    badger_bytes_store_u32(detail + 524u, 4u);
    if (nonce[14]==18u) station_wpa(l,mac);
    else (void)receive_for(l, 2500000u, 0u);
}

static void run(const uint8_t nonce[16]) {
    clean_loader_t l;
    wifi_board_init();
    wifi_board_select_pio(1u);
    clean_loader_init(&l, wifi_board_wire(), service, 0);
    soak_watchdog_at = l.wire->now_us(l.wire->context);
    if (badger_schedule_bootsel(8000u) != 0) l.error = CLEAN_CANCELLED;
    badger_bytes_zero(detail, sizeof(detail));
    clean_mac_rx_init(&station_rx);
    if (CLEAN_RX_WMM) clean_qos_rx_init(&station_qos);
    station_wmm_updates = station_wmm_offered = 0u;
    int secret_valid=1;
    if (nonce[14]==18u) {
        uint8_t digest[20];
        badger_bytes_copy(wpa_secret,control.clean_secret,64u);
        wpa_secret_binding(wpa_secret,nonce,nonce[15],digest);
        secret_valid=control.clean_secret_ready && wpa_equal(digest,nonce+6u,8u);
        wpa_wipe(digest,sizeof(digest));
    }
    wpa_wipe(control.clean_secret,64u); control.clean_secret_ready=0u;
    uint32_t length = (uint32_t)(clean_image_end - clean_image);
    uint32_t channel = nonce[15];
    if ((nonce[14] > CLEAN_PEER_RX_MODE && !(CLEAN_RX_AMPDU_PROBE &&
            nonce[14] <= CLEAN_HT_AMPDU_PROBE_RX_MODE)) || !secret_valid ||
        ((nonce[14] == 21u || nonce[14] == 22u || nonce[14] >= 24u) && nonce[13] >= 8u) ||
        (nonce[14] >= 26u && nonce[14] <= 28u && (!CLEAN_RX_HT_RESPONSE ||
                             clean_ht_data_length(nonce) != CLEAN_HT_ACK_MPDU)) ||
        (nonce[14] >= 24u && nonce[14] <= 28u &&
         !clean_ht_data_size_valid(clean_ht_data_length(nonce))))
        l.error = CLEAN_INPUT;
    detail[2000u] = nonce[14];
    detail[2001u] = nonce[15];
    badger_bytes_copy(peer_challenge, nonce, sizeof(peer_challenge));
    badger_bytes_copy(station_target, nonce, 6u);
    station_ap_status = 0;
    badger_bytes_zero((uint8_t *)&station_ap, sizeof(station_ap));
    badger_bytes_store_u32(detail + 2004u, nonce[14] == 8u || nonce[14] == 10u);
    badger_bytes_store_u32(detail + 2008u, nonce[14] == 9u || nonce[14] == 10u);
    if (l.error == CLEAN_OK &&
        clean_boot(&l, clean_image, length, channel, badger_bytes_load_u32(nonce) | 1u,
                   nonce[14] != 0u && nonce[14] != 2u && nonce[14] != 13u &&
                   nonce[14] != 17u &&
                   nonce[14] != 22u && nonce[14] != 23u &&
                   nonce[14] != 25u && nonce[14] != CLEAN_PEER_RX_MODE &&
                   nonce[14] != CLEAN_HT_AMPDU_PROBE_RX_MODE) &&
        clean_write(&l, 0x18002024u, 0xf0u) &&
        clean_write(&l, CLEAN_MBOX + 24u, 2u) && clean_wait_state(&l, 9u, 100000u)) {
        l.stage = 7u;
        if (nonce[14] == 26u || nonce[14] == 28u) {
            for (uint32_t i = 0u; i < 6u; ++i)
                detail[1972u+i] = clean_ht_ack_byte(10u+i, nonce);
        }
        if (nonce[14] == 27u || nonce[14] == 28u) {
            uint8_t config[16];
            for (uint32_t i = 0u; i < 6u; ++i) {
                config[i] = clean_ht_ack_byte((nonce[14] == 28u ? 10u : 4u)+i, nonce);
                config[6u+i] = clean_ht_ack_byte((nonce[14] == 28u ? 4u : 10u)+i, nonce);
            }
            config[12] = 0x50u; config[13] = 1u;
            config[14] = 1u; config[15] = 3u;
            if (station_command(&l, config, sizeof(config), 7u) &&
                station_command(&l, config, sizeof(config), CLEAN_HT_RESPONSE_CMD | (3u << 8u)))
                badger_bytes_store_u32(detail + 704u, 1u);
            if (CLEAN_RX_EDCA && nonce[14] == 28u && l.error == CLEAN_OK) {
                uint8_t parameters[16] = {
                    3u,0xa4u,0u,0u, 0x27u,0xa4u,0u,0u,
                    0x42u,0x43u,94u,0u, 0x62u,0x32u,47u,0u
                };
                (void)station_command(&l, parameters, sizeof(parameters), CLEAN_EDCA_CMD);
            }
        }
        if (soak_mode()) mac_soak_run(&l, nonce);
        else (void)receive_for(&l, nonce[14] == CLEAN_PEER_RX_MODE ||
                              nonce[14] == CLEAN_HT_AMPDU_PROBE_RX_MODE ? 4200000u :
                              nonce[14] == 13u || nonce[14] == 17u ||
                              nonce[14] == 22u || nonce[14] == 23u ||
                              nonce[14] == 25u || nonce[14] == 27u ? 2500000u : 700000u, 0u);
        if (l.error == CLEAN_OK && (nonce[14] == 24u || nonce[14] == 26u || nonce[14] == 28u ||
                                   nonce[14] == CLEAN_HT_AMPDU_PROBE_TX_MODE)) {
            uint32_t total = nonce[14] == CLEAN_HT_AMPDU_PROBE_TX_MODE ?
                CLEAN_HT_AMPDU_PROBE_TX_BYTES : 6u + clean_ht_data_length(nonce);
            uint32_t started = l.wire->now_us(l.wire->context);
            do {
                if (!(nonce[14] == CLEAN_HT_AMPDU_PROBE_TX_MODE ?
                      clean_ht_ampdu_probe_frame(rx_packet, nonce) :
                      nonce[14] == 28u ? clean_ht_clocked_frame(rx_packet, nonce) :
                      nonce[14] == 26u ? clean_ht_ack_frame(rx_packet, nonce) :
                                        clean_ht_data_frame(rx_packet, nonce))) {
                    l.error = CLEAN_INPUT;
                    break;
                }
                badger_bytes_store_u32(detail + 1988u, total);
                badger_bytes_store_u32(detail + 1092u, 1u);
                if (!station_command(&l, rx_packet, (CLEAN_HT_DATA_HEADER + total + 3u) & ~3u,
                        CLEAN_HT_DATA_CMD | clean_tx_length_bits(total) | (1u << 19u))) break;
                if (nonce[14] == 28u) {
                    clean_unicast_status_t status;
                    if (!clean_block(&l, CLEAN_UNICAST_ADDR, (uint8_t *)&status,
                                     sizeof(status), 0u)) break;
                    if (status.magic != CLEAN_UNICAST_MAGIC || status.attempts > 1u ||
                        (status.result != CLEAN_UNICAST_BUSY &&
                         status.result != CLEAN_UNICAST_ACKED &&
                         status.result != CLEAN_UNICAST_TIMEOUT) ||
                        (status.result == CLEAN_UNICAST_BUSY && status.attempts) ||
                        (status.result != CLEAN_UNICAST_BUSY && !status.attempts)) {
                        l.error = CLEAN_INPUT; l.actual = status.result;
                        break;
                    }
                    if (status.result == CLEAN_UNICAST_BUSY) {
                        if (l.wire->now_us(l.wire->context) - started >= 200000u) {
                            l.error = CLEAN_TIMEOUT;
                            break;
                        }
                        if (!receive_for(&l, 2000u, 0u)) break;
                        continue;
                    }
                }
                badger_bytes_store_u32(detail + 1096u, 1u);
                (void)receive_for(&l, 200000u, nonce[14] == 26u ? 26u : 0u);
                break;
            } while (l.error == CLEAN_OK);
        }
        if (l.error == CLEAN_OK && station_mode()) station_run(&l, nonce);
        if (l.error == CLEAN_OK && !soak_mode() && nonce[14] != 0u && nonce[14] != 13u &&
            nonce[14] != 16u && nonce[14] != 17u && nonce[14]!=18u &&
            nonce[14]!=22u && nonce[14]!=23u && nonce[14]<24u) {
            uint8_t frame[96];
            uint32_t tx_length = 94u;
            const uint8_t *selected = detail + 1640u;
            if (nonce[14] == 15u) {
                int index = clean_beacon_find(detail + 1640u,
                    badger_bytes_load_u32(detail + 1632u), nonce);
                if (index < 0) {
                    l.error = CLEAN_INPUT;
                    l.actual = 15u;
                } else selected += 40u * (uint32_t)index;
            }
            uint8_t ssid_length = selected[6];
            if (nonce[14] < 12u && (badger_bytes_load_u32(detail + 1632u) == 0u ||
                !clean_probe_request(frame, (uint16_t)badger_bytes_load_u32(nonce),
                                     selected + 8u, ssid_length))) {
                l.error = CLEAN_INPUT;
            }
            /* Fresh local source MAC distinguishes an AP response from old traffic. */
            if (l.error == CLEAN_OK) {
                badger_bytes_copy(detail + 1980u, selected, 6u);
                if ((nonce[14] >= 3u && nonce[14] <= 6u) ||
                    (nonce[14] >= 8u && nonce[14] <= 10u) || nonce[14] == 15u)
                    tx_length = clean_auth_request(frame, (uint16_t)badger_bytes_load_u32(nonce),
                                                   detail + 1980u, nonce[14] == 6u || nonce[14] >= 8u);
                static const uint8_t broadcast[6] = {255u,255u,255u,255u,255u,255u};
                if (nonce[14] == 7u || nonce[14] == 11u)
                    tx_length = clean_compact_probe_request(frame,
                        (uint16_t)badger_bytes_load_u32(nonce), detail + 1648u,
                        ssid_length, nonce[14] == 11u ? broadcast : detail + 1980u);
                if (nonce[14] == 4u) clean_ofdm_plcp(frame);
                if (nonce[14] == 15u &&
                    !clean_ofdm_plcp_rate(frame, tx_length - CLEAN_PLCP_BYTES, 0u))
                    l.error = CLEAN_INPUT;
                if (nonce[14] == 5u) clean_cck_plcp(frame, 0x0au, 704u);
                if (nonce[14] == 12u || nonce[14] == 14u)
                    clean_peer_request(frame, peer_challenge, nonce[14] == 14u);
                if (nonce[14] == 21u && !clean_ht_probe_frame(frame, peer_challenge))
                    l.error = CLEAN_INPUT;
                if (tx_length == 0u) l.error = CLEAN_INPUT;
                else {
                    frame[16] = 0x02u;
                    badger_bytes_copy(frame + 17u, nonce + (nonce[14] == 15u ? 6u : 0u), 5u);
                    badger_bytes_copy(detail + 1972u, frame + 16u, 6u);
                    badger_bytes_store_u32(frame + tx_length - 4u,
                                          diag_crc32(frame + 6u, tx_length - 10u));
                    badger_bytes_store_u32(detail + 1092u, 1u);
                }
            }
            badger_bytes_store_u32(detail + 1988u, tx_length);
            uint8_t staged[96];
            if (l.error == CLEAN_OK && clean_block(&l, CLEAN_TX_BUFFER_ADDR, frame, sizeof(frame), 1u) &&
                clean_block(&l, CLEAN_TX_BUFFER_ADDR, staged, sizeof(staged), 0u) &&
                !badger_bytes_equal(frame, staged, sizeof(frame))) {
                l.error = CLEAN_READBACK;
                l.address = CLEAN_TX_BUFFER_ADDR;
            }
            uint32_t phy_debug;
            if (l.error == CLEAN_OK && clean_read(&l, 0x18001158u, &phy_debug))
                badger_bytes_store_u32(detail + 2012u, phy_debug);
            badger_bytes_store_u32(detail + 2016u, badger_bytes_load_u32(detail + 1636u));
            if (l.error == CLEAN_OK &&
                clean_write(&l, CLEAN_MBOX + 24u,
                            (nonce[14] == 21u ? CLEAN_HT_PROBE_CMD : 5u) |
                            (tx_length << 8u) | (1u << 19u) |
                            (nonce[14] == 4u || nonce[14] == 15u ? 1u << 18u : 0u) |
                            (nonce[14] == 8u || nonce[14] == 10u ? CLEAN_TX_REARM_RX : 0u) |
                            (nonce[14] == 9u || nonce[14] == 10u ? CLEAN_TX_MATCH_SOURCE : 0u))) {
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
                if (l.error == CLEAN_OK) (void)receive_for(&l, 200000u, nonce[14]);
            }
        }
        if (l.error == CLEAN_OK && clean_write(&l, CLEAN_MBOX + 24u, 3u) &&
            clean_wait_state(&l, 10u, 200000u)) l.stage = 8u;
    }
    if (station_mode()) clean_station_ht_report(&station_ap, nonce[15], detail);
    uint32_t saved_error = l.error, saved_addr = l.address, saved_actual = l.actual;
    /* Collect fault evidence even after a failed stage, still under a deadline. */
    l.error = CLEAN_OK;
    int snapshot = 1;
    if ((soak_mode() || nonce[14] == 28u) && saved_error != CLEAN_OK) {
        uint32_t state;
        snapshot = clean_read(&l, CLEAN_MBOX + 4u, &state);
        if (snapshot && state != 255u)
            snapshot = clean_write(&l, CLEAN_MBOX + 24u, 3u) &&
                       clean_wait_state(&l, 10u, 200000u);
    }
    snapshot = snapshot && clean_block(&l, CLEAN_MBOX, detail + 32u, 448u, 0u) &&
                   clean_block(&l, CLEAN_MULTI, detail + 480u, 40u, 0u);
    if (snapshot && !station_mode() && !soak_mode() && nonce[14] != 17u && !ht_probe_mode())
        snapshot = clean_block(&l, CLEAN_MULTI + 40u, detail + 520u, 568u, 0u);
    if (snapshot && ht_probe_mode())
        snapshot = clean_block(&l, CLEAN_HT_RX_ADDR, detail + CLEAN_HT_DETAIL,
                               sizeof(clean_ht_rx_t), 0u) &&
                   clean_block(&l, CLEAN_HT_DIAG_ADDR, detail + CLEAN_HT_DIAG_DETAIL,
                               sizeof(clean_ht_diag_t), 0u);
    if (snapshot && (nonce[14] == CLEAN_HT_AMPDU_PROBE_TX_MODE ||
                     nonce[14] == CLEAN_HT_AMPDU_PROBE_RX_MODE)) {
        uint32_t chip_id;
        snapshot = clean_block(&l, CLEAN_AMPDU_PROBE_ADDR, detail + CLEAN_AMPDU_PROBE_DETAIL,
                               sizeof(clean_ampdu_probe_trace_t), 0u) &&
                   clean_read(&l, 0x18000000u, &chip_id);
        if (snapshot) badger_bytes_store_u32(detail + 680u, chip_id);
#if CLEAN_RX_AMPDU_INTERFACE
        if (snapshot) snapshot = clean_block(&l, CLEAN_AMPDU_IF_ADDR,
            detail + CLEAN_AMPDU_IF_DETAIL, sizeof(clean_ampdu_interface_t), 0u);
#endif
    }
    if (snapshot && (nonce[14] == 27u || nonce[14] == 28u))
        snapshot = clean_block(&l, CLEAN_HT_FAILURE_ADDR, detail + CLEAN_HT_FAILURE_DETAIL,
                               sizeof(clean_ht_failure_t), 0u);
    if (snapshot && nonce[14] == 27u)
        snapshot = clean_block(&l, CLEAN_UNICAST_ADDR + 16u, detail + 696u, 8u, 0u) &&
            clean_block(&l, CLEAN_MAC_TRACE_ADDR +
                        __builtin_offsetof(clean_mac_trace_status_t, response_delay),
                        detail + 708u, 4u, 0u);
    if (snapshot && nonce[14] == 28u)
        snapshot = clean_block(&l, CLEAN_ACK_RX_ADDR, detail + CLEAN_ACK_RX_DETAIL,
                               sizeof(clean_ack_rx_t), 0u) &&
            clean_block(&l, CLEAN_UNICAST_ADDR, detail + 1000u,
                        sizeof(clean_unicast_status_t), 0u) &&
            clean_block(&l, CLEAN_MAC_RADIO_ADDR, detail + 800u,
                        sizeof(clean_mac_radio_status_t), 0u) &&
            clean_block(&l, CLEAN_MAC_TRACE_ADDR, detail + 864u,
                        sizeof(clean_mac_trace_status_t), 0u) &&
            clean_block(&l, CLEAN_DCF_TRACE_ADDR, detail + CLEAN_DCF_TRACE_DETAIL,
                        sizeof(clean_dcf_trace_t), 0u);
    if (snapshot && nonce[14] == 28u && CLEAN_RX_EDCA)
        snapshot = clean_block(&l, CLEAN_EDCA_STATUS_ADDR, detail + CLEAN_EDCA_DETAIL,
                               sizeof(clean_edca_status_t), 0u);
    if (snapshot && (station_mode() || soak_mode())) {
        uint8_t status[sizeof(clean_unicast_status_t)];
        snapshot = clean_block(&l, CLEAN_UNICAST_ADDR, status, sizeof(status), 0u);
        if (snapshot) {
            if (station_mode()) badger_bytes_copy(detail + 552u, status + 16u, 8u);
            badger_bytes_copy(detail + 1000u, status, sizeof(status));
            snapshot = clean_block(&l, CLEAN_MAC_RADIO_ADDR,
                detail + CLEAN_STATION_MAC_DETAIL + 32u, sizeof(clean_mac_radio_status_t), 0u) &&
                clean_block(&l, CLEAN_MAC_TRACE_ADDR, detail + CLEAN_STATION_TRACE_DETAIL,
                            sizeof(clean_mac_trace_status_t), 0u);
            if (snapshot && soak_mode())
                snapshot = clean_block(&l, CLEAN_MBOX + 456u,
                    detail + CLEAN_SOAK_FAULT_DETAIL, 2u * CLEAN_RX_FAULT_WORDS, 0u) &&
                    clean_block(&l, CLEAN_RX_TRACE_ADDR, detail + 200u,
                                2u * CLEAN_RX_TRACE_WORDS + 24u, 0u);
        }
    }
    if (snapshot && station_mode() && !badger_bytes_load_u32(detail + 596u))
        snapshot = clean_block(&l, CLEAN_AUTH_FAILURE_ADDR, detail + CLEAN_AUTH_FAILURE_DETAIL,
                               sizeof(clean_auth_failure_t), 0u);
    if (station_mode() || soak_mode())
        clean_station_qos_report_at(&station_rx, CLEAN_RX_WMM ? &station_qos : 0,
                                   detail, CLEAN_STATION_MAC_DETAIL);
    if (station_mode()) {
        badger_bytes_store_u32(detail + CLEAN_WMM_DETAIL, CLEAN_WMM_MAGIC);
        badger_bytes_store_u32(detail + CLEAN_WMM_DETAIL + 4u, station_wmm_offered);
        badger_bytes_store_u32(detail + CLEAN_WMM_DETAIL + 8u, station_ap.association.wmm);
        badger_bytes_store_u32(detail + CLEAN_WMM_DETAIL + 12u, station_wmm_updates);
        if (snapshot && CLEAN_RX_WMM)
            snapshot = clean_block(&l, CLEAN_EDCA_STATUS_ADDR, detail + CLEAN_EDCA_DETAIL,
                                   sizeof(clean_edca_status_t), 0u);
    }
    if (saved_error != CLEAN_OK) {
        l.error = saved_error;
        l.address = saved_addr;
        l.actual = saved_actual;
    } else if (!snapshot && l.error == CLEAN_OK) l.error = CLEAN_READBACK;
    uint32_t elapsed = l.wire->now_us(l.wire->context) - l.started;
    wifi_wire_power_off(l.wire);
    wpa_wipe(wpa_secret,sizeof(wpa_secret)); wpa_wipe(&wpa,sizeof(wpa));
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
    screen(l.error ? "RADIO TEST FAILED" :
           ht_probe_mode() ? (nonce[14] == 21u || nonce[14] == 24u ? "HT / TX SUBMITTED ONLY" :
                              "HT PROBE / CHECK RAW EVIDENCE") :
           nonce[14] == 18u ?
           (badger_bytes_load_u32(detail+1644u)==WPA_PROTECTED ?
            "WPA2 / PROTECTED ECHO VERIFIED" : "WPA2 HANDSHAKE / DATA FAILED") :
           nonce[14] == 16u ?
           (clean_station_evidence(detail) ?
            "ASSOCIATED / WPA2 NOT AUTHORIZED" : "AP ASSOCIATION NOT CONFIRMED") :
           nonce[14] == 15u ?
           (clean_auth_response_status(detail) == 0 ? "AP AUTH OK / NOT CONNECTED" :
            clean_auth_response_status(detail) > 0 ? "AP REJECTED AUTHENTICATION" :
            "NO AP AUTHENTICATION REPLY") :
           clean_peer_received(detail, peer_challenge) ? "FRESH PEER FRAME RECEIVED" :
           clean_tx_confirmed(detail) ?
           "FRESH REPLY / TX CONFIRMED" :
           nonce[14] ? "TX SUBMITTED / NO AP RESPONSE" : "COLD RX COMPLETE",
           badger_bytes_load_u32(detail + 1636u), l.error);
}

void bare_main(void) {
    int32_t recovery = badger_schedule_bootsel(8000u);
    badger_display_init();
    screen("WAITING FOR USB TEST REQUEST", 0u, 0u);
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

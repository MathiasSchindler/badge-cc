#include "loader.h"
#include "build_id.h"
#include "frames.h"
#include "station.h"
#include "wpa2.h"
#include "../unicast.h"
#include "../tx_diag.h"
#include "../../src/shared/bytes.h"
#include "../../src/usb/live_usb_hw.h"
#include "../../src/arch/rp2350/bootrom_reboot.h"
#include "../../src/platform/badger/wifi_board.h"
#include "../../src/tools/diagnostics_logic.h"
#include "../../src/radio/wifi_usb_report.h"
#include "../../src/platform/badger/display.h"
#include "../../src/platform/badger/graphics.h"

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
static uint8_t wpa_secret[64];
/* RX consumers run synchronously; share a packet outside the 8 KiB stack. */
static uint8_t rx_packet[CLEAN_RX_PACKET_BYTES];

static int station_mode(void) { return detail[2000u]==16u || detail[2000u]==18u; }

static void monitor_packet(const uint8_t *packet, uint32_t size) {
    if (size < 32u || !clean_frame_valid(packet + 18u, size - 18u)) return;
    const uint8_t *m = packet + 18u;
    uint32_t n = size - 18u;
    if (n >= 34u && m[0] == 0xb0u && clean_station_u16(m + 26u) == 1u &&
        badger_bytes_equal(m + 4u, station_target, 6u) &&
        badger_bytes_equal(m + 16u, station_target, 6u)) {
        badger_bytes_copy(detail + 1972u, m + 10u, 6u);
        badger_bytes_store_u32(detail + 540u, badger_bytes_load_u32(detail + 540u) + 1u);
        badger_bytes_store_u32(detail + 596u, n < 192u ? n : 192u);
        badger_bytes_copy(detail + 600u, m, n < 192u ? n : 192u);
    } else if (n >= 28u && m[0] == 0u &&
        badger_bytes_equal(m + 4u, station_target, 6u) &&
        badger_bytes_equal(m + 10u, detail + 1972u, 6u)) {
        badger_bytes_store_u32(detail + 544u, badger_bytes_load_u32(detail + 544u) + 1u);
        badger_bytes_store_u32(detail + 796u, n < 192u ? n : 192u);
        badger_bytes_copy(detail + 800u, m, n < 192u ? n : 192u);
    } else if (clean_ack_reply(m, n, detail + 1972u)) {
        badger_bytes_store_u32(detail + 548u, badger_bytes_load_u32(detail + 548u) + 1u);
    } else if (clean_ack_reply(m, n, station_target)) {
        badger_bytes_store_u32(detail + 556u, badger_bytes_load_u32(detail + 556u) + 1u);
    } else if (n >= 28u && m[0] != 0x80u &&
        badger_bytes_equal(m + 10u, station_target, 6u) &&
        badger_bytes_equal(m + 4u, detail + 1972u, 6u)) {
        badger_bytes_store_u32(detail + 552u, badger_bytes_load_u32(detail + 552u) + 1u);
        badger_bytes_store_u32(detail + 1088u, size);
        badger_bytes_copy(detail + 1104u, packet,
            size < CLEAN_DETAIL_PACKET_BYTES ? size : CLEAN_DETAIL_PACKET_BYTES);
    }
}

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
    if (station_mode() && size >= 40u &&
        badger_bytes_equal(packet + 34u, station_target, 6u)) {
        station_ap_status = clean_station_beacon(packet + 18u, size - 18u,
                                                 station_target, &station_ap);
    }
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
        if (!clean_receive(l, packet, &size)) return 0;
        if (size != 0u) {
            if (detail[2000u] == 17u) monitor_packet(packet, size);
            if (station_mode() && size >= 18u &&
                badger_bytes_load_u32(detail + 524u) == 4u) {
                int reason = clean_station_departure(packet + 18u, size - 18u,
                                                     detail + 1972u, detail + 1980u);
                if (reason >= 0) {
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
            int peer = detail[2000u] == 13u && size >= 18u &&
                clean_peer_reply(packet + 18u, size - 18u, peer_challenge);
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

static int station_command(clean_loader_t *l, uint8_t *bytes, uint32_t size, uint32_t cmd) {
    uint8_t readback[64], staged[64];
    if (!size || size>512u) { l->error=CLEAN_INPUT; return 0; }
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

static int station_wait(clean_loader_t *l, const uint8_t mac[6], uint8_t association,
                         uint16_t *aid, uint32_t duration) {
    uint32_t started = l->wire->now_us(l->wire->context);
    while (l->wire->now_us(l->wire->context) - started < duration) {
        uint8_t *packet=rx_packet;
        uint32_t size;
        if (!clean_receive(l, packet, &size)) return -1;
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
            badger_bytes_store_u32(detail + 568u, (uint32_t)reason);
            return -2;
        }
        int status = clean_station_response(m, n, mac, station_ap.bssid, association, aid);
        if (status < 0) continue;
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
    wpa_wipe(wpa_secret,sizeof(wpa_secret));
    uint32_t started=l->wire->now_us(l->wire->context), last_probe=started;
    uint8_t *packet=rx_packet;
    uint8_t tx[512];
    while (!l->error && !wpa.error && l->wire->now_us(l->wire->context)-started<3000000u) {
        uint32_t size;
        if (!clean_receive(l,packet,&size)) break;
        if (size>=18u) {
            const uint8_t *m=packet+18u;
            if (size<=CLEAN_DETAIL_PACKET_BYTES && size>=42u && !(m[1]&64u) &&
                wpa_equal(m+4u,mac,6u) && clean_frame_valid(m,size-18u)) {
                badger_bytes_store_u32(detail+1088u,size);
                badger_bytes_copy(detail+1104u,packet,size);
            }
            int reason=clean_station_departure(m,size-18u,mac,station_ap.bssid);
            if (reason>=0) {
                badger_bytes_store_u32(detail+568u,(uint32_t)reason);
                badger_bytes_store_u32(detail+524u,5u);
                wpa.error=WPA_FORMAT; break;
            }
            uint32_t length;
            int result=clean_wpa_receive(&wpa,m,size-18u,tx,&length);
            if (result<0) break;
            if (result) {
                if (!station_command(l,tx,length,5u|clean_tx_length_bits(length)|
                    (1u<<18u)|(1u<<19u)|CLEAN_TX_ACK_REQUIRED)) break;
                if (wpa.installed) ++wpa.m4; else ++wpa.m2;
                if (wpa.stage==WPA_KEYS) wpa.stage=WPA_M4_SENT;
            } else if (wpa.installed && clean_wpa_protected(&wpa,m,size-18u)>0) break;
        }
        uint32_t tick=l->wire->now_us(l->wire->context);
        if (wpa.stage>=WPA_M4_SENT && tick-last_probe>=100000u) {
            uint32_t length=clean_wpa_probe(&wpa,tx);
            if (!length || !station_command(l,tx,length,5u|clean_tx_length_bits(length)|
                (1u<<18u)|(1u<<19u)|CLEAN_TX_ACK_REQUIRED)) break;
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
    config[15] = 1u;
    if (!station_command(l, config, sizeof(config), 7u)) return;
    badger_bytes_store_u32(detail + 524u, 1u);
    uint16_t aid = 0u;
    for (uint8_t association = 0u; association < 2u; ++association) {
        if (association && !receive_for(l,10000u,0u)) return;
        uint8_t frame[160];
        uint16_t sequence = (uint16_t)(clean_station_u16(nonce + 11u) + association);
        uint32_t length = association ?
            clean_station_associate(frame, sequence, mac, &station_ap) :
            clean_station_auth(frame, sequence, mac, &station_ap);
        badger_bytes_store_u32(detail + 1092u, 1u);
        int status = -1;
        for (uint32_t attempt = 0u; attempt < 4u; ++attempt) {
            if (attempt) {
                frame[7] |= 8u;
                badger_bytes_store_u32(frame + length - 4u, diag_crc32(frame + 6u, length - 10u));
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
            badger_bytes_store_u32(detail + at, badger_bytes_load_u32(detail + at) +
                                  badger_bytes_load_u32(status_bytes + 8u));
            uint32_t ack = badger_bytes_load_u32(status_bytes + 12u);
            badger_bytes_store_u32(detail + 548u, badger_bytes_load_u32(detail + 548u) + ack);
            status = station_wait(l, mac, association, &aid, ack ? 400000u : 100000u);
            if (status != -1 || l->error) break;
        }
        badger_bytes_store_u32(detail + 528u + 4u*association, (uint32_t)status);
        badger_bytes_store_u32(detail + 524u, association ? 3u : 2u);
        if (status != 0 || l->error) return;
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
    if (badger_schedule_bootsel(8000u) != 0) l.error = CLEAN_CANCELLED;
    badger_bytes_zero(detail, sizeof(detail));
    int secret_valid=1;
    if (nonce[14]==18u) {
        uint8_t digest[20];
        badger_bytes_copy(wpa_secret,control.clean_secret,64u);
        wpa_sha1_t sha;
        wpa_sha1_init(&sha); wpa_sha1_update(&sha,wpa_secret,64u);
        wpa_sha1_update(&sha,nonce,6u); wpa_sha1_update(&sha,nonce+15u,1u);
        wpa_sha1_final(&sha,digest);
        secret_valid=control.clean_secret_ready && wpa_equal(digest,nonce+6u,8u);
        wpa_wipe(digest,sizeof(digest));
    }
    wpa_wipe(control.clean_secret,64u); control.clean_secret_ready=0u;
    uint32_t length = (uint32_t)(clean_image_end - clean_image);
    uint32_t channel = nonce[15];
    if (nonce[14] > 18u || !secret_valid) l.error = CLEAN_INPUT;
    detail[2000u] = nonce[14];
    badger_bytes_copy(peer_challenge, nonce, sizeof(peer_challenge));
    badger_bytes_copy(station_target, nonce, 6u);
    station_ap_status = 0;
    badger_bytes_store_u32(detail + 2004u, nonce[14] == 8u || nonce[14] == 10u);
    badger_bytes_store_u32(detail + 2008u, nonce[14] == 9u || nonce[14] == 10u);
    if (l.error == CLEAN_OK &&
        clean_boot(&l, clean_image, length, channel, badger_bytes_load_u32(nonce) | 1u,
                   nonce[14] != 0u && nonce[14] != 2u && nonce[14] != 13u && nonce[14] != 17u) &&
        clean_write(&l, 0x18002024u, 0xf0u) &&
        clean_write(&l, CLEAN_MBOX + 24u, 2u) && clean_wait_state(&l, 9u, 100000u)) {
        l.stage = 7u;
        (void)receive_for(&l, nonce[14] == 13u || nonce[14] == 17u ? 2500000u : 700000u, 0u);
        if (l.error == CLEAN_OK && station_mode()) station_run(&l, nonce);
        if (l.error == CLEAN_OK && nonce[14] != 0u && nonce[14] != 13u &&
            nonce[14] != 16u && nonce[14] != 17u && nonce[14]!=18u) {
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
                            5u | (tx_length << 8u) | (1u << 19u) |
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
    uint32_t saved_error = l.error, saved_addr = l.address, saved_actual = l.actual;
    /* Collect fault evidence even after a failed stage, still under a deadline. */
    l.error = CLEAN_OK;
    int snapshot = clean_block(&l, CLEAN_MBOX, detail + 32u, 448u, 0u) &&
                   clean_block(&l, CLEAN_MULTI, detail + 480u, 40u, 0u);
    if (snapshot && !station_mode() && nonce[14] != 17u)
        snapshot = clean_block(&l, CLEAN_MULTI + 40u, detail + 520u, 568u, 0u);
    if (snapshot && station_mode()) {
        uint8_t status[sizeof(clean_unicast_status_t)];
        snapshot = clean_block(&l, CLEAN_UNICAST_ADDR, status, sizeof(status), 0u);
        if (snapshot) {
            badger_bytes_copy(detail + 552u, status + 16u, 8u);
            badger_bytes_copy(detail + 1000u, status, sizeof(status));
        }
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

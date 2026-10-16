#include "display.h"
#include "graphics.h"
#include "diagnostics_logic.h"
#include "wifi_board.h"
#include "../platform/badger/wifi_pio.h"
#include "../usb/live_usb_hw.h"
#include "../arch/rp2350/bootrom_reboot.h"
#include "../radio/wifi_firmware.h"
#include "../radio/wifi_assets.h"
#include "../radio/wifi_link_report.h"
#include "wifi_link_view.h"
#ifdef LIVE_USB_UDP
#include "../radio/wifi_udp_report.h"
#include "usb_udp_build.h"
#ifdef BADGER_WIFI_SMOKE_40M
#define EXPERIMENT_TICKS 40000000u
#else
#define EXPERIMENT_TICKS 30000000u
#endif
#else
#include "usb_link_build.h"
#define EXPERIMENT_TICKS 15000000u
#endif

static badger_canvas_t canvas;
static live_usb_control_t usb;
static wifi_link_t link;
static wifi_fw_report_t firmware;
static uint8_t session_nonce[16];
static uint32_t generation;
static uint32_t boot_started, last_watchdog;
static uint32_t last_usb_poll;
static uint32_t last_draw_step;
static uint32_t last_publish, published_generation;
static uint32_t service_usb_ticks, service_publish_ticks, service_draw_ticks;
static uint32_t drawn_state = 0xffffffffu;
static uint32_t drawn_generation, drawn_command, drawn_status, drawn_peers;
static uint8_t drawn_peer[6];
static int32_t usb_error;
static uint8_t has_role;
static uint8_t display_ready;
static uint8_t detail[WIFI_TRACE_SIZE];
static wifi_link_snapshot_t snapshot;
static wifi_view_t view = {.first_frame = 1u};
static uint8_t fixture, fixture_data;
static uint32_t fixture_rx, fixture_tx;
#ifdef LIVE_USB_UDP
static wifi_udp_t network;
static wifi_udp_metrics_t metrics;
static uint32_t udp_epoch;
static uint8_t udp_active, udp_finished;
static uint32_t udp_finished_at;
static uint8_t rx_frame[WIFI_UDP_FRAME_MAX], tx_frame[WIFI_UDP_FRAME_MAX];
static uint8_t udp_detail[WIFI_UDP_DETAIL_SIZE];
static badger_canvas_t previous;
static uint32_t drawn_sent, drawn_received, drawn_lost, drawn_rtt;
#endif

static void store(uint8_t *bytes, uint32_t value) {
    badger_bytes_store_u32(bytes, value);
}

static uint32_t now(void) {
    return *(volatile uint32_t *)0x400b000cu;
}

static void mac_text(char text[18], const uint8_t mac[6]) {
    const char digits[] = "0123456789ABCDEF";
    for (uint32_t i = 0u; i < 6u; ++i) {
        text[3u*i] = digits[mac[i] >> 4u];
        text[3u*i+1u] = digits[mac[i] & 15u];
        text[3u*i+2u] = ':';
    }
    text[17] = '\0';
}

static void draw_text(int32_t x, int32_t y, const char *text,
                      uint32_t scale, uint16_t color) {
    if (!wifi_view_text(&view, x, y, text, scale, color)) usb_error = -2020;
}

static void prepare_draw(int force) {
    const wifi_link_report_t *report = wifi_link_snapshot_view(&snapshot,&link.report);
    uint32_t key = (uint32_t)report->stage | (uint32_t)report->error << 8u |
                   (uint32_t)report->authorized << 16u | (uint32_t)firmware.stage << 20u;
#ifdef LIVE_USB_UDP
    if(udp_active) key|=(uint32_t)network.stage<<25u | (uint32_t)network.error<<28u;
#endif
    if (!force && key == drawn_state && generation == drawn_generation &&
        report->last_command == drawn_command && report->controller_status == drawn_status &&
        report->peer_count == drawn_peers && badger_bytes_equal(report->peer, drawn_peer, 6u)
#ifdef LIVE_USB_UDP
        && (!udp_active || (network.sent == drawn_sent && network.received == drawn_received &&
                           network.lost == drawn_lost && network.last_rtt == drawn_rtt))
#endif
    ) return;
    drawn_state = key;
    drawn_generation = generation;
    drawn_command = report->last_command;
    drawn_status = report->controller_status;
    drawn_peers = report->peer_count;
    badger_bytes_copy(drawn_peer, report->peer, 6u);
#ifdef LIVE_USB_UDP
    drawn_sent = network.sent;
    drawn_received = network.received;
    drawn_lost = network.lost;
    drawn_rtt = network.last_rtt;
#endif
    view.count = view.offset = view.command = view.character = view.tile = 0u;
    view.phase = 1u;
    uint16_t color = report->error != WIFI_LINK_OK ? 0xf9c7u :
                     report->stage == WIFI_LINK_CONNECTED ? 0x5ecbu : 0xfda8u;
    draw_text(8, 8, !has_role ? "WIFI LINK LAB" :
                    report->role == WIFI_LINK_AP ? "AP HOST" : "CLIENT", 3u, color);
    draw_text(8, 43, usb.serial, 1u, 0x9cf3u);
    draw_text(8, 65, has_role ? link.ssid : "WAITING FOR USB HOST", 2u, 0xffffu);
    const char *status = report->stage == WIFI_LINK_FIRMWARE ?
                         wifi_fw_stage_name(firmware.stage) : wifi_link_stage_name(report->stage);
#ifdef LIVE_USB_UDP
    if (udp_active) {
        status=wifi_udp_stage_name(network.stage);
        if (network.error!=WIFI_UDP_OK) color=0xf9c7u;
    }
#endif
    draw_text(8, 101, status, 2u, color);
    char peer[18], number[12];
#ifdef LIVE_USB_UDP
    if (!udp_active) {
#endif
    mac_text(peer, report->peer);
    draw_text(8, 135, "PEER", 1u, 0x9cf3u);
    draw_text(47, 135, peer, 1u, 0xffffu);
    diag_decimal(number, (int32_t)report->peer_count);
    draw_text(8, 158, "PEERS", 1u, 0x9cf3u);
    draw_text(47, 153, number, 2u, color);
    draw_text(88, 158, link.open_network ? "OPEN / NO ENCRYPTION" :
              report->authorized ? "WPA2 AUTHORIZED" : "AUTH NOT CONFIRMED", 1u, color);
    if (report->error != WIFI_LINK_OK) {
        draw_text(8, 182, wifi_link_error_name(report->error), 1u, color);
        diag_hex(number, report->controller_status, 8u);
    } else {
        draw_text(8, 182, "LAST CONTROL", 1u, 0x9cf3u);
        diag_hex(number, report->last_command, 8u);
    }
    draw_text(196, 182, number, 1u, 0xffffu);
#ifdef LIVE_USB_UDP
    }
#endif
#ifdef LIVE_USB_UDP
    if (udp_active) {
        draw_text(8,133,network.role==0u?"192.168.77.1":"192.168.77.2",1u,0x9cf3u);
        draw_text(8,155,"TX",1u,0x9cf3u);
        diag_decimal(number,(int32_t)network.sent); draw_text(32,151,number,2u,color);
        draw_text(102,155,"RX",1u,0x9cf3u);
        diag_decimal(number,(int32_t)network.received); draw_text(126,151,number,2u,color);
        draw_text(206,155,"LOST",1u,0x9cf3u);
        diag_decimal(number,(int32_t)network.lost); draw_text(240,151,number,2u,color);
        draw_text(8,182,network.error?wifi_udp_error_name(network.error):"LAST RTT TICKS",1u,color);
        diag_decimal(number,(int32_t)network.last_rtt); draw_text(176,179,number,2u,color);
        draw_text(8,214,"100 UDP ECHOES EACH WAY / USB RESULTS",1u,0x9cf3u);
    } else draw_text(8,214,"UDP TEST ARMS AFTER AUTHORIZATION",1u,0x9cf3u);
#else
    draw_text(8, 214, "NO IP/UDP YET / BOUNDED USB RECOVERY", 1u, 0x9cf3u);
#endif
}

static void draw(void) {
    if (!display_ready) return;
    uint32_t tick = now();
    uint32_t interval = has_role ? 10000u : 1000u;
#ifdef LIVE_USB_UDP
    if (udp_finished) interval = 1000u;
#endif
    if (tick - last_draw_step < interval) return;
    last_draw_step = tick;
    if (view.phase == 0u) prepare_draw(0);
    wifi_view_step(&view, &canvas,
#ifdef LIVE_USB_UDP
                   &previous
#else
                   0
#endif
    );
}

static void draw_final(void) {
    if (!display_ready) return;
#ifdef LIVE_USB_UDP
    badger_canvas_t *last = &previous;
#else
    badger_canvas_t *last = 0;
#endif
    wifi_view_finish(&view, &canvas, last);
    prepare_draw(1);
    wifi_view_finish(&view, &canvas, last);
}

static void publish(void) {
    if (!has_role) return;
    last_publish = now();
    published_generation = generation;
    uint8_t report[64];
    wifi_link_report_encode(report, USB_REPORT_BUILD_ID, session_nonce,
                            wifi_link_snapshot_view(&snapshot,&link.report));
    live_usb_link_publish(&usb, generation, report);
    store(detail,0x314b4e4cu);
    store(detail+40u,(uint32_t)firmware.error);
    store(detail+44u,firmware.verified_bytes);
    store(detail+48u,firmware.expected);
    store(detail+52u,firmware.actual);
    store(detail+56u,(uint32_t)firmware.stage);
    store(detail+60u,firmware.elapsed_ticks);
    store(detail+92u,firmware.address);
    store(detail+96u,firmware.service_ticks);
    store(detail+100u,firmware.transfer_ticks);
    store(detail+104u,service_usb_ticks);
    store(detail+108u,service_publish_ticks);
    store(detail+112u,service_draw_ticks);
    store(detail+116u,view.phase);
    store(detail+120u,view.tile);
    const uint32_t *pio_profile = wifi_pio_profile();
    store(detail+124u,pio_profile[0]);
    store(detail+128u,pio_profile[1]);
    store(detail+132u,pio_profile[2]);
    store(detail+136u,wifi_link_snapshot_view(&snapshot,&link.report)->associated);
    store(detail+140u,fixture_rx);
    store(detail+144u,fixture_tx);
    for (uint32_t i=0u;i<28u;++i) detail[64u+i]=firmware.last_rx_prefix[i];
    live_usb_link_details(&usb,generation,detail);
#ifdef LIVE_USB_UDP
    if (udp_active) {
        if (!udp_finished && (network.stage==WIFI_UDP_DONE || network.stage==WIFI_UDP_FAILED)) {
            udp_finished=1u; udp_finished_at=now();
        }
        metrics.elapsed_ticks=(udp_finished?udp_finished_at:now())-network.started;
        metrics.radio_drops=wifi_firmware_rx_dropped();
        uint8_t bench[64];
        wifi_udp_report_encode(bench,USB_REPORT_BUILD_ID,session_nonce,&network,&metrics);
        wifi_udp_detail_encode(udp_detail,&network,&metrics);
        live_usb_udp_publish(&usb,generation,udp_epoch,bench,udp_detail,udp_finished);
    }
#endif
}

static int poll_usb(void) {
    uint32_t tick = now();
    if (usb_error == 0 && (live_usb_control_poll_immediate(&usb) ||
                           tick - last_usb_poll >= 5000u)) {
        last_usb_poll = tick;
        usb_error = live_usb_hw_poll(&usb);
    }
    if (usb_error != 0 || usb.reboot_requested || tick - boot_started >= EXPERIMENT_TICKS) return 0;
    if (tick - last_watchdog >= 500000u) {
        usb_error = badger_schedule_bootsel(8000u);
        last_watchdog = tick;
        if (usb_error != 0) return 0;
    }
    if (has_role && (generation != usb.probe_generation || !usb.configured)) return 0;
    return 1;
}

static int initialize_display_service(void *context) {
    (void)context;
    return poll_usb();
}

static int service(void *context, const wifi_fw_report_t *report) {
    (void)context;
    (void)report;
    uint32_t started = now();
    if (!poll_usb()) return 0;
    uint32_t polled = now();
    if (published_generation != generation || now() - last_publish >= 50000u) publish();
    uint32_t published = now();
    draw();
    uint32_t drawn = now();
    service_usb_ticks += polled - started;
    service_publish_ticks += published - polled;
    service_draw_ticks += drawn - published;
    return 1;
}

static int settle(uint32_t ticks) {
    while (ticks != 0u) {
        uint32_t part = ticks > 1000u ? 1000u : ticks;
        if (!service(0, &firmware) || wifi_wire_delay(wifi_board_wire(), part) != WIFI_BUS_OK) {
            link.report.controller_status = 0x80000000u | WIFI_FW_CANCELLED;
            return 0;
        }
        ticks -= part;
    }
    return 1;
}

static int control(void *context, uint32_t command, uint8_t set, uint8_t interface,
                    const uint8_t *request, uint32_t size,
                    uint8_t *reply, uint32_t capacity, uint32_t *reply_size) {
    (void)context;
    link.report.last_command = command;
    detail[4] = interface;
    detail[5] = set;
    detail[6] = 0u;
    for (uint32_t i = 0u; i < 32u; ++i) detail[8u+i] = 0u;
    if ((command == 262u || command == 263u) && request != 0) {
        for (uint32_t i = 0u; i < size && i < 31u && request[i] != 0u; ++i) {
            uint8_t c = request[i];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ':' || c == '_')) break;
            detail[8u+i] = c;
            ++detail[6];
        }
    }
    if (!service(0, &firmware)) return 0;
    /* Conservative settling in inherited timer ticks; USB remains serviced. */
    if (set && command == 2u && !settle(150000u)) return 0;
    if (set && command == 268u && !settle(2000u)) return 0;
    int ok = wifi_firmware_control(command, set, interface, request, size, reply, capacity, reply_size);
    link.report.controller_status = firmware.controller_status;
    if (!ok && firmware.error != WIFI_FW_CONTROLLER_ERROR) {
        link.report.controller_status = 0x80000000u | (uint32_t)firmware.error;
    }
    int country = request != 0 && size >= 8u;
    const uint8_t country_name[] = "country";
    for (uint32_t i = 0u; country && i < sizeof(country_name); ++i) {
        if (request[i] != country_name[i]) country = 0;
    }
    if (ok && set && (command == 2u || (command == 263u && country))) ok = settle(50000u);
    publish();
    return ok;
}

#ifdef LIVE_USB_UDP
static int calibrate(void) {
    volatile uint32_t *sof=(volatile uint32_t *)0x50110048u;
    uint32_t previous_sof=*sof&0x7ffu, started=now(), first=0u, frames=0u;
    uint8_t aligned=0u;
    for(uint32_t i=0u;i<3000000u;++i) {
        int32_t error=live_usb_hw_poll(&usb);
        if(error!=0) { usb_error=error; return 0; }
        if(usb.reboot_requested)return 0;
        uint32_t frame=*sof&0x7ffu, tick=now();
        if(frame!=previous_sof) {
            if(!aligned) { aligned=1u; first=tick; }
            else frames+=(frame-previous_sof)&0x7ffu;
            previous_sof=frame;
            if(frames>=128u) {
                metrics.calibration_ticks=tick-first;
                metrics.calibration_frames=frames;
                return metrics.calibration_ticks!=0u && frames<=256u;
            }
        }
        if(tick-started>=500000u)break;
    }
    return 0;
}

static void fail_udp(wifi_udp_error_t error) {
    network.stage=WIFI_UDP_FAILED;
    network.error=error;
}

static int send_udp_frame(uint32_t length) {
    uint8_t interface=network.role==0u?1u:0u;
    if(!wifi_firmware_send_frame(interface,tx_frame,length)) { fail_udp(WIFI_UDP_TX_ERROR); return 0; }
    ++metrics.tx_frames;
    return 1;
}

static void run_udp(void) {
    if (udp_epoch!=usb.udp_generation || generation!=usb.probe_generation || !usb.configured) {
        udp_active=0u; return;
    }
    if(network.stage==WIFI_UDP_FAILED || usb.udp_frozen)return;
    for(uint32_t packets=0u;packets<8u;++packets) {
        uint32_t length; uint8_t interface;
        int received=wifi_firmware_read_frame(rx_frame,&length,&interface);
        if(received<0) { fail_udp(WIFI_UDP_RADIO_ERROR); break; }
        if(!received)break;
        ++metrics.rx_frames;
        if(interface!=(network.role==0u?1u:0u)) {
            ++network.dropped; network.last_drop=WIFI_UDP_DROP_ADDRESS; continue;
        }
        uint32_t reply_length=0u;
        wifi_udp_result_t result=wifi_udp_receive(&network,now(),rx_frame,length,tx_frame,&reply_length);
        if(result==WIFI_UDP_SEND_FRAME && !send_udp_frame(reply_length))break;
        if(result==WIFI_UDP_ERROR)break;
    }
    if(network.stage!=WIFI_UDP_FAILED && network.stage!=WIFI_UDP_DONE) {
        if(now()-network.started>=15000000u) fail_udp(WIFI_UDP_DEADLINE);
        else {
            uint32_t length=0u;
            wifi_udp_result_t result=wifi_udp_step(&network,now(),tx_frame,&length);
            if(result==WIFI_UDP_SEND_FRAME) send_udp_frame(length);
        }
    }
    if (!udp_finished && (network.stage == WIFI_UDP_DONE || network.stage == WIFI_UDP_FAILED))
        publish();
    else if (now() - last_publish >= 50000u) publish();
}
#endif

void bare_main(void) {
    boot_started = last_watchdog = now();
    usb_error = badger_schedule_bootsel(8000u);
    uint32_t id[2];
    char serial[17], half[9];
    int32_t identity = badger_rom_chip_id(id);
    if (usb_error == 0 && identity != 0) usb_error = identity;
    if (usb_error != 0) {
        badger_display_init();
        display_ready = 1u;
        link.report.stage = WIFI_LINK_FAILED;
        link.report.error = WIFI_LINK_RADIO_ERROR;
        link.report.controller_status = (uint32_t)usb_error;
        draw_final();
        while (1) __asm__ volatile ("wfi");
    }
    diag_hex(half, id[1], 8u);
    for (uint32_t i = 0u; i < 8u; ++i) serial[i] = half[i];
    diag_hex(half, id[0], 8u);
    for (uint32_t i = 0u; i < 8u; ++i) serial[i+8u] = half[i];
    serial[16] = '\0';
    live_usb_control_init(&usb, USB_REPORT_BUILD_ID, serial);
    usb_error = live_usb_hw_init();
    display_ready = (uint8_t)badger_display_init_serviced(initialize_display_service, 0);
    if (!display_ready && usb_error == 0)
        usb.reboot_requested = 1u;
    draw();
    uint8_t active = 0u, reboot_wait = 0u;
    uint32_t active_started = 0u, last_poll = 0u, reboot_at = 0u;
    const uint8_t mac[6] = {2u, (uint8_t)(id[1] >> 24u), (uint8_t)(id[1] >> 16u),
                            (uint8_t)(id[0] >> 16u), (uint8_t)(id[0] >> 8u), (uint8_t)id[0]};
    const wifi_link_ops_t ops = {.context = 0, .control = control};
    while (1) {
        int alive = service(0, &firmware);
        uint32_t tick = now();
        if (active && (!alive || generation != usb.probe_generation)) {
#ifdef LIVE_USB_UDP
            if(udp_active) fail_udp(WIFI_UDP_RADIO_ERROR);
#endif
            wifi_firmware_stop();
            active = 0u;
        }
        uint8_t nonce[16];
        uint32_t next = live_usb_probe_take(&usb, nonce);
        if (next != 0u && usb_error == 0 && usb.incoming_test == WIFI_LINK_TEST_ID) {
#ifdef LIVE_USB_UDP
            udp_active=udp_finished=0u;
#endif
            wifi_firmware_stop();
            generation = next;
            for (uint32_t i = 0u; i < 16u; ++i) session_nonce[i] = nonce[i];
            fixture=usb.probe_role==WIFI_LINK_FIXTURE_AP;
            fixture_data=0u; fixture_rx=fixture_tx=0u;
            wifi_link_init(&link, &ops, usb.probe_role, session_nonce, mac);
            has_role = 1u;
            link.report.stage = WIFI_LINK_FIRMWARE;
            wifi_board_init();
            wifi_board_select_pio(1u);
            active_started = now();
            if (!wifi_firmware_begin(wifi_board_wire(), wifi_firmware_assets(), &firmware, service, 0)) {
                link.report.stage = WIFI_LINK_FAILED;
                link.report.error = WIFI_LINK_RADIO_ERROR;
                link.report.controller_status = 0x80000000u | (uint32_t)firmware.error;
            } else if (!wifi_link_configure(&link)) {
                wifi_firmware_stop();
            } else {
                active = 1u;
                active_started = now();
            }
            last_poll = now();
            publish();
            draw();
        }
        tick = now();
        uint32_t poll_interval=50000u;
#ifdef LIVE_USB_UDP
        if(udp_active)poll_interval=500000u;
#endif
        if (active && tick - last_poll >= poll_interval) {
            last_poll = tick;
            wifi_link_snapshot_begin(&snapshot,&link.report);
            int polled = wifi_link_poll(&link);
            wifi_link_snapshot_end(&snapshot);
            if (!polled) {
                wifi_firmware_stop();
                active = 0u;
            } else if (tick - active_started >= 8000000u && link.report.stage != WIFI_LINK_CONNECTED) {
                link.report.stage = WIFI_LINK_FAILED;
                link.report.error = WIFI_LINK_JOIN_TIMEOUT;
                wifi_firmware_stop();
                active = 0u;
            }
            publish();
        }
        if (active && fixture && link.report.authorized) {
            if (!fixture_data) {
                if (!wifi_firmware_data_enable()) {
                    link.report.stage=WIFI_LINK_FAILED; link.report.error=WIFI_LINK_RADIO_ERROR;
                    wifi_firmware_stop(); active=0u; publish();
                } else fixture_data=1u;
            }
            if (active && fixture_data) {
                uint8_t frame[1514], interface;
                uint32_t length;
                int received=wifi_firmware_read_frame(frame,&length,&interface);
                if (received<0) {
                    link.report.stage=WIFI_LINK_FAILED; link.report.error=WIFI_LINK_RADIO_ERROR;
                    wifi_firmware_stop(); active=0u; publish();
                } else if (received && interface==1u && length>=38u &&
                    badger_bytes_equal(frame,link.report.mac,6u) &&
                    badger_bytes_equal(frame+6u,link.report.peer,6u) &&
                    frame[12]==136u && frame[13]==181u &&
                    badger_bytes_equal(frame+14u,(const uint8_t *)"U26WPA2",8u)) {
                    ++fixture_rx;
                    badger_bytes_copy(frame,link.report.peer,6u);
                    badger_bytes_copy(frame+6u,link.report.mac,6u); frame[21]=1u;
                    if (!wifi_firmware_send_frame(1u,frame,38u)) {
                        link.report.stage=WIFI_LINK_FAILED; link.report.error=WIFI_LINK_RADIO_ERROR;
                        wifi_firmware_stop(); active=0u;
                    } else ++fixture_tx;
                    publish();
                }
            }
        }
#ifdef LIVE_USB_UDP
        uint32_t bench=live_usb_udp_take(&usb);
        if(bench!=0u) {
            udp_active=0u; udp_finished=0u; udp_epoch=bench;
            metrics.calibration_ticks=metrics.calibration_frames=0u;
            metrics.tx_frames=metrics.rx_frames=metrics.radio_drops=metrics.elapsed_ticks=0u;
            int calibrated=calibrate();
            wifi_udp_init(&network,link.report.role,link.report.mac,link.report.peer,now());
            udp_active=1u;
            if(!active || link.report.stage!=WIFI_LINK_CONNECTED || !link.report.authorized) fail_udp(WIFI_UDP_BAD_CONFIG);
            else if(!calibrated || !wifi_firmware_data_enable()) fail_udp(WIFI_UDP_RADIO_ERROR);
            publish();
        }
        if(udp_active && active)run_udp();
#endif
        if (usb.reboot_requested && !reboot_wait) {
            reboot_wait = 1u;
            reboot_at = now() + 10000u;
        }
        if (usb_error != 0 || now() - boot_started >= EXPERIMENT_TICKS ||
            (reboot_wait && (int32_t)(now() - reboot_at) >= 0)) {
            wifi_firmware_stop();
            if (usb_error != 0) {
                link.report.stage = WIFI_LINK_FAILED;
                link.report.error = WIFI_LINK_RADIO_ERROR;
                link.report.controller_status = (uint32_t)usb_error;
                draw_final();
            }
            live_usb_hw_disconnect();
            int32_t reboot_error = badger_schedule_bootsel(10u);
            if (reboot_error != 0) {
                link.report.stage = WIFI_LINK_FAILED;
                link.report.error = WIFI_LINK_RADIO_ERROR;
                link.report.controller_status = (uint32_t)reboot_error;
                draw_final();
            }
            while (1) __asm__ volatile ("wfi");
        }
    }
}

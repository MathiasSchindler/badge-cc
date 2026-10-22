#include "link.h"
#include "build_id.h"
#include "screen.h"
#include "../badge/loader.h"
#include "../../src/usb/live_usb_hw.h"
#include "../../src/arch/rp2350/bootrom_reboot.h"
#include "../../src/arch/rp2350/nametag_clock.h"
#include "../../src/platform/badger/wifi_board.h"
#include "../../src/platform/badger/display.h"
#include "../../src/platform/badger/graphics.h"

_Static_assert(MSG_ROLE == 1 || MSG_ROLE == 2, "Build one image for each badge role");
extern const uint8_t clean_image[], clean_image_end[];
static live_usb_control_t usb;
static clean_loader_t radio;
static msg_link_t link;
static badger_canvas_t canvas;
static uint8_t packet[CLEAN_RX_PACKET_BYTES], tx[MSG_STORAGE], last_packet[98];
static uint32_t restarts, last_fault, fault_address, fault_actual, retry_boot_at;
static uint32_t watchdog_at, screen_at, heartbeat_at, boot_at, received, rejected;
static uint32_t clock_ticks, clock_frames, screen_ticks, screen_max, loop_max, poll_at;
static uint32_t scheduled_at, scheduled_token;
static uint8_t scheduled_buttons, scheduled;
static uint32_t display_column, display_started, display_session, display_sequence;
static uint32_t paint_ticks, strip_max;
static uint8_t display_busy, painted;
static int32_t usb_error;
static uint8_t radio_ready;

static uint32_t now_us(void) { return *(volatile uint32_t *)0x400b000cu; }

static int service(void *unused) {
    (void)unused;
    usb_error=live_usb_hw_poll(&usb);
    uint32_t now=now_us();
    if (!usb_error && now-watchdog_at >= 3000000u) {
        usb_error=badger_schedule_bootsel(8000u);
        watchdog_at=now;
    }
    return !usb_error && !usb.reboot_requested;
}

static void buttons_init(void) {
    static const uint8_t pins[] = {7u,9u,10u,11u,6u};
    for (uint32_t i=0u; i<sizeof(pins); ++i) {
        *(volatile uint32_t *)0xd0000040u = 1u<<pins[i];
        *(volatile uint32_t *)(0x40028004u+8u*pins[i])=5u;
        *(volatile uint32_t *)(0x40038004u+4u*pins[i])=0xcau;
    }
}

static uint8_t buttons_read(void) {
    static const uint8_t pins[] = {7u,9u,10u,11u,6u};
    uint32_t input=*(volatile uint32_t *)0xd0000004u;
    uint8_t result=0u;
    for (uint32_t i=0u; i<sizeof(pins); ++i)
        if (!(input & (1u<<pins[i]))) result |= (uint8_t)(1u<<i);
    return result;
}

static void number(int32_t x, int32_t y, const char *label, uint32_t value) {
    char text[12];
    badger_gfx_text(&canvas,x,y,label,1u,0xffffu);
    diag_decimal(text,(int32_t)value);
    badger_gfx_text(&canvas,x+96,y,text,1u,0xffffu);
}

static void button_row(int32_t y, uint8_t mask) {
    static const char *const names[] = {"LEFT","RIGHT","ENTER","UP","DOWN"};
    for (uint32_t i=0u; i<5u; ++i) {
        int32_t x=8+(int32_t)i*62;
        badger_gfx_rect(&canvas,x,y,58,22,mask&(1u<<i)?0x2589u:0x2104u);
        badger_gfx_text(&canvas,x+4,y+7,names[i],1u,0xffffu);
    }
}

static void screen_paint(void) {
    uint32_t now=now_us();
    if (!painted || MSG_BASELINE) badger_gfx_clear(&canvas,0x0863u);
    else {
        badger_gfx_rect(&canvas,0,48,320,20,0x0863u);
        badger_gfx_rect(&canvas,0,150,320,16,0x0863u);
        badger_gfx_rect(&canvas,0,170,320,48,0x0863u);
    }
    badger_gfx_text(&canvas,8,8,MSG_ROLE==1?"HT20 BADGE A":"HT20 BADGE B",2u,0xffffu);
    badger_gfx_text(&canvas,8,34,"CH 1 / MCS 0 / DIRECT / NO AP",1u,0x9cf3u);
    const char *state=!radio_ready?"RADIO RESTARTING":
        link.peer_at && now-link.peer_at<6000000u?"PEER ONLINE":"WAITING FOR PEER";
    badger_gfx_text(&canvas,8,52,state,1u,radio_ready?0x5ecbu:0xfda8u);
    badger_gfx_text(&canvas,8,72,"LOCAL BUTTONS",1u,0xffffu);
    button_row(84,link.buttons);
    badger_gfx_text(&canvas,8,114,"REMOTE BUTTONS",1u,0xffffu);
    button_row(126,link.peer_buttons);
    char pressed[]="LAST REMOTE: L R E U D";
    static const uint8_t positions[]={13u,15u,17u,19u,21u};
    for (uint32_t i=0u; i<5u; ++i)
        if (!(link.last_press & (1u<<i))) pressed[positions[i]]='-';
    badger_gfx_text(&canvas,8,155,pressed,1u,0x5ecbu);
    number(8,174,"RX MSGS",link.rx_data);
    number(166,174,"ACKED",link.rx_ack);
    number(8,190,"FAILED",link.failed);
    number(166,190,"Q DROPS",link.dropped);
    number(8,206,"RADIO BOOTS",restarts);
    number(166,206,"FAULT",last_fault);
    badger_gfx_text(&canvas,8,220,"EXPERIMENTAL / UNENCRYPTED",1u,0xfda8u);
    painted=1u;
    paint_ticks=now_us()-now;
}

static void screen(void) {
    uint32_t now=now_us();
    if (!display_busy) {
        if (!MSG_BASELINE && radio_ready && (link.active || link.ack_pending)) return;
        if (now-screen_at<(MSG_BASELINE?250000u:40000u) &&
            (MSG_BASELINE || (link.receipt.sequence==display_sequence &&
                             link.receipt.session==display_session))) return;
        display_started=now;
        screen_paint();
        display_session=link.receipt.session; display_sequence=link.receipt.sequence;
        display_column=0u; display_busy=1u;
    }
    uint32_t start=now_us(),columns=MSG_BASELINE?320u:4u;
    msg_display_columns(&canvas,display_column,columns,service);
    display_column+=columns;
    if (display_column==320u) {
        badger_display_end();
        display_busy=0u; screen_at=now_us(); screen_ticks=screen_at-display_started;
        if (screen_ticks>screen_max) screen_max=screen_ticks;
        if (link.receipt.session==display_session && link.receipt.sequence==display_sequence)
            msg_displayed(&link,screen_at);
    } else badger_display_end_update();
    if (now_us()-start>strip_max) strip_max=now_us()-start;
}

static int clock_sample(void) {
    volatile uint32_t *sof=(volatile uint32_t *)0x50110048u;
    uint32_t previous=*sof&2047u, begin=now_us(), first=0u, frames=0u;
    uint8_t aligned=0u;
    while (now_us()-begin<100000u && service(0)) {
        uint32_t frame=*sof&2047u, now=now_us();
        if (frame==previous) continue;
        if (!aligned) { first=now; aligned=1u; }
        else frames+=(frame-previous)&2047u;
        previous=frame;
        if (frames>=32u) {
            clock_ticks=now-first; clock_frames=frames;
            return 1;
        }
    }
    return 0;
}

static void radio_fault(void) {
    last_fault=radio.error; fault_actual=radio.actual; fault_address=radio.address;
    radio_ready=0u; retry_boot_at=now_us()+1000000u;
    wifi_wire_power_off(wifi_board_wire());
}

static void radio_boot(void) {
    wifi_board_init();
    wifi_board_select_pio(1u);
    buttons_init();
    clean_loader_init(&radio,wifi_board_wire(),service,0);
    ++restarts;
    if (clean_boot(&radio,clean_image,(uint32_t)(clean_image_end-clean_image),
                   MSG_CHANNEL,link.session,1u) &&
        clean_write(&radio,0x18002024u,0xf0u) &&
        clean_write(&radio,CLEAN_MBOX+24u,2u) &&
        clean_wait_state(&radio,9u,100000u)) {
        radio_ready=1u; boot_at=now_us();
    } else radio_fault();
}

static int transmit(const uint8_t frame[MSG_MPDU]) {
    uint8_t readback[MSG_STORAGE];
    if (!msg_template(tx,frame)) { radio.error=CLEAN_INPUT; return 0; }
    if (!clean_block(&radio,CLEAN_TX_BUFFER_ADDR,tx,sizeof(tx),1u) ||
        !clean_block(&radio,CLEAN_TX_BUFFER_ADDR,readback,sizeof(readback),0u)) return 0;
    if (!badger_bytes_equal(tx,readback,sizeof(tx))) {
        radio.error=CLEAN_READBACK; radio.address=CLEAN_TX_BUFFER_ADDR; return 0;
    }
    uint32_t command=CLEAN_HT_DATA_CMD|clean_tx_length_bits(MSG_MPDU+6u)|(1u<<19u);
    return clean_write(&radio,CLEAN_MBOX+24u,command) &&
           clean_wait_command(&radio,command,100000u);
}

static void usb_snapshot(void) {
    uint8_t nonce[16], report[64], trace[WIFI_TRACE_SIZE];
    uint32_t generation=live_usb_probe_take(&usb,nonce);
    if (!generation) return;
    uint32_t request_error=0u;
    if (nonce[0]==1u) {
        if (!msg_enqueue(&link,nonce[1],MSG_USB,badger_bytes_load_u32(nonce+2u),now_us()))
            request_error=CLEAN_INPUT;
    } else if (nonce[0]==2u) {
        if (scheduled || nonce[1]>31u) request_error=CLEAN_INPUT;
        else {
            scheduled_at=now_us()+200000u; scheduled_buttons=nonce[1];
            scheduled_token=badger_bytes_load_u32(nonce+2u); scheduled=1u;
        }
    } else if (nonce[0]==3u) {
        if (!clock_sample()) request_error=CLEAN_TIMEOUT;
    } else if (nonce[0]) request_error=CLEAN_INPUT;
    badger_bytes_zero(trace,sizeof(trace));
    uint32_t values[] = {
        MSG_TRACE_MAGIC,MSG_ROLE,link.session,link.sequence,
        link.active?link.pending.sequence:0u,link.tx_data,link.tx_ack,link.rx_data,
        link.rx_ack,link.duplicates,link.failed,link.dropped,restarts,last_fault,
        link.buttons,link.peer_buttons,link.peer_session,link.peer_sequence,
        link.last_event.origin,link.last_event.token,link.last_acked.sequence,
        link.last_acked.origin,link.last_acked.token,
        link.peer_at?(now_us()-link.peer_at)/1000u:0xffffffffu,
        received,rejected,radio_ready,radio.rx_resyncs,fault_actual,fault_address,
        request_error,link.last_event.buttons,
    };
    for (uint32_t i=0u; i<sizeof(values)/sizeof(values[0]); ++i)
        badger_bytes_store_u32(trace+4u*i,values[i]);
    badger_bytes_copy(trace+128u,last_packet,sizeof(last_packet));
    badger_bytes_store_u32(trace+228u,link.last_press);
    badger_bytes_store_u32(trace+232u,link.peer_presses);
    const msg_tx_time_t *t=&link.last_timing;
    const msg_rx_time_t *r=&link.receipt;
    uint32_t timing[] = {
        t->token,t->sequence,t->detected_at,t->queued_at,t->first_tx_at,t->last_tx_at,
        t->first_done_at,t->last_done_at,t->completed_at,t->attempts,t->outcome,t->flags,
        r->token,r->session,r->sequence,r->received_at,r->ack_selected_at,r->ack_done_at,
        r->displayed_at,r->flags,r->duplicates,
        screen_ticks,screen_max,loop_max,clock_ticks,clock_frames,MSG_ACK_DELAY_US,
        MSG_RETRY_BASE,!MSG_BASELINE,now_us(),scheduled,link.active?link.timing.token:0u,
        paint_ticks,strip_max,
    };
    for (uint32_t i=0u; i<sizeof(timing)/sizeof(timing[0]); ++i)
        badger_bytes_store_u32(trace+256u+4u*i,timing[i]);
    usb_report_header(report,0x46363255u,USB_REPORT_BUILD_ID,nonce,3u);
    badger_bytes_store_u32(report+32u,request_error);
    badger_bytes_store_u32(report+36u,link.tx_data);
    badger_bytes_store_u32(report+40u,link.rx_data);
    badger_bytes_store_u32(report+44u,link.rx_ack);
    badger_bytes_store_u32(report+48u,link.failed);
    badger_bytes_store_u32(report+52u,restarts);
    badger_bytes_store_u32(report+56u,MSG_ROLE);
    badger_bytes_store_u32(report+60u,diag_crc32(report,60u));
    live_usb_probe_finish(&usb,generation,report);
    live_usb_trace_finish(&usb,generation,trace);
}

void bare_main(void) {
    uint32_t id[2], random[4];
    char serial[17], half[9];
    if (badger_schedule_bootsel(8000u) ||
        (!MSG_BASELINE && nametag_clock_reference()) || badger_rom_chip_id(id) ||
        badger_rom_boot_random(random)) while (1) __asm__ volatile("wfi");
    diag_hex(half,id[1],8u);
    badger_bytes_copy((uint8_t *)serial,(uint8_t *)half,8u);
    diag_hex(half,id[0],8u);
    badger_bytes_copy((uint8_t *)serial+8u,(uint8_t *)half,8u); serial[16]=0;
    msg_init(&link,MSG_ROLE,random[0]?random[0]:1u);
    live_usb_control_init(&usb,USB_REPORT_BUILD_ID,serial);
    if (live_usb_hw_init()) while (1) __asm__ volatile("wfi");
    watchdog_at=now_us();
    if (!badger_display_init_serviced(service,0))
        while (1) __asm__ volatile("wfi");
    radio_boot();
    heartbeat_at=now_us();
    (void)msg_enqueue(&link,0u,MSG_HEARTBEAT,0u,now_us());
    for (;;) {
        (void)service(0);
        if (usb_error || usb.reboot_requested) {
            wifi_wire_power_off(wifi_board_wire());
            uint32_t start=now_us();
            while (now_us()-start<10000u) (void)live_usb_hw_poll(&usb);
            live_usb_hw_disconnect();
            (void)badger_schedule_bootsel(10u);
            while (1) __asm__ volatile("wfi");
        }
        uint32_t now=now_us();
        if (poll_at && now-poll_at>loop_max) loop_max=now-poll_at;
        poll_at=now;
        msg_buttons(&link,buttons_read(),now);
        usb_snapshot();
        if (scheduled && now_us()-scheduled_at<0x80000000u) {
            (void)msg_enqueue(&link,scheduled_buttons,MSG_USB,scheduled_token,now_us());
            scheduled=0u;
        }
        if (!radio_ready) {
            if (now-retry_boot_at<0x80000000u) radio_boot();
        } else {
            /* The loader's six-second boot budget becomes a per-loop deadline. */
            radio.elapsed_us=0u; radio.serviced_at=now; radio.budget_us=2000000u;
            uint32_t size;
            if (!clean_receive(&radio,packet,&size)) radio_fault();
            else if (size) {
                ++received;
                if (size==18u+MSG_MPDU &&
                    clean_ht_rx_metadata(packet[15],packet[16],packet[17],
                                         MSG_MPDU,MSG_CHANNEL,MSG_MCS)) {
                    int result=msg_receive(&link,packet+18u,MSG_MPDU,now_us());
                    if (!result) ++rejected;
                    else if (result==1 && packet[18u+52u]!=MSG_HEARTBEAT)
                        badger_bytes_copy(last_packet,packet,sizeof(last_packet));
                }
            }
            if (radio_ready) {
                if (now-heartbeat_at>=3000000u+170000u*MSG_ROLE &&
                    !link.active && !link.count) {
                    (void)msg_enqueue(&link,link.buttons,MSG_HEARTBEAT,0u,now);
                    heartbeat_at=now;
                }
                uint8_t frame[MSG_MPDU];
                if (msg_next(&link,now_us(),frame)) {
                    if (!transmit(frame)) radio_fault();
                    else msg_transmitted(&link,frame,now_us());
                }
                uint32_t check=now_us();
                uint32_t activity=link.peer_at &&
                    check-link.peer_at<check-boot_at?link.peer_at:boot_at;
                if (radio_ready && check-activity>=15000000u) {
                    radio.error=CLEAN_TIMEOUT; radio.actual=link.rx_data;
                    radio_fault();
                }
            }
        }
        screen();
    }
}

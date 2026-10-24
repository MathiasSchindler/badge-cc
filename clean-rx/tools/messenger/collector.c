#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "../../messenger/link.h"
#include "build_id.h"
#include "../../../src/host/tools/usb_live_transport.h"

static libusb_device_handle *open_badge(libusb_context *context, const char *serial) {
    if (strlen(serial)!=16u || strspn(serial,"0123456789ABCDEF")!=16u) {
        fputs("Expected a 16-digit uppercase badge serial\n",stderr); return NULL;
    }
    libusb_device_handle *device=find_badge(context,serial);
    if (!device) { fputs("Serial-matched messenger badge not found\n",stderr); return NULL; }
    int configuration, status=libusb_get_configuration(device,&configuration);
    if (!status && configuration!=1) status=libusb_set_configuration(device,1);
    if (!status) status=libusb_claim_interface(device,0);
    uint8_t info[12];
    if (status || !transfer(device,0xc0u,LIVE_USB_INFO,info,sizeof(info)) ||
        memcmp(info,"U26L\1\0\100\0",8u) ||
        usb_report_u32(info+8u)!=USB_REPORT_BUILD_ID) {
        fprintf(stderr,"Messenger USB claim/protocol/build mismatch: %d\n",status);
        libusb_close(device); return NULL;
    }
    return device;
}

static int snapshot(libusb_device_handle *device, uint8_t trace[WIFI_TRACE_SIZE],
                    uint8_t inject, uint8_t buttons, uint32_t token) {
    static uint32_t request_id;
    uint8_t nonce[16]={0}, request[64], report[64];
    nonce[0]=inject; nonce[1]=buttons;
    badger_bytes_store_u32(nonce+2u,token);
    badger_bytes_store_u32(nonce+6u,++request_id);
    usb_report_request_for_test(request,USB_REPORT_BUILD_ID,nonce,3u);
    if (!transfer(device,0x40u,LIVE_USB_FW_SUBMIT,request,sizeof(request))) return 0;
    int got=0;
    for (uint32_t i=0u; i<1000u; ++i) {
        int n=libusb_control_transfer(device,0xc0u,LIVE_USB_FW_REPORT,0u,0u,
                                     report,sizeof(report),1000u);
        if (n==sizeof(report)) { got=1; break; }
        if (n!=LIBUSB_ERROR_PIPE) { fprintf(stderr,"Messenger report read: %d\n",n); return 0; }
        usleep(10000u);
    }
    if (!got || usb_report_u32(report)!=0x46363255u ||
        report[4]!=1u || report[5] || report[6]!=64u || report[7] ||
        usb_report_u32(report+8u)!=USB_REPORT_BUILD_ID ||
        usb_report_u32(report+12u)!=3u || memcmp(report+16u,nonce,16u) ||
        usb_report_u32(report+60u)!=diag_crc32(report,60u)) {
        fprintf(stderr,"Invalid or missing messenger snapshot: got=%d request=%u "
                "build=%08x expected=%08x nonce_match=%d crc_match=%d\n",
                got,request_id,got?usb_report_u32(report+8u):0u,USB_REPORT_BUILD_ID,
                got && !memcmp(report+16u,nonce,16u),
                got && usb_report_u32(report+60u)==diag_crc32(report,60u));
        return 0;
    }
    for (uint32_t page=0u; page<WIFI_TRACE_PAGES; ++page) {
        uint8_t bytes[64];
        int n=libusb_control_transfer(device,0xc0u,LIVE_USB_WIFI_TRACE,(uint16_t)page,
                                     0u,bytes,sizeof(bytes),1000u);
        if (n!=sizeof(bytes) ||
            wifi_trace_page_decode(trace,bytes,sizeof(bytes),report,(uint8_t)page)!=USB_RECORD_OK) {
            fprintf(stderr,"Messenger trace page %u invalid: %d\n",page,n); return 0;
        }
    }
    if (usb_report_u32(report+32u) || usb_report_u32(trace)!=MSG_TRACE_MAGIC ||
        usb_report_u32(trace+4u)!=usb_report_u32(report+56u)) {
        fputs("Messenger command rejected or trace layout mismatch\n",stderr); return 0;
    }
    return 1;
}

static void print_status(const char *serial, const uint8_t *t) {
    printf("{\"serial\":\"%s\",\"role\":%u,\"session\":%u,\"tx_data\":%u,"
           "\"tx_ack\":%u,\"rx_unique\":%u,\"acked\":%u,\"duplicates\":%u,"
           "\"failed\":%u,\"queue_drops\":%u,\"radio_boots\":%u,\"last_fault\":%u,"
           "\"local_buttons\":%u,\"peer_buttons\":%u,\"peer_age_ms\":%u,"
           "\"last_event_origin\":%u,\"last_event_token\":%u,\"last_event_buttons\":%u,"
           "\"last_acked_token\":%u,\"radio_ready\":%u,\"raw_rx\":%u,"
           "\"fault_actual\":%u,\"fault_address\":%u,\"last_press\":%u,\"peer_presses\":%u}\n",
           serial,usb_report_u32(t+4),usb_report_u32(t+8),
           usb_report_u32(t+20),usb_report_u32(t+24),usb_report_u32(t+28),
           usb_report_u32(t+32),usb_report_u32(t+36),usb_report_u32(t+40),
           usb_report_u32(t+44),usb_report_u32(t+48),usb_report_u32(t+52),
           usb_report_u32(t+56),usb_report_u32(t+60),usb_report_u32(t+92),
           usb_report_u32(t+72),usb_report_u32(t+76),usb_report_u32(t+124),
           usb_report_u32(t+88),usb_report_u32(t+104),usb_report_u32(t+96),
           usb_report_u32(t+112),usb_report_u32(t+116),
           usb_report_u32(t+228),usb_report_u32(t+232));
    fflush(stdout);
}

static uint64_t milliseconds(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) { perror("clock_gettime"); exit(1); }
    return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;
}

static int exchange(libusb_device_handle *sender, libusb_device_handle *receiver,
                    uint8_t buttons, uint32_t token, uint8_t inject) {
    uint8_t a[WIFI_TRACE_SIZE], b[WIFI_TRACE_SIZE];
    if (!snapshot(sender,a,inject,buttons,token)) return 0;
    uint32_t session=usb_report_u32(a+8u), role=usb_report_u32(a+4u);
    uint64_t start=milliseconds();
    int delivered=0, acked=0;
    while (milliseconds()-start<12000u) {
        if (!snapshot(sender,a,0u,0u,0u) || !snapshot(receiver,b,0u,0u,0u)) return 0;
        if (usb_report_u32(b+76u)==token && usb_report_u32(b+72u)==MSG_USB) {
            msg_frame_t m;
            const uint8_t *p=b+128u;
            if (!clean_ht_rx_metadata(p[15],p[16],p[17],MSG_MPDU,MSG_CHANNEL,MSG_MCS) ||
                !msg_decode(&m,p+18u,MSG_MPDU) || m.kind!=MSG_STATE ||
                m.session!=session || m.role!=role || m.buttons!=buttons || m.token!=token ||
                m.origin!=MSG_USB || usb_report_u32(b+124u)!=buttons) {
                fputs("HT20 metadata/full-body/FCS/token mismatch\n",stderr); return 0;
            }
            delivered=1;
        }
        if (usb_report_u32(a+88u)==token && usb_report_u32(a+84u)==MSG_USB) acked=1;
        if (delivered && acked) {
            printf("{\"test\":\"ht20-button-exchange\",\"sender_role\":%u,"
                   "\"buttons\":%u,\"token\":%u,\"delivered\":true,\"acked\":true,"
                   "\"elapsed_ms\":%llu}\n",role,buttons,token,
                   (unsigned long long)(milliseconds()-start));
            fflush(stdout);
            return 1;
        }
        usleep(50000u);
    }
    fprintf(stderr,"HT20 exchange timeout: token=%u delivered=%d acked=%d\n",token,delivered,acked);
    print_status("sender",a); print_status("receiver",b);
    return 0;
}

static uint32_t word(const uint8_t *trace, uint32_t offset) {
    return usb_report_u32(trace+offset);
}

static int latency_sample(libusb_device_handle *sender, libusb_device_handle *receiver,
                          uint8_t buttons, uint32_t token) {
    uint8_t a[WIFI_TRACE_SIZE],b[WIFI_TRACE_SIZE];
    if (!snapshot(sender,a,2u,buttons,token)) return -1;
    uint32_t session=word(a,8u),role=word(a,4u);
    uint64_t start=milliseconds();
    /* The event starts 200 ms after arming; no collection during initial exchange. */
    usleep(500000u);
    int terminal=0,rx=0;
    do {
        if (!snapshot(sender,a,0u,0u,0u) || !snapshot(receiver,b,0u,0u,0u)) return -1;
        terminal=word(a,256u)==token && word(a,296u)!=0u;
        rx=word(b,304u)==token && word(b,308u)==session;
        if (terminal && (word(a,296u)==2u || (rx && (word(b,332u)&15u)==15u))) break;
        usleep(100000u);
    } while (milliseconds()-start<8000u);
    int acked=terminal && word(a,296u)==1u;
    if (rx) {
        msg_frame_t m;
        const uint8_t *p=b+128u;
        if (!clean_ht_rx_metadata(p[15],p[16],p[17],MSG_MPDU,MSG_CHANNEL,MSG_MCS) ||
            !msg_decode(&m,p+18u,MSG_MPDU) || m.token!=token || m.session!=session ||
            m.sequence!=word(b,312u) || m.role!=role || m.buttons!=buttons ||
            m.kind!=MSG_STATE || m.origin!=MSG_USB ||
            (terminal && m.sequence!=word(a,260u))) {
            fputs("Latency event/body/timing correlation mismatch\n",stderr); return -1;
        }
    }
    printf("{\"test\":\"ht20-latency\",\"token\":%u,\"sender_role\":%u,"
           "\"sender_session\":%u,\"delivered\":%s,\"acked\":%s,"
           "\"terminal\":%s,\"tx\":[",token,role,session,rx?"true":"false",
           acked?"true":"false",terminal?"true":"false");
    for (uint32_t i=0u; i<12u; ++i) printf("%s%u",i?",":"",word(a,256u+4u*i));
    printf("],\"rx\":[");
    for (uint32_t i=0u; i<9u; ++i) printf("%s%u",i?",":"",word(b,304u+4u*i));
    printf("],\"sender_boots\":%u,\"receiver_boots\":%u,"
           "\"sender_failed\":%u,\"receiver_failed\":%u,"
           "\"sender_screen_ticks\":%u,\"receiver_screen_ticks\":%u,"
           "\"sender_loop_max_ticks\":%u,\"receiver_loop_max_ticks\":%u,"
           "\"sender_paint_ticks\":%u,\"receiver_paint_ticks\":%u,"
           "\"sender_strip_max_ticks\":%u,\"receiver_strip_max_ticks\":%u,"
           "\"rx_packet_hex\":\"",
           word(a,48u),word(b,48u),word(a,40u),word(b,40u),
           word(a,340u),word(b,340u),word(a,348u),word(b,348u),
           word(a,384u),word(b,384u),word(a,388u),word(b,388u));
    if (rx) for (uint32_t i=0u; i<98u; ++i) printf("%02x",b[128u+i]);
    puts("\"}");
    fflush(stdout);
    return acked && rx ? 1:0;
}

static int latency_test(libusb_device_handle *a, libusb_device_handle *b, uint32_t count) {
    uint8_t first[WIFI_TRACE_SIZE],second[WIFI_TRACE_SIZE];
    if (!snapshot(a,first,3u,0u,0u) || !snapshot(b,second,3u,0u,0u)) return 0;
    if (word(first,4u)==word(second,4u)) { fputs("Latency test needs distinct roles\n",stderr); return 0; }
    for (uint32_t i=0u; i<2u; ++i) {
        const uint8_t *t=i?second:first;
        uint32_t ticks=word(t,352u),frames=word(t,356u);
        if (!frames || !ticks ||
            (word(t,368u) && (ticks<frames*990u || ticks>frames*1010u))) {
            fprintf(stderr,"Missing timer calibration: role=%u ticks=%u frames=%u\n",
                    word(t,4u),ticks,frames); return 0;
        }
        printf("{\"test\":\"ht20-clock\",\"build\":%u,\"role\":%u,"
               "\"ticks\":%u,\"sof_frames\":%u,\"ack_delay_ticks\":%u,"
               "\"retry_base_ticks\":%u,\"partial_display\":%u}\n",
               USB_REPORT_BUILD_ID,word(t,4u),ticks,frames,word(t,360u),
               word(t,364u),word(t,368u));
    }
    uint64_t start=milliseconds();
    while ((!word(first,104u) || !word(second,104u) ||
            word(first,92u)>6000u || word(second,92u)>6000u) &&
           milliseconds()-start<35000u) {
        usleep(500000u);
        if (!snapshot(a,first,0u,0u,0u) || !snapshot(b,second,0u,0u,0u)) return 0;
    }
    print_status("start-A",first); print_status("start-B",second);
    uint32_t passed=0u,seed=(uint32_t)milliseconds();
    for (uint32_t i=0u; i<count; ++i) {
        uint32_t token=seed+i;
        if (!token) token=0xffffffffu;
        int result=latency_sample(i&1u?b:a,i&1u?a:b,(uint8_t)(i%6u==5u?0u:1u<<(i%5u)),token);
        if (result<0) return 0;
        passed+=(uint32_t)result;
    }
    if (!snapshot(a,first,0u,0u,0u) || !snapshot(b,second,0u,0u,0u)) return 0;
    print_status("end-A",first); print_status("end-B",second);
    printf("{\"test\":\"ht20-latency-total\",\"requested\":%u,\"passed\":%u,"
           "\"failed\":%u}\n",count,passed,count-passed);
    return passed==count;
}

int main(int argc, char **argv) {
    if (argc<3 || (strcmp(argv[1],"status") && strcmp(argv[1],"press") &&
                  strcmp(argv[1],"pair-test") && strcmp(argv[1],"bootsel") &&
                  strcmp(argv[1],"latency-test")) ||
        ((!strcmp(argv[1],"status") || !strcmp(argv[1],"bootsel")) && argc!=3) ||
        ((!strcmp(argv[1],"press") || !strcmp(argv[1],"pair-test")) && argc!=4) ||
        (!strcmp(argv[1],"latency-test") && argc!=5)) {
        fputs("Usage: collector status SERIAL | press SERIAL MASK:0-31 | "
              "pair-test SERIAL_A SERIAL_B | latency-test SERIAL_A SERIAL_B COUNT:1-256 | "
              "bootsel SERIAL\n",stderr); return 2;
    }
    libusb_context *context=NULL;
    int status=libusb_init(&context);
    if (status) { fprintf(stderr,"libusb_init: %s\n",libusb_error_name(status)); return 1; }
    libusb_device_handle *a=open_badge(context,argv[2]), *b=NULL;
    int ok=a!=NULL;
    uint8_t trace[WIFI_TRACE_SIZE];
    if (ok && !strcmp(argv[1],"latency-test")) {
        char *end;
        unsigned long count=strtoul(argv[4],&end,10);
        if (!argv[4][0] || *end || count<1u || count>256u || !strcmp(argv[2],argv[3])) {
            fputs("Invalid latency count or repeated serial\n",stderr); ok=0;
        } else {
            b=open_badge(context,argv[3]);
            ok=b && latency_test(a,b,(uint32_t)count);
        }
    } else if (ok && !strcmp(argv[1],"pair-test")) {
        b=open_badge(context,argv[3]);
        uint8_t other[WIFI_TRACE_SIZE];
        ok=b && strcmp(argv[2],argv[3]) && snapshot(a,trace,0u,0u,0u) &&
            snapshot(b,other,0u,0u,0u) && usb_report_u32(trace+4u)!=usb_report_u32(other+4u);
        uint32_t tokens[24];
        FILE *random=fopen("/dev/urandom","rb");
        if (!random) { perror("/dev/urandom"); ok=0; }
        else {
            if (fread(tokens,sizeof(tokens),1u,random)!=1u) { fputs("Random token read failed\n",stderr); ok=0; }
            if (fclose(random)) { perror("close random"); ok=0; }
        }
        for (uint32_t i=0u; ok && i<16u; ++i)
            ok=exchange(i&1u?b:a,i&1u?a:b,(uint8_t)(i>=14u?0u:1u<<(i%5u)),
                        tokens[i]?tokens[i]:i+1u,1u);
        for (uint32_t i=16u; ok && i<24u; i+=2u) {
            uint8_t first=(uint8_t)(i==22u?0u:1u<<(i%5u));
            uint8_t second=(uint8_t)(i==22u?0u:1u<<((i+1u)%5u));
            uint32_t ta=tokens[i]?tokens[i]:i+1u, tb=tokens[i+1u]?tokens[i+1u]:i+2u;
            ok=snapshot(a,trace,1u,first,ta) && snapshot(b,other,1u,second,tb) &&
                exchange(a,b,first,ta,0u) && exchange(b,a,second,tb,0u);
        }
        if (ok) ok=snapshot(a,trace,0u,0u,0u) && snapshot(b,other,0u,0u,0u);
        if (ok) { print_status(argv[2],trace); print_status(argv[3],other); }
        if (!ok) fputs("Pair test FAILED (requires distinct roles and all24 exchanges)\n",stderr);
    } else if (ok && !strcmp(argv[1],"bootsel")) {
        ok=transfer(a,0x40u,LIVE_USB_BOOTSEL,NULL,0u);
    } else if (ok) {
        uint8_t inject=(uint8_t)!strcmp(argv[1],"press");
        unsigned long buttons=0u;
        if (inject) {
            char *end;
            buttons=strtoul(argv[3],&end,10);
            if (!argv[3][0] || *end || buttons>31u) { fputs("Mask must be0..31\n",stderr); ok=0; }
        }
        uint32_t token=(uint32_t)milliseconds();
        if (ok) ok=snapshot(a,trace,inject,(uint8_t)buttons,token);
        if (ok) print_status(argv[2],trace);
    }
    if (b) { libusb_release_interface(b,0); libusb_close(b); }
    if (a) { libusb_release_interface(a,0); libusb_close(a); }
    libusb_exit(context);
    return ok?0:1;
}

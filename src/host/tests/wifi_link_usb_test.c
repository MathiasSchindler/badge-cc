#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../usb/live_usb.h"
#include "../../radio/wifi_link_report.h"
#include "../../tools/diagnostics_logic.h"
#ifdef LIVE_USB_UDP
#include "../../radio/wifi_udp_report.h"
#endif

static live_usb_control_t usb;
static const uint32_t build = 123u;
static uint8_t nonce[16] = {4u};

static void setup(uint8_t type, uint8_t request, uint16_t value, uint16_t length) {
    const uint8_t packet[8] = {type,request,(uint8_t)value,(uint8_t)(value>>8u),
                               0u,0u,(uint8_t)length,(uint8_t)(length>>8u)};
    live_usb_control_setup(&usb,packet);
}

static void configure(void) {
    setup(0u,5u,12u,0u);
    live_usb_control_complete(&usb,1u,NULL,0u);
    setup(0u,9u,1u,0u);
    live_usb_control_complete(&usb,1u,NULL,0u);
    assert(usb.configured);
}

int main(void) {
    live_usb_control_init(&usb,build,"0123456789ABCDEF");
    configure();
    setup(0x40u,LIVE_USB_WIFI_SUBMIT,0u,64u);
    assert(usb.phase==USB_PHASE_STALL);
    setup(0x40u,LIVE_USB_CLEAN_SECRET,0u,64u);
    assert(usb.phase==USB_PHASE_STALL);
    setup(0x40u,LIVE_USB_FW_SUBMIT,0u,64u);
    assert(usb.phase==USB_PHASE_STALL);
    uint8_t request[64], bytes[64], received_nonce[16];
    usb_report_request_for_test(request,build,nonce,WIFI_LINK_TEST_ID);
    setup(0x40u,LIVE_USB_LINK_AP,0u,64u);
    live_usb_control_complete(&usb,0u,request,64u);
    assert(live_usb_probe_take(&usb,received_nonce)==0u);
    live_usb_control_complete(&usb,1u,NULL,0u);
    uint32_t generation=live_usb_probe_take(&usb,received_nonce);
    assert(generation!=0u && usb.probe_role==WIFI_LINK_AP && memcmp(nonce,received_nonce,16u)==0);
    wifi_link_report_t report={.role=WIFI_LINK_AP,.stage=WIFI_LINK_FIRMWARE};
    wifi_link_report_encode(bytes,build,nonce,&report);
    live_usb_link_publish(&usb,generation,bytes);
    setup(0xc0u,LIVE_USB_LINK_REPORT,0u,64u);
    assert(usb.phase==USB_PHASE_DATA_IN);
    uint8_t snapshot[64]; memcpy(snapshot,usb.data,64u);
    report.stage=WIFI_LINK_CONFIGURE;
    wifi_link_report_encode(bytes,build,nonce,&report);
    live_usb_link_publish(&usb,generation,bytes);
    assert(memcmp(snapshot,usb.data,64u)==0 && memcmp(snapshot,usb.report,64u)!=0);
    live_usb_control_complete(&usb,1u,NULL,64u);
    live_usb_control_complete(&usb,0u,NULL,0u);
    setup(0xc0u,LIVE_USB_REPORT,0u,64u);
    assert(usb.phase==USB_PHASE_STALL);
    wifi_link_report_t decoded;
    assert(wifi_link_report_decode(bytes,build,nonce,WIFI_LINK_AP,&decoded)==USB_RECORD_OK);
    uint8_t detail[WIFI_TRACE_SIZE]={0x4cu,0x4eu,0x4bu,0x31u};
    live_usb_link_details(&usb,generation,detail);
    setup(0xc0u,LIVE_USB_WIFI_TRACE,0u,64u);
    uint8_t detail_read[WIFI_TRACE_SIZE]={0};
    assert(wifi_trace_page_decode(detail_read,usb.data,64u,bytes,0u)==USB_RECORD_OK);
    assert(memcmp(detail_read,detail,24u)==0);
    live_usb_control_complete(&usb,1u,NULL,64u);
    live_usb_control_complete(&usb,0u,NULL,0u);
    assert(wifi_link_report_decode(bytes,build,nonce,WIFI_LINK_STATION,&decoded)==USB_RECORD_PAYLOAD);
    for (unsigned i=0u;i<64u;++i) {
        bytes[i]^=1u; assert(wifi_link_report_decode(bytes,build,nonce,0u,&decoded)!=USB_RECORD_OK); bytes[i]^=1u;
    }
    report.stage=WIFI_LINK_CONNECTED;
    wifi_link_report_encode(bytes,build,nonce,&report);
    assert(wifi_link_report_decode(bytes,build,nonce,0u,&decoded)==USB_RECORD_PAYLOAD);
    report.peer_count=1u; report.mac[0]=2u; report.peer[0]=2u; report.peer[5]=1u; report.authorized=1u;
    wifi_link_report_encode(bytes,build,nonce,&report);
    assert(wifi_link_report_decode(bytes,build,nonce,0u,&decoded)==USB_RECORD_OK);
    wifi_link_snapshot_t visible={0};
    wifi_link_snapshot_begin(&visible,&report);
    report.stage=WIFI_LINK_WAIT_PEER;
    report.peer_count=0u;
    report.authorized=0u;
    wifi_link_report_encode(bytes,build,nonce,wifi_link_snapshot_view(&visible,&report));
    assert(wifi_link_report_decode(bytes,build,nonce,0u,&decoded)==USB_RECORD_OK);
    assert(decoded.stage==WIFI_LINK_CONNECTED && decoded.peer_count==1u && decoded.authorized);
    wifi_link_snapshot_end(&visible);
    wifi_link_report_encode(bytes,build,nonce,wifi_link_snapshot_view(&visible,&report));
    assert(wifi_link_report_decode(bytes,build,nonce,0u,&decoded)==USB_RECORD_OK);
    assert(decoded.stage==WIFI_LINK_WAIT_PEER && decoded.peer_count==0u && !decoded.authorized);
    setup(0x40u,LIVE_USB_LINK_STATION,0u,64u);
    live_usb_link_publish(&usb,generation,bytes);
    assert(!usb.report_valid);
#ifdef LIVE_USB_UDP
    setup(0x40u,LIVE_USB_LINK_AP,0u,64u);
    usb_report_request_for_test(request,build,nonce,WIFI_LINK_TEST_ID);
    live_usb_control_complete(&usb,0u,request,64u);
    live_usb_control_complete(&usb,1u,NULL,0u);
    generation=live_usb_probe_take(&usb,received_nonce);
    setup(0x40u,LIVE_USB_UDP_START,0u,64u);
    live_usb_control_complete(&usb,0u,request,64u);
    assert(live_usb_udp_take(&usb)==0u);
    live_usb_control_complete(&usb,1u,NULL,0u);
    uint32_t epoch=live_usb_udp_take(&usb);
    assert(epoch!=0u);
    wifi_udp_t net={.stage=WIFI_UDP_ECHO,.arp_ready=1u};
    wifi_udp_metrics_t metrics={.calibration_ticks=60000u,.calibration_frames=128u};
    uint8_t bench_report[64],bench_detail[512],frozen[64];
    wifi_udp_report_encode(bench_report,build,nonce,&net,&metrics);
    wifi_udp_detail_encode(bench_detail,&net,&metrics);
    live_usb_udp_publish(&usb,generation,epoch,bench_report,bench_detail,0u);
    setup(0xc0u,LIVE_USB_UDP_FREEZE,0u,64u);
    assert(usb.phase==USB_PHASE_STALL);
    net.stage=WIFI_UDP_DONE; net.sent=net.lost=100u;
    wifi_udp_report_encode(bench_report,build,nonce,&net,&metrics);
    wifi_udp_detail_encode(bench_detail,&net,&metrics);
    live_usb_udp_publish(&usb,generation,epoch,bench_report,bench_detail,1u);
    setup(0xc0u,LIVE_USB_UDP_FREEZE,0u,64u);
    assert(usb.phase==USB_PHASE_DATA_IN && usb.udp_frozen);
    memcpy(frozen,usb.data,64u);
    live_usb_control_complete(&usb,1u,NULL,64u);
    live_usb_control_complete(&usb,0u,NULL,0u);
    bench_report[44]^=1u;
    live_usb_udp_publish(&usb,generation,epoch,bench_report,bench_detail,1u);
    assert(memcmp(usb.udp_report,frozen,64u)==0);
    uint8_t collected[512]={0};
    for(uint8_t page=0u;page<WIFI_UDP_DETAIL_PAGES;++page) {
        setup(0xc0u,LIVE_USB_UDP_PAGE,page,64u);
        assert(wifi_udp_page_decode(collected,usb.data,frozen,page)==USB_RECORD_OK);
        live_usb_control_complete(&usb,1u,NULL,64u);
        live_usb_control_complete(&usb,0u,NULL,0u);
    }
    assert(memcmp(collected,bench_detail,512u)==0);
    setup(0x40u,LIVE_USB_UDP_START,0u,64u);
    request[16]^=1u;
    uint32_t crc=diag_crc32(request,60u);
    for(unsigned i=0u;i<4u;++i)request[60u+i]=(uint8_t)(crc>>(8u*i));
    live_usb_control_complete(&usb,0u,request,64u);
    assert(usb.phase==USB_PHASE_STALL && !usb.udp_valid);
    live_usb_udp_publish(&usb,generation,epoch,bench_report,bench_detail,1u);
    assert(!usb.udp_valid);
    usb_report_request_for_test(request,build,nonce,WIFI_LINK_TEST_ID);
    setup(0x40u,LIVE_USB_LINK_STATION,0u,64u);
#endif
    live_usb_link_details(&usb,generation,detail);
    assert(!usb.trace_valid);
    live_usb_control_complete(&usb,0u,request,64u);
    live_usb_control_complete(&usb,1u,NULL,0u);
    generation=live_usb_probe_take(&usb,received_nonce);
    assert(generation && usb.probe_role==WIFI_LINK_STATION);
#ifndef LIVE_USB_UDP
    setup(0x40u,LIVE_USB_LINK_OPEN_AP,0u,64u);
    live_usb_control_complete(&usb,0u,request,64u);
    live_usb_control_complete(&usb,1u,NULL,0u);
    assert(live_usb_probe_take(&usb,received_nonce) && usb.probe_role == WIFI_LINK_OPEN_AP);
    setup(0x40u,LIVE_USB_LINK_OPEN_STATION,0u,64u);
    live_usb_control_complete(&usb,0u,request,64u);
    live_usb_control_complete(&usb,1u,NULL,0u);
    assert(live_usb_probe_take(&usb,received_nonce) && usb.probe_role == WIFI_LINK_OPEN_STATION);
    setup(0x40u,LIVE_USB_LINK_WPA2_FIXTURE,0u,64u);
    live_usb_control_complete(&usb,0u,request,64u);
    live_usb_control_complete(&usb,1u,NULL,0u);
    assert(live_usb_probe_take(&usb,received_nonce) && usb.probe_role == WIFI_LINK_FIXTURE_AP);
#else
    setup(0x40u,LIVE_USB_LINK_WPA2_FIXTURE,0u,64u);
    assert(usb.action == USB_ACTION_STALL);
    setup(0x40u,LIVE_USB_LINK_OPEN_AP,0u,64u);
    assert(usb.action == USB_ACTION_STALL);
    setup(0x40u,LIVE_USB_LINK_OPEN_STATION,0u,64u);
    assert(usb.action == USB_ACTION_STALL);
#endif
    live_usb_control_reset(&usb);
    configure();
    live_usb_link_publish(&usb,generation,bytes);
    assert(!usb.report_valid);
    puts("wifi_link_usb: explicit roles, deferred work, coherent snapshots, CRC/nonce, stale rejection PASS");
    return 0;
}

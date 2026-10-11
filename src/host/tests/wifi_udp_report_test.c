#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../radio/wifi_udp_report.h"
#include "../../tools/diagnostics_logic.h"

int main(void) {
    wifi_udp_t net={.stage=WIFI_UDP_DONE,.arp_ready=1u,.sent=100u,.received=100u,.echoed=100u,
                    .rtt_min=1u,.rtt_max=100u,.rtt_sum=5050u,.last_rtt=100u};
    wifi_udp_metrics_t metrics={.calibration_ticks=60000u,.calibration_frames=128u,.elapsed_ticks=123456u};
    uint8_t nonce[16]={4u},report[64],detail[512],received[512]={0};
    for(unsigned i=0u;i<100u;++i)net.samples[i]=i+1u;
    wifi_udp_report_encode(report,123u,nonce,&net,&metrics);
    wifi_udp_summary_t summary;
    assert(wifi_udp_report_decode(report,123u,nonce,0u,&summary)==USB_RECORD_OK);
    for(unsigned i=0u;i<64u;++i) {
        report[i]^=1u;
        assert(wifi_udp_report_decode(report,123u,nonce,0u,&summary)!=USB_RECORD_OK);
        report[i]^=1u;
    }
    assert(wifi_udp_report_decode(report,123u,nonce,0u,&summary)==USB_RECORD_OK);
    wifi_udp_detail_encode(detail,&net,&metrics);
    assert(wifi_udp_detail_check(detail,&summary)==USB_RECORD_OK);
    for(uint8_t i=0u;i<WIFI_UDP_DETAIL_PAGES;++i) {
        uint8_t page[64];
        wifi_udp_page(page,detail,report,i);
        assert(wifi_udp_page_decode(received,page,report,i)==USB_RECORD_OK);
        for(unsigned j=0u;j<64u;++j) {
            page[j]^=1u;
            assert(wifi_udp_page_decode(received,page,report,i)!=USB_RECORD_OK);
            page[j]^=1u;
        }
    }
    assert(memcmp(detail,received,512u)==0);
    detail[WIFI_UDP_SAMPLE_OFFSET]^=1u;
    assert(wifi_udp_detail_check(detail,&summary)==USB_RECORD_PAYLOAD);
    detail[WIFI_UDP_SAMPLE_OFFSET]^=1u;
    detail[92]=1u;
    assert(wifi_udp_detail_check(detail,&summary)==USB_RECORD_RESERVED);
    detail[92]=0u;
    net.received=0u;net.lost=100u;net.rtt_min=net.rtt_max=net.rtt_sum=net.last_rtt=0u;
    memset(net.samples,0,sizeof(net.samples));
    wifi_udp_report_encode(report,123u,nonce,&net,&metrics);
    assert(wifi_udp_report_decode(report,123u,nonce,0u,&summary)==USB_RECORD_OK);
    wifi_udp_detail_encode(detail,&net,&metrics);
    assert(wifi_udp_detail_check(detail,&summary)==USB_RECORD_OK);
    net.lost=99u;
    wifi_udp_report_encode(report,123u,nonce,&net,&metrics);
    assert(wifi_udp_report_decode(report,123u,nonce,0u,&summary)==USB_RECORD_PAYLOAD);
    puts("wifi_udp_report: frozen pages, nonces/CRC, complete/loss counts and all RTT samples PASS");
    return 0;
}

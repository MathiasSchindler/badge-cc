#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cck_monitor.h"

int main(void) {
    uint8_t nonce[14]={1u,2u,3u,4u,5u,6u,7u,8u,10u,0u,0u,0u,0u,33u};
    uint8_t trace[CLEAN_PEER_TRACE_BYTES], packet[CLEAN_MSDU_BYTES+128u], frame[CLEAN_MSDU_BYTES+64u];
    for (uint32_t profile=4u; profile<=7u; profile+=3u) {
        nonce[13]=(uint8_t)((profile<<3u)|1u);
        for (uint32_t role=1u; role<=2u; ++role) {
            for (uint32_t index=0u; index<10u; ++index) {
                uint32_t size=clean_soak_frame(frame,nonce,(uint8_t)role,index);
                assert(size>6u);
                memset(packet,0,sizeof(packet)); memset(trace,0,sizeof(trace));
                memcpy(packet+40u,frame+6u,size-6u);
                clean_cck_monitor_packet(trace,nonce,packet,size+34u);
                assert(usb_report_u32(trace+56u)==1u);
                assert(usb_report_u32(trace+60u)==size-34u);
                assert(usb_report_u32(trace+64u+4u*(role-1u))==(1u<<index));
                assert(usb_report_u32(trace+80u)==40u);
                memset(trace,0,sizeof(trace));
                clean_cck_monitor_packet(trace,nonce,packet,size+33u);
                assert(usb_report_u32(trace+44u)==1u && !usb_report_u32(trace+56u));
                for (uint32_t at=0u; at<size-6u; ++at) {
                    packet[40u+at]^=1u; memset(trace,0,sizeof(trace));
                    clean_cck_monitor_packet(trace,nonce,packet,size+34u);
                    assert(!usb_report_u32(trace+56u));
                    packet[40u+at]^=1u;
                }
                nonce[5]^=1u; memset(trace,0,sizeof(trace));
                clean_cck_monitor_packet(trace,nonce,packet,size+34u);
                assert(!usb_report_u32(trace+56u)); nonce[5]^=1u;
            }
        }
    }
    memset(packet,0,sizeof(packet));
    for (uint32_t length=0u; length<100u; ++length) {
        memset(trace,0,sizeof(trace));
        clean_cck_monitor_packet(trace,nonce,packet,length);
        assert(!usb_report_u32(trace+56u));
    }
    puts("CCK vendor-monitor complete-MPDU, FCS, nonce and truncation tests passed");
    return 0;
}

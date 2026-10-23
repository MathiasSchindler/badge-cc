#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cck_monitor.h"

static void station_tests(void) {
    uint8_t nonce[14]={1u,2u,3u,4u,5u,6u,7u,8u};
    uint8_t trace[CLEAN_PEER_TRACE_BYTES], packet[WIFI_CONTROL_MAX];
    uint8_t frame[CLEAN_PEER_ETHERNET_STORAGE];
    static const uint32_t sizes[]={62u,1514u,1562u,1606u,2014u};
    for (uint32_t rate=1u;rate<=2u;++rate) {
        for (uint32_t size=0u;size<sizeof(sizes)/sizeof(sizes[0]);++size) {
            assert(clean_peer_cck_configure(nonce,rate,sizes[size]));
            memset(trace,0,sizeof(trace));
            for (uint8_t sequence=0u;sequence<CLEAN_PEER_FRAME_COUNT;++sequence) {
                clean_peer_ethernet(frame,nonce,sequence);
                uint32_t bytes=wifi_data_frame_diagnostic(packet,sequence,0u,frame,sizes[size]);
                assert(bytes && bytes<=sizeof(packet));
                wifi_control_packet_t decoded;
                const uint8_t *ethernet; uint32_t length; uint8_t interface;
                assert(wifi_control_parse_raw(packet,bytes,&decoded));
                assert(wifi_data_decode(&decoded,&interface,&ethernet,&length)==
                    (sizes[size]<=WIFI_ETHERNET_MAX));
                assert(wifi_data_decode_diagnostic(&decoded,&interface,&ethernet,&length));
                assert(interface==0u && length==sizes[size]);
                clean_cck_station_packet(trace,nonce,packet,bytes);
            }
            assert(usb_report_u32(trace+64u)==255u && usb_report_u32(trace+80u)==8u);
            assert(usb_report_u32(trace+76u)==sizes[size]);
            assert(!usb_report_u32(trace+68u) && !usb_report_u32(trace+84u));
            assert(!usb_report_u32(trace+88u));
            uint32_t bytes=(uint32_t)packet[0]|(uint32_t)packet[1]<<8u;
            clean_cck_station_packet(trace,nonce,packet,bytes);
            assert(usb_report_u32(trace+68u)==1u && usb_report_u32(trace+80u)==9u);
            for (uint32_t i=0u;i<sizes[size];++i) {
                packet[18u+i]^=1u; memset(trace,0,sizeof(trace));
                clean_cck_station_packet(trace,nonce,packet,bytes);
                assert(!usb_report_u32(trace+80u));
                packet[18u+i]^=1u;
            }
            for (uint32_t i=0u;i<bytes;++i) {
                memset(trace,0,sizeof(trace));
                clean_cck_station_packet(trace,nonce,packet,i);
                assert(!usb_report_u32(trace+80u) && usb_report_u32(trace+84u)==1u);
            }
            nonce[7]^=1u; memset(trace,0,sizeof(trace));
            clean_cck_station_packet(trace,nonce,packet,bytes);
            assert(!usb_report_u32(trace+80u) && usb_report_u32(trace+88u)==1u);
            nonce[7]^=1u;
            packet[17]=255u; memset(trace,0,sizeof(trace));
            clean_cck_station_packet(trace,nonce,packet,bytes);
            assert(!usb_report_u32(trace+80u));
            packet[17]=0u; packet[14]=0x10u; memset(trace,0,sizeof(trace));
            clean_cck_station_packet(trace,nonce,packet,bytes);
            assert(!usb_report_u32(trace+80u) && usb_report_u32(trace+84u)==1u);
            packet[14]=0x20u; packet[16]=2u; memset(trace,0,sizeof(trace));
            clean_cck_station_packet(trace,nonce,packet,bytes);
            assert(!usb_report_u32(trace+80u) && usb_report_u32(trace+84u)==1u);
        }
    }
    puts("CCK station: full Ethernet payload/identity/nonce, all sequences, duplicates, every-byte corruption/truncation, BDC and unchanged normal MTU pass (NOT RF)");
}

int main(void) {
    station_tests();
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

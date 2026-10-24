#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "network.h"
#include "../badge/station_network.h"

static clean_network_t a, b;
static const uint8_t ap[6] = {2u,1u,2u,3u,4u,5u};
static const uint8_t sta[6] = {2u,6u,7u,8u,9u,10u};
static const uint8_t session[6] = {1u,2u,3u,4u,5u,6u};
static void checksum(uint8_t *p) {
    p[40]=p[41]=0u;
    uint32_t s=73u;
    for (uint32_t i=26u;i<34u;i+=2u) s+=(uint32_t)p[i]*256u+p[i+1u];
    for (uint32_t i=34u;i<90u;i+=2u) s+=(uint32_t)p[i]*256u+p[i+1u];
    while (s>>16u) s=(s&65535u)+(s>>16u);
    s=(~s)&65535u;
    if (!s) s=65535u;
    p[40]=(uint8_t)(s>>8u); p[41]=(uint8_t)s;
}
static void init(uint32_t now, uint32_t seconds) {
    uint8_t empty[6] = {0};
    clean_network_init(&a,0u,ap,empty,empty,0u,seconds,now);
    clean_network_init(&b,1u,sta,ap,session,1u,seconds,now);
}
static void transfer(clean_network_t *from, clean_network_t *to, int duplicate) {
    uint32_t size;
    const uint8_t *p = clean_network_output(from,&size);
    if (!p) return;
    assert(clean_network_input(to,p,size));
    if (duplicate) assert(clean_network_input(to,p,size));
    clean_network_sent(from);
}
int main(void) {
    init(0u,3600u);
    for (uint32_t now=0u;now<3600000000u;now+=10000u) {
        clean_network_step(&a,now); clean_network_step(&b,now);
        transfer(&a,&b,0); transfer(&b,&a,0);
        assert(a.tx.count<=4u && a.rx.count<=4u && b.tx.count<=4u && b.rx.count<=4u);
    }
    assert(a.report.generated > 12000u && b.report.generated > 12000u);
    assert(a.report.generated==b.report.delivered && b.report.generated==a.report.delivered);
    assert(a.report.acknowledged==a.report.generated && b.report.acknowledged==b.report.generated);
    assert(a.report.tx_hash==b.report.rx_hash && b.report.tx_hash==a.report.rx_hash);
    assert(a.report.large_generated==a.report.generated/2u);
    assert(b.report.large_generated==b.report.generated/2u);
    assert(a.report.large_generated==b.report.large_delivered);
    assert(b.report.large_generated==a.report.large_delivered);
    assert(a.report.large_generated==a.report.large_acknowledged);
    assert(b.report.large_generated==b.report.large_acknowledged);
    assert(a.report.latency_samples==a.report.acknowledged);
    assert(b.report.latency_samples==b.report.acknowledged);
    assert(a.report.metrics_magic==CLEAN_NETWORK_METRICS_MAGIC);
    assert(a.report.ack_attempts[0]==a.report.acknowledged);
    assert(b.report.ack_attempts[0]==b.report.acknowledged);
    assert(a.report.latency_histogram[0]+a.report.latency_histogram[1]+
        a.report.latency_histogram[2]+a.report.latency_histogram[3]==a.report.acknowledged);
    assert(!a.report.lost && !b.report.lost && !a.report.corrupt && !b.report.corrupt);
    assert(a.report.idle_periods==180u && b.report.idle_periods==180u);
    a.report.state=b.report.state=2u;
    assert(clean_network_valid(&a.report) && clean_network_valid(&b.report));
    init(0xffff0000u,30u);
    b.report.latency_total_low=0xfffffff0u;
    for (uint32_t elapsed=0u;elapsed<2000000u;elapsed+=10000u) {
        uint32_t now=0xffff0000u+elapsed;
        clean_network_step(&a,now); clean_network_step(&b,now);
        transfer(&a,&b,0); transfer(&b,&a,1);
    }
    assert(a.report.duplicates && !a.report.corrupt && !b.report.corrupt);
    assert(a.report.delivered==b.report.generated && b.report.delivered==a.report.generated);
    assert(b.report.latency_total_high==1u);
    init(0u,30u);
    uint8_t benign[14]={0};
    for (uint32_t i=0u;i<4u;++i) assert(clean_network_input(&a,benign,sizeof(benign)));
    assert(!clean_network_input(&a,benign,sizeof(benign)) && a.report.rx_full==1u);
    for (uint32_t i=0u;i<4u;++i) clean_network_step(&a,i);
    assert(!a.rx.count && a.report.rx_high==4u);
    init(0u,30u);
    for (uint32_t now=0u;now<100000u;now+=10000u) {
        clean_network_step(&a,now); clean_network_step(&b,now);
        transfer(&a,&b,0); transfer(&b,&a,0);
    }
    clean_network_step(&b,300000u);
    uint32_t size;
    const uint8_t *packet=clean_network_output(&b,&size);
    assert(packet && size==90u);
    uint8_t corrupt[90], frame[CLEAN_NETWORK_MTU+32u], decoded[CLEAN_NETWORK_MTU];
    memcpy(corrupt,packet,size); corrupt[60]^=1u;
    assert(clean_network_input(&a,corrupt,size));
    clean_network_step(&a,310000u);
    assert(!a.report.bound && a.report.corrupt==1u);
    uint32_t length=clean_network_mpdu(frame,packet,size,sta,ap,17u);
    assert(length==118u && clean_frame_valid(frame+6u,length-6u));
    frame[7]=2u; memcpy(frame+10u,sta,6u); memcpy(frame+16u,ap,6u);
    memcpy(frame+22u,ap,6u);
    badger_bytes_store_u32(frame+length-4u,diag_crc32(frame+6u,length-10u));
    assert(clean_network_ethernet(decoded,frame+6u,length-6u,sta,ap)==90u);
    uint8_t original[90]; memcpy(original,packet,90u);
    init(0u,30u);
    clean_network_step(&a,1000000u);
    assert(a.report.service_max_us==1000000u);
    assert(!a.report.service_gaps && !a.report.event_count);
    memcpy(a.report.peer,sta,6u); a.neighbor=1u;
    assert(clean_network_input(&a,original,90u));
    clean_network_step(&a,1000001u);
    assert(a.report.bound && a.started==1000001u);
    assert(!a.report.service_gaps && !a.report.event_count);
    for (uint32_t i=0u;i<90u;++i) {
        memcpy(corrupt,original,90u); corrupt[i]^=1u;
        init(0u,30u);
        assert(clean_network_input(&a,corrupt,90u));
        clean_network_step(&a,1u);
        assert(!a.report.bound && !a.report.delivered);
    }
    init(0u,30u);
    memcpy(a.report.peer,sta,6u); a.neighbor=1u;
    memcpy(corrupt,original,90u); corrupt[70]^=1u; checksum(corrupt);
    assert(clean_network_input(&a,corrupt,90u)); clean_network_step(&a,1u);
    assert(!a.report.bound && !a.report.epoch && a.report.corrupt==1u);
    assert(clean_network_input(&a,original,90u)); clean_network_step(&a,2u);
    assert(a.report.bound && a.report.epoch==1u && a.report.delivered==1u);
    clean_network_sent(&a);
    memcpy(corrupt,original,90u);
    badger_bytes_store_u32(corrupt+56u,2u);
    for (uint32_t i=24u;i<48u;++i)
        corrupt[42u+i]=(uint8_t)(i*29u+session[i%6u]+
            badger_bytes_load_u32(original+60u)+2u+1u);
    checksum(corrupt);
    assert(clean_network_input(&a,corrupt,90u)); clean_network_step(&a,3u);
    assert(a.report.epoch==2u && a.report.delivered==2u && !a.rx.count);
    assert(clean_network_input(&a,original,90u)); clean_network_step(&a,4u);
    assert(a.report.stale==1u && a.report.epoch==2u && a.report.delivered==2u);
    init(0u,30u);
    clean_network_step(&b,0u);
    packet=clean_network_output(&b,&size); assert(packet && size==60u);
    uint8_t request[60]; memcpy(request,packet,60u);
    for (uint32_t i=0u;i<5u;++i) {
        assert(clean_network_input(&a,request,60u)); clean_network_step(&a,i);
    }
    assert(a.tx.count==4u && a.rx.count==1u && a.report.tx_full);
    clean_network_sent(&a); clean_network_step(&a,5u);
    assert(a.tx.count==4u && !a.rx.count && !a.report.rx_full);
    init(0u,30u); b.neighbor=1u;
    for (uint32_t now=0u;now<=1250000u;now+=250000u) {
        clean_network_step(&b,now);
        if (b.tx.count) clean_network_sent(&b);
    }
    assert(b.report.lost==1u && b.report.retries==3u);
    assert(b.report.event_count==8u && b.report.service_gaps==5u);
    assert((b.report.events[7u%CLEAN_NETWORK_EVENTS]>>30u)==2u);
    b.report.state=2u; assert(!clean_network_valid(&b.report));
    uint8_t maximum[CLEAN_NETWORK_MTU]={0};
    assert(!clean_network_input(&a,maximum,13u));
    assert(!clean_network_input(&a,maximum,CLEAN_NETWORK_MTU+1u));
    memcpy(maximum,ap,6u); memcpy(maximum+6u,sta,6u);
    length=clean_network_mpdu(frame,maximum,sizeof(maximum),sta,ap,0u);
    assert(length==CLEAN_NETWORK_MTU+28u && clean_frame_valid(frame+6u,length-6u));
    assert(!clean_network_mpdu(frame,maximum,13u,sta,ap,0u));
    assert(!clean_network_mpdu(frame,maximum,CLEAN_NETWORK_MTU+1u,sta,ap,0u));
    frame[7]=2u; memcpy(frame+10u,sta,6u); memcpy(frame+16u,ap,6u);
    badger_bytes_store_u32(frame+length-4u,diag_crc32(frame+6u,length-10u));
    assert(clean_network_ethernet(decoded,frame+6u,length-6u,sta,ap)==CLEAN_NETWORK_MTU);
    for (uint32_t i=0u;i<3u;++i) {
        uint32_t at=i==0u?7u:i==1u?10u:30u;
        frame[at]^=1u;
        badger_bytes_store_u32(frame+length-4u,diag_crc32(frame+6u,length-10u));
        assert(!clean_network_ethernet(decoded,frame+6u,length-6u,sta,ap));
        frame[at]^=1u;
    }
    init(0u,30u); clean_network_sent(&a);
    assert(a.report.error==2u);
    a.report.state=2u; assert(!clean_network_valid(&a.report));
    printf("B2 fixed engine storage: %zu bytes; TX/RX rings: %zu bytes each; no heap\n",
        sizeof(a),sizeof(a.tx));
    puts("B2: virtual 60-minute bidirectional run, idle, wrap, duplication, bounded queue pressure, "
         "ARP/IP/UDP checksums, corrupt binding, reconnect, stale epochs, retry exhaustion "
         "and maximum-sized Ethernet/MPDU framing pass; NOT RF");
}

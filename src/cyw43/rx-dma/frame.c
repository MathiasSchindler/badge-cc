#include "frame.h"

static uint32_t crc(const uint8_t *bytes,uint32_t n) {
    uint32_t value=0xffffffffu;
    for(uint32_t i=0u;i<n;++i) {
        value^=bytes[i];
        for(uint32_t bit=0u;bit<8u;++bit)value=(value>>1u)^(0xedb88320u&(0u-(value&1u)));
    }
    return ~value;
}
static int same(const uint8_t *a,const uint8_t *b,uint32_t n) {
    for(uint32_t i=0u;i<n;++i)if(a[i]!=b[i])return 0;
    return 1;
}
r3_frame_result_t r3_frame_match(const uint8_t *p,uint32_t n,const uint8_t expected[66],const uint8_t bssid[6]) {
    if(!p||!expected||!bssid)return R3_FRAME_ARGUMENT;
    if(n<28u||n>1716u)return R3_FRAME_EXTENT;
    uint32_t fc=(uint32_t)p[0]|(uint32_t)p[1]<<8u;
    /* Accept data/QoS-data from the AP only; no fragmentation, protection,
       four-address, HT control, A-MSDU or null-data interpretation. */
    if((fc&0xf7ffu)!=0x0208u&&(fc&0xf7ffu)!=0x0288u)return R3_FRAME_UNSUPPORTED;
    if(p[22]&15u)return R3_FRAME_UNSUPPORTED;
    uint32_t header=(fc&0x80u)?26u:24u;
    if(n!=header+8u+52u+4u)return R3_FRAME_EXTENT;
    if(header==26u&&(p[24]&0x80u))return R3_FRAME_UNSUPPORTED;
    uint32_t supplied=(uint32_t)p[n-4u]|(uint32_t)p[n-3u]<<8u|
                      (uint32_t)p[n-2u]<<16u|(uint32_t)p[n-1u]<<24u;
    if(crc(p,n-4u)!=supplied)return R3_FRAME_FCS;
    static const uint8_t snap[6]={0xaau,0xaau,3u,0u,0u,0u};
    if(!same(p+4u,expected,6u)||!same(p+10u,bssid,6u)||!same(p+16u,expected+6u,6u)||
       !same(p+header,snap,6u)||!same(p+header+6u,expected+12u,54u))return R3_FRAME_IDENTITY;
    return R3_FRAME_MATCH;
}

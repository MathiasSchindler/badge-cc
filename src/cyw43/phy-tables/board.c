#include "board.h"
#include "../../include/r0_hash.h"

static uint32_t get(const uint8_t *p,uint32_t bytes) {
    uint32_t value=0u;
    for(uint32_t i=0u;i<bytes;++i)value|=(uint32_t)p[i]<<(8u*i);
    return value;
}
static r3_board_error_t fail(r3_board_report_t *r,r3_board_error_t error) {
    r->error=error;return error;
}
r3_board_error_t r3_rx_board_load(r3_board_read_t read,void *context,r3_rx_board_t *b,r3_board_report_t *r) {
    static const uint32_t offsets[5]={0u,164u,468u,852u,878u};
    static const uint32_t sizes[5]={164u,304u,384u,26u,1164u};
    static const uint32_t crc[5]={0xedaa1b5eu,0xb11830b4u,0x60ac5f7au,0x58e7a824u,0xd4145ee1u};
    static const uint32_t common_offsets[5]={0x446u,0x546u,0x5c6u,0x6c6u,0x36eu};
    static const uint32_t common_counts[5]={128u,64u,256u,154u,108u};
    static const uint16_t channel1[31]={
        0x4b,0x176,0x13,0x3032,0x16,0x3,0x7e41,0x600,0xa00,0x3d9,0x27,0x25,0,0x991,0,0x96c,
        0,0,0x305,0xe75b,8,0x583f,0x1212,0x142f,0x1f1f,0x1f1f,0x5120,0x6127,0,0x6009,0x703f
    };
    if(b)b->ready=0u;
    if(!r)return R3_BOARD_ARGUMENT;
    volatile uint8_t *bytes=(volatile uint8_t *)r;
    for(uint32_t i=0u;i<sizeof(*r);++i)bytes[i]=0u;
    if(!b||!read)return fail(r,R3_BOARD_ARGUMENT);
    for(uint32_t pass=0u;pass<2u;++pass)for(uint32_t at=0u;at<R3_BOARD_PACKED_BYTES;at+=4u) {
        uint32_t value;
        r->address=R3_BOARD_ROM_START+at;
        if(read(context,r->address,&value)!=1)return fail(r,R3_BOARD_IO);
        ++r->reads;r->value=value;
        if(pass) {
            if(value!=get(b->packed+at,4u))return fail(r,R3_BOARD_CHANGED);
            r->bytes_verified+=4u;
        } else for(uint32_t i=0u;i<4u;++i)b->packed[at+i]=(uint8_t)(value>>(8u*i));
    }
    for(uint32_t i=0u;i<5u;++i) {
        r->checksums[i]=r0_crc32_update(0xffffffffu,b->packed+offsets[i],sizes[i])^0xffffffffu;
        if(r->checksums[i]!=crc[i])return fail(r,R3_BOARD_CHECKSUM);
    }
    for(uint32_t i=0u;i<56u;++i)b->tia16[i]=(uint16_t)get(b->packed+i*2u,2u);
    for(uint32_t i=0u;i<52u;++i)b->tia8[i]=b->packed[112u+i];
    for(uint32_t i=0u;i<76u;++i)b->gainidx[i]=get(b->packed+164u+i*4u,4u);
    for(uint32_t i=0u;i<96u;++i)b->gain[i]=get(b->packed+468u+i*4u,4u);
    for(uint32_t i=0u;i<26u;++i)b->gainval[i]=b->packed[852u+i];
    uint32_t out=0u;
    for(uint32_t i=0u;i<5u;++i) {
        b->common.data[i]=b->common_values+out;b->common.capacity[i]=common_counts[i];
        uint32_t width=i==2u?1u:2u;
        for(uint32_t j=0u;j<common_counts[i];++j)
            b->common_values[out++]=get(b->packed+common_offsets[i]+j*width,width);
    }
    for(uint32_t i=0u;i<31u;++i)b->channel_words[i]=channel1[i];
    b->gains=(r3_rx_gains_t){b->gainval,b->gain,b->gainidx,26u,96u,76u};
    b->core_flags=0u;b->xtal_hz=37400000u;
    b->channel=(r3_radio_channel_t){37400000u,1u,2412u,b->channel_words,31u};
    b->agc=(r3_rx_agc_config_t){0u,0u,1u,187u,181u};
    b->ready=1u;return R3_BOARD_OK;
}

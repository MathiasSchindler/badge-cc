#include "prepare.h"
static void clear(void *memory,uint32_t size) {
    volatile uint8_t *bytes=memory;
    for(uint32_t i=0u;i<size;++i)bytes[i]=0u;
}
static uint32_t parity(uint32_t word) {
    word^=word>>16u;word^=word>>8u;word^=word>>4u;word^=word>>2u;word^=word>>1u;
    return word&1u;
}
r3_rx_status_t r3_rx_encode_identity(uint32_t out[4],uint32_t address,uint32_t length,
                                   uint32_t end_ring,uint32_t parity_enabled) {
    if(!out)return R3_RX_ARGUMENT;
    clear(out,16u);
    if(end_ring>1u || parity_enabled>1u)return R3_RX_ARGUMENT;
    if(!length || length>0x7fffu)return R3_RX_LENGTH;
    if(!address || (address&3u) || address>=R3_RX_RAM_END || length>R3_RX_RAM_END-address)
        return R3_RX_BUFFER;
    uint32_t control=0x80000000u|(end_ring?0x10000000u:0u),word=length;
    if(parity_enabled && parity(control^word^address))word|=0x40000u;
    out[2]=address;out[3]=0u;out[0]=control;out[1]=word;
    return R3_RX_CANDIDATE;
}
r3_rx_status_t r3_rx_plan_buffers(const r3_rx_layout_t *layout,uint32_t epoch,uint32_t revision,
                                 uint32_t consumer,uint32_t wanted,uint32_t parity_enabled,
                                 r3_rx_buffer_plan_t *out) {
    if(!out)return R3_RX_ARGUMENT;
    clear(out,sizeof(*out));
    r3_rx_status_t status=r3_rx_validate_layout(layout);
    if(status!=R3_RX_CANDIDATE)return status;
    if(!epoch || !revision)return R3_RX_EPOCH;
    if(consumer>=R3_RX_SLOTS || !wanted || wanted>=R3_RX_SLOTS)return R3_RX_INDEX;
    if(parity_enabled>1u)return R3_RX_ARGUMENT;
    uint32_t first=layout->ring_base+layout->ring_bytes;
    uint32_t stride=(layout->buffer_bytes+3u)&~3u;
    uint32_t available=layout->arena_base+layout->arena_bytes-first;
    if(stride*R3_RX_SLOTS>available)return R3_RX_BUFFER;
    for(uint32_t n=0u;n<wanted;++n) {
        uint32_t index=(consumer+n)&(R3_RX_SLOTS-1u),address=first+index*stride;
        status=r3_rx_encode_identity(out->descriptors[index],address,layout->buffer_bytes,
                                     index==R3_RX_SLOTS-1u,parity_enabled);
        if(status!=R3_RX_CANDIDATE){clear(out,sizeof(*out));return status;}
        out->slots[index].id=index+1u;out->slots[index].address=address;
        out->slots[index].capacity=stride;out->slots[index].epoch=epoch;
    }
    out->state.consumer=consumer;out->state.producer=(consumer+wanted)&(R3_RX_SLOTS-1u);
    out->state.epoch=epoch;out->state.revision=revision;
    out->producer_pointer=layout->ring_base+(out->state.producer<<4u);
    return R3_RX_CANDIDATE;
}
r3_rx_status_t r3_rx_pointer_index(uint32_t raw,uint32_t mask,uint32_t base,uint32_t *index) {
    if(!index)return R3_RX_ARGUMENT;
    *index=0u;
    if(mask<0x1ffu || mask>0xffffu || (mask&(mask+1u)) || (base&15u))return R3_RX_GEOMETRY;
    if(raw==0xffffffffu)return R3_RX_READ_ERROR;
    uint32_t offset=((raw&mask)-base)&mask;
    if((offset&15u) || offset>=R3_RX_SLOTS*16u)return R3_RX_COMPLETION;
    *index=offset>>4u;
    return R3_RX_CANDIDATE;
}

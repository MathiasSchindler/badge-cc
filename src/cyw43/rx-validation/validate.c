#include "validate.h"

static int span(uint32_t base,uint32_t bytes,uint32_t address,uint32_t size) {
    return size && address>=base && size<=bytes && address-base<=bytes-size;
}
static int overlap(uint32_t a,uint32_t size_a,uint32_t b,uint32_t size_b) {
    return a<b+size_b && b<a+size_a;
}
static r3_rx_candidate_t result(r3_rx_status_t status) {
    r3_rx_candidate_t out;
    /* Keep clearing local fields freestanding: no compiler-emitted memclr helper. */
    volatile r3_rx_candidate_t *fields=&out;
    fields->status=status;fields->completed=0u;fields->slot=0u;
    fields->next_consumer=0u;fields->buffer_id=0u;fields->buffer_address=0u;
    return out;
}
r3_rx_status_t r3_rx_validate_layout(const r3_rx_layout_t *layout) {
    if(!layout)return R3_RX_ARGUMENT;
    if(layout->count!=R3_RX_SLOTS || layout->ring_bytes!=R3_RX_SLOTS*16u ||
       !layout->arena_base || layout->arena_base>=R3_RX_RAM_END ||
       !layout->arena_bytes || layout->arena_bytes>R3_RX_RAM_END-layout->arena_base ||
       (layout->ring_base&15u) || !span(layout->arena_base,layout->arena_bytes,layout->ring_base,layout->ring_bytes) ||
       layout->buffer_bytes>0x7fffu || layout->rx_offset<2u || layout->rx_offset>=layout->buffer_bytes)
        return R3_RX_GEOMETRY;
    return R3_RX_CANDIDATE;
}
r3_rx_candidate_t r3_rx_validate(const r3_rx_layout_t *layout,const r3_rx_snapshot_t *state,
                                const r3_rx_sample_t *sample,const r3_rx_slot_t *slots,uint32_t length) {
    if(!layout || !state || !sample || !slots || length!=R3_RX_SLOTS)return result(R3_RX_ARGUMENT);
    r3_rx_status_t geometry=r3_rx_validate_layout(layout);
    if(geometry!=R3_RX_CANDIDATE)return result(geometry);
    if(state->consumer>=R3_RX_SLOTS || state->producer>=R3_RX_SLOTS)return result(R3_RX_INDEX);
    if(!state->epoch || !state->revision)return result(R3_RX_EPOCH);
    if(sample->read==R3_RX_NOT_READ)return result(R3_RX_UNAVAILABLE);
    if(sample->read!=R3_RX_READ_OK)return result(R3_RX_READ_ERROR);
    if(sample->epoch!=state->epoch || sample->revision!=state->revision)return result(R3_RX_STALE);
    if(sample->completion_index>=R3_RX_SLOTS)return result(R3_RX_COMPLETION);
    uint32_t posted=(state->producer-state->consumer)&(R3_RX_SLOTS-1u);
    uint32_t completed=(sample->completion_index-state->consumer)&(R3_RX_SLOTS-1u);
    if(completed>posted)return result(R3_RX_COMPLETION);
    for(uint32_t i=0u;i<R3_RX_SLOTS;++i) {
        const r3_rx_slot_t *slot=&slots[i];
        uint32_t distance=(i-state->consumer)&(R3_RX_SLOTS-1u);
        if(distance>=posted) {
            if(slot->id || slot->address || slot->capacity || slot->epoch)return result(R3_RX_SLOT);
            continue;
        }
        if(!slot->id || slot->id==0x80000000u || slot->epoch!=state->epoch)return result(R3_RX_SLOT);
        if((slot->address&3u) || slot->capacity<layout->buffer_bytes ||
           !span(layout->arena_base,layout->arena_bytes,slot->address,slot->capacity))
            return result(R3_RX_BUFFER);
        if(overlap(slot->address,slot->capacity,layout->ring_base,layout->ring_bytes))return result(R3_RX_ALIAS);
        for(uint32_t j=0u;j<i;++j) {
            if(((j-state->consumer)&(R3_RX_SLOTS-1u))>=posted)continue;
            if(slots[j].id==slot->id || overlap(slot->address,slot->capacity,slots[j].address,slots[j].capacity))
                return result(R3_RX_ALIAS);
        }
    }
    if(!completed)return result(R3_RX_EMPTY);
    r3_rx_candidate_t out={R3_RX_CANDIDATE,completed,state->consumer,
                          (state->consumer+1u)&(R3_RX_SLOTS-1u),
                          slots[state->consumer].id,slots[state->consumer].address};
    return out;
}
r3_rx_length_t r3_rx_validate_length(r3_rx_format_t format,uint32_t buffer_bytes,uint32_t offset,uint32_t length) {
    r3_rx_length_t out={R3_RX_FORMAT,0u};
    if(format!=R3_RX_ORDINARY_LENGTH_MODEL)return out;
    out.status=R3_RX_GEOMETRY;
    if(buffer_bytes>0x7fffu || offset<2u || offset>=buffer_bytes)return out;
    out.status=R3_RX_LENGTH;
    if(!length || length>0xffffu || length>buffer_bytes-offset)return out;
    out.status=R3_RX_CANDIDATE;out.total_bytes=offset+length;return out;
}
const char *r3_rx_status_name(r3_rx_status_t status) {
    switch(status) {
    case R3_RX_ARGUMENT:return "argument";
    case R3_RX_GEOMETRY:return "unsupported geometry";
    case R3_RX_INDEX:return "invalid index";
    case R3_RX_EPOCH:return "uninitialized generation";
    case R3_RX_UNAVAILABLE:return "completion unavailable";
    case R3_RX_READ_ERROR:return "completion read failed";
    case R3_RX_STALE:return "stale completion sample";
    case R3_RX_COMPLETION:return "invalid completion boundary";
    case R3_RX_SLOT:return "invalid ledger slot";
    case R3_RX_BUFFER:return "invalid buffer span";
    case R3_RX_ALIAS:return "overlapping or duplicate allocation";
    case R3_RX_FORMAT:return "unsupported length format";
    case R3_RX_LENGTH:return "invalid length";
    case R3_RX_EMPTY:return "no completed buffers";
    case R3_RX_CANDIDATE:return "model candidate, not hardware authorization";
    default:return "invalid validation result";
    }
}

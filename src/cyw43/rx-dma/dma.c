#include "dma.h"

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8u|(uint32_t)p[2]<<16u|(uint32_t)p[3]<<24u;
}
static void put32(uint8_t *p,uint32_t v) {
    for(uint32_t i=0u;i<4u;++i)p[i]=(uint8_t)(v>>(8u*i));
}
static int valid_io(const r3_dma_io_t *io) {
    return io&&io->read&&io->write&&io->read_memory&&io->write_memory&&
           io->barrier&&io->now&&io->service;
}
static r3_dma_error_t fail(r3_dma_t *s,r3_dma_error_t error) {
    s->error=error;s->phase=R3_DMA_QUARANTINED;s->raw_bytes=0u;return error;
}
static int check(r3_dma_t *s,const r3_dma_io_t *io) {
    int service=io->service(io->context);
    if(service!=1){(void)fail(s,service==0?R3_DMA_CANCELLED:R3_DMA_IO);return 0;}
    uint32_t now;
    if(io->now(io->context,&now)!=1){(void)fail(s,R3_DMA_IO);return 0;}
    if(now-s->start>=s->budget){(void)fail(s,R3_DMA_TIMEOUT);return 0;}
    return 1;
}
static int read_reg(r3_dma_t *s,const r3_dma_io_t *io,uint32_t address,uint32_t width,uint32_t *out) {
    s->address=address;
    if(!check(s,io))return 0;
    if(io->read(io->context,address,width,out)!=1){(void)fail(s,R3_DMA_IO);return 0;}
    s->value=*out;
    if(*out==0xffffffffu||(width==1u&&*out>=255u)){(void)fail(s,R3_DMA_READBACK);return 0;}
    return check(s,io);
}
static int write_reg(r3_dma_t *s,const r3_dma_io_t *io,uint32_t address,uint32_t value) {
    s->address=address;s->value=value;
    if(!check(s,io))return 0;
    if(io->write(io->context,address,4u,value)!=1){(void)fail(s,R3_DMA_IO);return 0;}
    return check(s,io);
}
static int fence(r3_dma_t *s,const r3_dma_io_t *io) {
    if(!check(s,io))return 0;
    if(io->barrier(io->context)!=1){(void)fail(s,R3_DMA_IO);return 0;}
    return check(s,io);
}
static int accessible(r3_dma_t *s,const r3_dma_io_t *io) {
    uint32_t reset,clock;
    if(!read_reg(s,io,0x18101800u,1u,&reset)||!read_reg(s,io,0x18101408u,1u,&clock))return 0;
    if(reset!=0u||!(clock&1u)){(void)fail(s,R3_DMA_STATE);return 0;}
    return 1;
}
static int memory(r3_dma_t *s,const r3_dma_io_t *io,uint32_t address,uint8_t *bytes,uint32_t n,int write) {
    s->address=address;
    if(!n||n>R3_DMA_BUFFER_BYTES||address<R3_DMA_ARENA||address-R3_DMA_ARENA>R3_DMA_ARENA_BYTES-n) {
        (void)fail(s,R3_DMA_OWNERSHIP);return 0;
    }
    if(!check(s,io))return 0;
    int ok=write?io->write_memory(io->context,address,bytes,n):io->read_memory(io->context,address,bytes,n);
    if(ok!=1){(void)fail(s,R3_DMA_IO);return 0;}
    return check(s,io);
}
static int zero_memory(r3_dma_t *s,const r3_dma_io_t *io,uint32_t address,uint32_t n) {
    uint8_t zero[64],back[64];
    for(uint32_t i=0u;i<64u;++i)zero[i]=0u;
    for(uint32_t at=0u;at<n;at+=64u) {
        uint32_t size=n-at<64u?n-at:64u;
        if(!memory(s,io,address+at,zero,size,1)||!memory(s,io,address+at,back,size,0))return 0;
        for(uint32_t i=0u;i<size;++i)if(back[i]){(void)fail(s,R3_DMA_READBACK);return 0;}
    }
    return 1;
}
static int descriptor(r3_dma_t *s,const r3_dma_io_t *io,uint32_t index,const uint32_t words[4]) {
    uint8_t bytes[16],back[16];
    for(uint32_t i=0u;i<4u;++i)put32(bytes+4u*i,words[i]);
    uint32_t address=s->layout.ring_base+16u*index;
    if(!memory(s,io,address,bytes,16u,1)||!memory(s,io,address,back,16u,0))return 0;
    for(uint32_t i=0u;i<4u;++i)if(get32(back+4u*i)!=words[i]){
        (void)fail(s,R3_DMA_READBACK);return 0;
    }
    return 1;
}
static r3_dma_error_t enter(r3_dma_t *s,const r3_dma_io_t *io) {
    if(!s)return R3_DMA_ARGUMENT;
    if(s->busy)return R3_DMA_REENTRANT;
    s->raw_bytes=0u;
    if(!valid_io(io))return R3_DMA_ARGUMENT;
    s->busy=1u;s->raw_bytes=0u;return R3_DMA_OK;
}
static r3_dma_error_t leave(r3_dma_t *s,r3_dma_error_t error) {
    s->busy=0u;s->error=error;return error;
}
static int begin_time(r3_dma_t *s,const r3_dma_io_t *io,uint32_t budget) {
    s->budget=budget;
    if(io->now(io->context,&s->start)!=1){(void)fail(s,R3_DMA_IO);return 0;}
    return check(s,io);
}
static int running_sample(r3_dma_t *s,const r3_dma_io_t *io,uint32_t *current) {
    uint32_t control,producer,first,last,error;
    for(uint32_t attempt=0u;attempt<8u;++attempt) {
        if(!accessible(s,io)||
           !read_reg(s,io,R3_DMA_BASE,4u,&control)||
           !read_reg(s,io,R3_DMA_BASE+4u,4u,&producer)||
           !read_reg(s,io,R3_DMA_BASE+16u,4u,&first)||
           !read_reg(s,io,R3_DMA_BASE+20u,4u,&error)||
           !read_reg(s,io,R3_DMA_BASE+16u,4u,&last)||!accessible(s,io))return 0;
        s->status0=last;s->status1=error;
        if(control!=R3_DMA_CONTROL||(error&0xf0000000u)||
           (last>>28u!=1u&&last>>28u!=2u)) { (void)fail(s,R3_DMA_ENGINE);return 0; }
        if((producer&0x1fffu)!=((s->layout.ring_base+s->plan.state.producer*16u)&0x1fffu)) {
            (void)fail(s,R3_DMA_OWNERSHIP);return 0;
        }
        if(first!=last)continue;
        if(r3_rx_pointer_index(last,0x1fffu,s->layout.ring_base,current)!=R3_RX_CANDIDATE) {
            (void)fail(s,R3_DMA_OWNERSHIP);return 0;
        }
        return 1;
    }
    s->error=R3_DMA_RETRY;return 0;
}
r3_dma_error_t r3_dma_start(r3_dma_t *s,const r3_dma_io_t *io,uint32_t epoch,uint32_t budget) {
    r3_dma_error_t error=enter(s,io);if(error)return error;
    if(s->phase!=R3_DMA_NEW||!epoch||!budget||budget>0x7fffffffu)
        return leave(s,R3_DMA_ARGUMENT);
    s->phase=R3_DMA_STARTING;s->epoch=epoch;
    s->layout=(r3_rx_layout_t){R3_DMA_ARENA,R3_DMA_ARENA_BYTES,R3_DMA_ARENA,512u,32u,R3_DMA_BUFFER_BYTES,38u};
    if(r3_rx_plan_buffers(&s->layout,epoch,1u,0u,31u,0u,&s->plan)!=R3_RX_CANDIDATE)
        return leave(s,fail(s,R3_DMA_OWNERSHIP));
    if(!begin_time(s,io,budget)||!accessible(s,io))return leave(s,s->error);
    uint32_t control,status,error_bits;
    if(!read_reg(s,io,R3_DMA_BASE,4u,&control)||
       !read_reg(s,io,R3_DMA_BASE+16u,4u,&status)||
       !read_reg(s,io,R3_DMA_BASE+20u,4u,&error_bits))return leave(s,s->error);
    if(control!=0u||(status>>28u)!=0u||(error_bits&0xf0000000u))
        return leave(s,fail(s,R3_DMA_STATE));
    if(!zero_memory(s,io,R3_DMA_ARENA,512u+32u*R3_DMA_BUFFER_BYTES))return leave(s,s->error);
    for(uint32_t i=0u;i<31u;++i)
        if(!descriptor(s,io,i,s->plan.descriptors[i]))return leave(s,s->error);
    if(!fence(s,io)||!accessible(s,io)||
       !write_reg(s,io,R3_DMA_BASE+8u,R3_DMA_ARENA)||!write_reg(s,io,R3_DMA_BASE+12u,0u)||
       !read_reg(s,io,R3_DMA_BASE+8u,4u,&status)||!read_reg(s,io,R3_DMA_BASE+12u,4u,&error_bits))
        return leave(s,s->error);
    if(status!=R3_DMA_ARENA||error_bits!=0u)return leave(s,fail(s,R3_DMA_READBACK));
    /* Begin with an empty hardware producer before enabling; publish only after
       descriptor/buffer writes are ordered and the software ledger is complete. */
    if(!write_reg(s,io,R3_DMA_BASE+4u,R3_DMA_ARENA)||!fence(s,io)||
       !write_reg(s,io,R3_DMA_BASE,R3_DMA_CONTROL)||!fence(s,io)||
       !write_reg(s,io,R3_DMA_BASE+4u,s->plan.producer_pointer)||!fence(s,io))
        return leave(s,s->error);
    ++s->publications;
    uint32_t current;
    if(!running_sample(s,io,&current))return leave(s,fail(s,s->error));
    r3_rx_sample_t sample={R3_RX_READ_OK,epoch,1u,current};
    r3_rx_candidate_t candidate=r3_rx_validate(&s->layout,&s->plan.state,&sample,s->plan.slots,32u);
    if(candidate.status!=R3_RX_EMPTY&&candidate.status!=R3_RX_CANDIDATE)
        return leave(s,fail(s,R3_DMA_OWNERSHIP));
    s->phase=R3_DMA_RUNNING;return leave(s,R3_DMA_OK);
}
r3_dma_error_t r3_dma_receive(r3_dma_t *s,const r3_dma_io_t *io) {
    r3_dma_error_t error=enter(s,io);if(error)return error;
    if(s->phase!=R3_DMA_RUNNING)return leave(s,R3_DMA_STATE);
    uint32_t current;
    if(!running_sample(s,io,&current))return leave(s,s->error);
    r3_rx_sample_t sample={R3_RX_READ_OK,s->epoch,s->plan.state.revision,current};
    r3_rx_candidate_t item=r3_rx_validate(&s->layout,&s->plan.state,&sample,s->plan.slots,32u);
    if(item.status==R3_RX_EMPTY)return leave(s,R3_DMA_EMPTY);
    if(item.status!=R3_RX_CANDIDATE)return leave(s,fail(s,R3_DMA_OWNERSHIP));
    if(!fence(s,io)||!memory(s,io,item.buffer_address,s->raw,R3_DMA_BUFFER_BYTES,0)||!fence(s,io))
        return leave(s,s->error);
    uint32_t after;
    if(!running_sample(s,io,&after))return leave(s,s->error);
    uint32_t posted=(s->plan.state.producer-s->plan.state.consumer)&31u;
    uint32_t completed=(after-s->plan.state.consumer)&31u;
    if(completed<item.completed||completed>posted)return leave(s,fail(s,R3_DMA_OWNERSHIP));
    uint32_t length=(uint32_t)s->raw[0]|(uint32_t)s->raw[1]<<8u;
    r3_rx_length_t extent=r3_rx_validate_length(R3_RX_ORDINARY_LENGTH_MODEL,R3_DMA_BUFFER_BYTES,38u,length);
    if(extent.status!=R3_RX_CANDIDATE)return leave(s,fail(s,R3_DMA_LENGTH));
    uint32_t slot=s->plan.state.producer,address=R3_DMA_ARENA+512u+slot*R3_DMA_BUFFER_BYTES;
    if(s->plan.state.revision==0xffffffffu)return leave(s,fail(s,R3_DMA_OWNERSHIP));
    const uint32_t poison[4]={0u,0u,0xdeadbeefu,0xdeadbeefu};
    if(!descriptor(s,io,item.slot,poison)||!zero_memory(s,io,address,R3_DMA_BUFFER_BYTES))
        return leave(s,s->error);
    uint32_t words[4];
    if(r3_rx_encode_identity(words,address,R3_DMA_BUFFER_BYTES,slot==31u,0u)!=R3_RX_CANDIDATE)
        return leave(s,fail(s,R3_DMA_OWNERSHIP));
    if(!descriptor(s,io,slot,words)||!fence(s,io)||!accessible(s,io))return leave(s,s->error);
    for(uint32_t i=0u;i<4u;++i) {
        s->plan.descriptors[item.slot][i]=poison[i];
        s->plan.descriptors[slot][i]=words[i];
    }
    s->plan.slots[item.slot]=(r3_rx_slot_t){0u,0u,0u,0u};
    s->plan.slots[slot]=(r3_rx_slot_t){slot+1u,address,R3_DMA_BUFFER_BYTES,s->epoch};
    s->plan.state.consumer=item.next_consumer;s->plan.state.producer=(slot+1u)&31u;
    ++s->plan.state.revision;
    s->plan.producer_pointer=R3_DMA_ARENA+s->plan.state.producer*16u;
    uint32_t published;
    if(!write_reg(s,io,R3_DMA_BASE+4u,s->plan.producer_pointer)||!fence(s,io)||
       !read_reg(s,io,R3_DMA_BASE+4u,4u,&published))return leave(s,s->error);
    if((published&0x1fffu)!=(s->plan.producer_pointer&0x1fffu))
        return leave(s,fail(s,R3_DMA_READBACK));
    ++s->publications;++s->copies;s->raw_bytes=extent.total_bytes;
    return leave(s,R3_DMA_OK);
}
r3_dma_error_t r3_dma_stop(r3_dma_t *s,const r3_dma_io_t *io,uint32_t budget) {
    r3_dma_error_t error=enter(s,io);if(error)return error;
    if(s->phase!=R3_DMA_RUNNING||!budget||budget>0x7fffffffu)return leave(s,R3_DMA_STATE);
    if(!begin_time(s,io,budget)||!accessible(s,io)||!write_reg(s,io,R3_DMA_BASE,0u)||!fence(s,io))
        return leave(s,s->error);
    for(uint32_t i=0u;i<1001u;++i) {
        uint32_t status,errors,control;
        if(!accessible(s,io)||!read_reg(s,io,R3_DMA_BASE+16u,4u,&status)||
           !read_reg(s,io,R3_DMA_BASE+20u,4u,&errors)||!read_reg(s,io,R3_DMA_BASE,4u,&control))
            return leave(s,s->error);
        if((errors&0xf0000000u)||control!=0u)return leave(s,fail(s,R3_DMA_ENGINE));
        if(!(status>>28u)) {
            if(!fence(s,io)||!accessible(s,io))return leave(s,s->error);
            s->phase=R3_DMA_STOPPED;return leave(s,R3_DMA_OK);
        }
    }
    return leave(s,fail(s,R3_DMA_TIMEOUT));
}
const char *r3_dma_error_name(r3_dma_error_t error) {
    switch(error) {
    case R3_DMA_OK:return "operation complete, not frame proof";
    case R3_DMA_EMPTY:return "no completed descriptor";
    case R3_DMA_RETRY:return "moving completion snapshot";
    case R3_DMA_ARGUMENT:return "invalid argument";
    case R3_DMA_STATE:return "unavailable core or invalid lifecycle";
    case R3_DMA_IO:return "callback failure";
    case R3_DMA_CANCELLED:return "cancelled";
    case R3_DMA_TIMEOUT:return "deadline or sample bound";
    case R3_DMA_READBACK:return "readback mismatch";
    case R3_DMA_ENGINE:return "DMA state/control/error";
    case R3_DMA_OWNERSHIP:return "ownership violation";
    case R3_DMA_LENGTH:return "invalid raw buffer extent";
    case R3_DMA_REENTRANT:return "reentrant operation";
    default:return "unknown DMA error";
    }
}

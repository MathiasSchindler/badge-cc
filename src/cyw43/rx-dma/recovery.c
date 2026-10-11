#include "recovery.h"

static int fail(r3_rxe_report_t *r,r3_rxe_error_t error) {
    r->error=error;r->reset_required=r->issued;return 0;
}
static int service(const r3_dma_io_t *io,uint32_t budget,r3_rxe_report_t *r) {
    int result=io->service(io->context);
    if(result!=1)return fail(r,result==0?R3_RXE_CANCELLED:R3_RXE_IO);
    uint32_t tick;
    if(io->now(io->context,&tick)!=1)return fail(r,R3_RXE_IO);
    r->elapsed=tick-r->start;
    return r->elapsed<budget?1:fail(r,R3_RXE_TIMEOUT);
}
static int read(const r3_dma_io_t *io,uint32_t address,uint32_t width,uint32_t *value,r3_rxe_report_t *r) {
    r->last_address=address;
    if(io->read(io->context,address,width,value)!=1)return fail(r,R3_RXE_IO);
    if(*value==0xffffffffu||(width==1u&&*value>=255u)||(width==2u&&*value>=65535u))
        return fail(r,R3_RXE_IO);
    return 1;
}
static int stopped(const r3_dma_io_t *io,r3_rxe_report_t *r) {
    uint32_t reset,clock,mac,dma,status,error;
    if(!read(io,0x18101800u,1u,&reset,r)||!read(io,0x18101408u,1u,&clock,r))return 0;
    if(reset||!(clock&1u))return fail(r,R3_RXE_UNAVAILABLE);
    if(!read(io,0x18001120u,4u,&mac,r)||!read(io,R3_DMA_BASE,4u,&dma,r)||
       !read(io,R3_DMA_BASE+16u,4u,&status,r)||!read(io,R3_DMA_BASE+20u,4u,&error,r))return 0;
    if((mac&3u)||dma||(status>>28u)||(error&0xf0000000u))return fail(r,R3_RXE_NOT_STOPPED);
    return 1;
}
r3_rxe_error_t r3_rxe_flush_protocol(const r3_dma_io_t *io,uint32_t budget,uint32_t limit,r3_rxe_report_t *r) {
    if(!r)return R3_RXE_ARGUMENT;
    volatile uint8_t *bytes=(volatile uint8_t *)r;
    for(uint32_t i=0u;i<sizeof(*r);++i)bytes[i]=0u;
    if(!io||!io->read||!io->write||!io->barrier||!io->now||!io->service||
       !budget||budget>0x7fffffffu||!limit||limit>100000u) {
        r->error=R3_RXE_ARGUMENT;return r->error;
    }
    if(io->now(io->context,&r->start)!=1){(void)fail(r,R3_RXE_IO);return r->error;}
    if(!service(io,budget,r)||!stopped(io,r)||!service(io,budget,r))return r->error;
    r->last_address=0x18001408u;r->issued=1u;
    if(io->write(io->context,0x18001408u,2u,0x14u)!=1||io->barrier(io->context)!=1) {
        (void)fail(r,R3_RXE_IO);return r->error;
    }
    for(uint32_t i=0u;i<limit;++i) {
        if(!service(io,budget,r)||!stopped(io,r)||
           !read(io,0x18001408u,2u,&r->last_control,r)||!service(io,budget,r))return r->error;
        ++r->samples;
        if(!(r->last_control&0x14u)) {
            if(io->barrier(io->context)!=1){(void)fail(r,R3_RXE_IO);return r->error;}
            if(!service(io,budget,r)||!stopped(io,r))return r->error;
            r->clear_observed=1u;return R3_RXE_CLEAR_OBSERVED;
        }
    }
    (void)fail(r,R3_RXE_SAMPLE_LIMIT);return r->error;
}
const char *r3_rxe_error_name(r3_rxe_error_t error) {
    switch(error) {
    case R3_RXE_CLEAR_OBSERVED:return "both RXE request bits read clear, not RF/ownership proof";
    case R3_RXE_ARGUMENT:return "invalid recovery argument";
    case R3_RXE_UNAVAILABLE:return "D11 reset or clock unavailable";
    case R3_RXE_NOT_STOPPED:return "MAC/PSM or RX DMA not in required stopped state";
    case R3_RXE_IO:return "recovery I/O or service failure";
    case R3_RXE_CANCELLED:return "recovery cancelled";
    case R3_RXE_TIMEOUT:return "recovery deadline";
    case R3_RXE_SAMPLE_LIMIT:return "recovery sample limit";
    default:return "unknown RXE recovery error";
    }
}

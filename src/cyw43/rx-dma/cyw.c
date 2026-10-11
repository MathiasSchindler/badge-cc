#include "cyw.h"

#if !defined(__ARM_ARCH_7M__)
#error CYW DMA adapter requires the Cortex-M3 build
#endif

static int read_register(void *context,uint32_t address,uint32_t width,uint32_t *value) {
    (void)context;
    if(!value)return 0;
    if(width==1u&&(address==0x18101800u||address==0x18101408u)) {
        *value=*(volatile const uint8_t *)address;return 1;
    }
    if(width!=4u||address<R3_DMA_BASE||address>R3_DMA_BASE+20u||(address&3u))return 0;
    *value=*(volatile const uint32_t *)address;return 1;
}
static int write_register(void *context,uint32_t address,uint32_t width,uint32_t value) {
    (void)context;
    if(width!=4u||address<R3_DMA_BASE||address>R3_DMA_BASE+12u||(address&3u))return 0;
    if(address==R3_DMA_BASE&&value!=0u&&value!=R3_DMA_CONTROL)return 0;
    if(address==R3_DMA_BASE+4u&&(value<R3_DMA_ARENA||value>=R3_DMA_ARENA+512u||(value&15u)))return 0;
    if(address==R3_DMA_BASE+8u&&value!=R3_DMA_ARENA)return 0;
    if(address==R3_DMA_BASE+12u&&value!=0u)return 0;
    *(volatile uint32_t *)address=value;return 1;
}
static int span(uint32_t address,uint32_t n) {
    return n&&n<=R3_DMA_BUFFER_BYTES&&address>=R3_DMA_ARENA&&
           address-R3_DMA_ARENA<=R3_DMA_ARENA_BYTES-n;
}
static int read_memory(void *context,uint32_t address,uint8_t *out,uint32_t n) {
    (void)context;if(!out||!span(address,n))return 0;
    volatile const uint8_t *source=(volatile const uint8_t *)address;
    for(uint32_t i=0u;i<n;++i)out[i]=source[i];
    return 1;
}
static int write_memory(void *context,uint32_t address,const uint8_t *in,uint32_t n) {
    (void)context;if(!in||!span(address,n))return 0;
    volatile uint8_t *destination=(volatile uint8_t *)address;
    for(uint32_t i=0u;i<n;++i)destination[i]=in[i];
    return 1;
}
static int barrier(void *context) {
    (void)context;__asm__ volatile("dsb sy" ::: "memory");return 1;
}
static int now(void *context,uint32_t *value) {
    (void)context;if(!value)return 0;
    *value=*(volatile const uint32_t *)0x18003090u;return 1;
}
static int service(void *context) {
    r3_dma_cyw_context_t *io=context;
    return io->service(io->context);
}
int r3_dma_cyw_bind(r3_dma_io_t *out,r3_dma_cyw_context_t *context) {
    if(!out||!context||!context->service)return 0;
    *out=(r3_dma_io_t){context,read_register,write_register,read_memory,write_memory,barrier,now,service};
    return 1;
}
static int recovery_read(void *context,uint32_t address,uint32_t width,uint32_t *value) {
    if(!value)return 0;
    if(address==0x18001120u&&width==4u){*value=*(volatile const uint32_t *)address;return 1;}
    if(address==0x18001408u&&width==2u){*value=*(volatile const uint16_t *)address;return 1;}
    return read_register(context,address,width,value);
}
static int recovery_write(void *context,uint32_t address,uint32_t width,uint32_t value) {
    (void)context;
    if(address!=0x18001408u||width!=2u||value!=0x14u)return 0;
    *(volatile uint16_t *)address=(uint16_t)value;return 1;
}
int r3_rxe_cyw_bind(r3_dma_io_t *out,r3_dma_cyw_context_t *context) {
    if(!out||!context||!context->service)return 0;
    *out=(r3_dma_io_t){context,recovery_read,recovery_write,0,0,barrier,now,service};
    return 1;
}

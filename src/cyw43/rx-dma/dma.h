#ifndef R3_RX_DMA_H
#define R3_RX_DMA_H
#include "../rx-preparation/prepare.h"

#define R3_DMA_BASE 0x18001220u
#define R3_DMA_CONTROL 0x0004084du
#define R3_DMA_ARENA 0x00020000u
#define R3_DMA_ARENA_BYTES 0x00010000u
#define R3_DMA_BUFFER_BYTES 1716u
typedef enum {
    R3_DMA_OK,R3_DMA_EMPTY,R3_DMA_RETRY,R3_DMA_ARGUMENT,R3_DMA_STATE,
    R3_DMA_IO,R3_DMA_CANCELLED,R3_DMA_TIMEOUT,R3_DMA_READBACK,
    R3_DMA_ENGINE,R3_DMA_OWNERSHIP,R3_DMA_LENGTH,R3_DMA_REENTRANT
} r3_dma_error_t;
typedef enum { R3_DMA_NEW,R3_DMA_STARTING,R3_DMA_RUNNING,R3_DMA_STOPPED,R3_DMA_QUARANTINED } r3_dma_phase_t;
typedef struct {
    void *context;
    int (*read)(void *,uint32_t,uint32_t,uint32_t *);
    int (*write)(void *,uint32_t,uint32_t,uint32_t);
    int (*read_memory)(void *,uint32_t,uint8_t *,uint32_t);
    int (*write_memory)(void *,uint32_t,const uint8_t *,uint32_t);
    int (*barrier)(void *);
    int (*now)(void *,uint32_t *);
    int (*service)(void *);
} r3_dma_io_t;
typedef struct {
    r3_dma_phase_t phase;
    r3_dma_error_t error;
    uint32_t busy,epoch,start,budget,address,value,status0,status1,copies,publications;
    uint32_t raw_bytes;
    r3_rx_layout_t layout;
    r3_rx_buffer_plan_t plan;
    uint8_t raw[R3_DMA_BUFFER_BYTES];
} r3_dma_t;

/* Zero-initialize once after independent cold recovery; never clear a live/quarantined
   object. Caller owns an exclusive, identity-mapped, coherent CYW RAM arena and a
   qualified active D11/PHY/PSM lifecycle. This API supplies none of those prerequisites.
   Callbacks return exactly 1 on success; service returns 0 to cancel, negative on error.
   All callbacks must be bounded. Barriers order device/RAM accesses (DMB/DSB on ARM).
   NEW is a software state, not hardware permission. No function writes TX/PHY/PSM.
   A QUARANTINED arena must not be reused until independent power-off/reset recovery. */
r3_dma_error_t r3_dma_start(r3_dma_t *,const r3_dma_io_t *,uint32_t,uint32_t);
/* Copies a completed buffer before refill; raw bytes are not a validated 802.11 frame.
   Error/retry/empty always clears raw_bytes. Do not alias callback memory with state. */
r3_dma_error_t r3_dma_receive(r3_dma_t *,const r3_dma_io_t *);
r3_dma_error_t r3_dma_stop(r3_dma_t *,const r3_dma_io_t *,uint32_t);
const char *r3_dma_error_name(r3_dma_error_t);
#endif

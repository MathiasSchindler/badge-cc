#ifndef R3_RX_DMA_CYW_H
#define R3_RX_DMA_CYW_H
#include "dma.h"
typedef struct {
    void *context;
    int (*service)(void *);
} r3_dma_cyw_context_t;
/* CYW Cortex-M3 only, not an RP backplane adapter. Binds accessors without doing
   I/O; r3_dma_start still requires a completed, independently qualified initializer.
   A bus fault needs the source fault handler plus independent RP radio recovery. */
int r3_dma_cyw_bind(r3_dma_io_t *,r3_dma_cyw_context_t *);
/* Separate least-permission backend for quiesced RXE recovery; it cannot write DMA. */
int r3_rxe_cyw_bind(r3_dma_io_t *,r3_dma_cyw_context_t *);
#endif

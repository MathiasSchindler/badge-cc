#ifndef R3_RXE_RECOVERY_H
#define R3_RXE_RECOVERY_H
#include "dma.h"

typedef enum { R3_RXE_CLEAR_OBSERVED,R3_RXE_ARGUMENT,R3_RXE_UNAVAILABLE,
               R3_RXE_NOT_STOPPED,R3_RXE_IO,R3_RXE_CANCELLED,
               R3_RXE_TIMEOUT,R3_RXE_SAMPLE_LIMIT } r3_rxe_error_t;
typedef struct {
    r3_rxe_error_t error;
    uint32_t issued,clear_observed,reset_required;
    uint32_t samples,start,elapsed,last_control,last_address;
} r3_rxe_report_t;

/* Exclusive source-owned recovery only: all competing RXE writers must be quiesced.
   Checks requested MAC/PSM and RX-DMA stopped state before issuing exactly0x14.
   Register state checks are necessary, not proof of physical writer quiescence or RF
   safety. No pause/resume or implicit rollback is attempted. On post-write failure
   independent power-off/reset is required; successful polling is only a bit predicate. */
r3_rxe_error_t r3_rxe_flush_protocol(const r3_dma_io_t *,uint32_t,uint32_t,r3_rxe_report_t *);
const char *r3_rxe_error_name(r3_rxe_error_t);
#endif

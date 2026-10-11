#ifndef R3_RX_PREPARE_H
#define R3_RX_PREPARE_H
#include "../rx-validation/validate.h"
typedef struct {
    r3_rx_snapshot_t state;
    uint32_t producer_pointer;
    uint32_t descriptors[R3_RX_SLOTS][4];
    r3_rx_slot_t slots[R3_RX_SLOTS];
} r3_rx_buffer_plan_t;

/* Local staging only: outputs must not alias inputs or memory owned by DMA. */
r3_rx_status_t r3_rx_encode_identity(uint32_t [4],uint32_t,uint32_t,uint32_t,uint32_t);
r3_rx_status_t r3_rx_plan_buffers(const r3_rx_layout_t *,uint32_t,uint32_t,uint32_t,uint32_t,
                                 uint32_t,r3_rx_buffer_plan_t *);
/* Decodes pointer arithmetic only, not DMA state/error bits or ownership. */
r3_rx_status_t r3_rx_pointer_index(uint32_t,uint32_t,uint32_t,uint32_t *);
#endif

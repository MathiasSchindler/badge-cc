#ifndef R3_RX_DMA_FRAME_H
#define R3_RX_DMA_FRAME_H
#include <stdint.h>
typedef enum {
    R3_FRAME_ARGUMENT,R3_FRAME_EXTENT,R3_FRAME_UNSUPPORTED,R3_FRAME_FCS,
    R3_FRAME_IDENTITY,R3_FRAME_MATCH
} r3_frame_result_t;
/* Input is exactly one complete, unencrypted 802.11 MPDU including FCS.
   No DMA-header offset is guessed here. The caller must establish the raw DMA
   format before slicing it. expected is the complete 66-byte lab Ethernet frame. */
r3_frame_result_t r3_frame_match(const uint8_t *,uint32_t,const uint8_t [66],const uint8_t [6]);
#endif

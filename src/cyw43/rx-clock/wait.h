#ifndef R3_RX_WAIT_H
#define R3_RX_WAIT_H
#include <stdint.h>
typedef enum { R3_WAIT_INVALID,R3_WAIT_PENDING,R3_WAIT_READY,R3_WAIT_TIMEOUT,
               R3_WAIT_SAMPLE_LIMIT,R3_WAIT_IO_ERROR,R3_WAIT_CANCELLED } r3_wait_result_t;
typedef enum { R3_SAMPLE_PENDING,R3_SAMPLE_READY,R3_SAMPLE_ERROR,R3_SAMPLE_CANCEL } r3_wait_sample_t;
typedef struct {
    r3_wait_result_t result;
    uint32_t start,budget,limit,samples,elapsed;
} r3_wait_t;
r3_wait_result_t r3_wait_begin(r3_wait_t *,uint32_t,uint32_t,uint32_t);
r3_wait_result_t r3_wait_update(r3_wait_t *,uint32_t,r3_wait_sample_t);
#endif

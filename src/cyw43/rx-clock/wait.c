#include "wait.h"
r3_wait_result_t r3_wait_begin(r3_wait_t *wait,uint32_t now,uint32_t budget,uint32_t limit) {
    if(!wait)return R3_WAIT_INVALID;
    wait->result=R3_WAIT_INVALID;wait->start=now;wait->budget=budget;wait->limit=limit;
    wait->samples=0u;wait->elapsed=0u;
    if(!budget || budget>0x7fffffffu || !limit || limit>4096u)return wait->result;
    wait->result=R3_WAIT_PENDING;return wait->result;
}
r3_wait_result_t r3_wait_update(r3_wait_t *wait,uint32_t now,r3_wait_sample_t sample) {
    if(!wait)return R3_WAIT_INVALID;
    if(wait->result!=R3_WAIT_PENDING)return wait->result;
    ++wait->samples;wait->elapsed=now-wait->start;
    if(sample==R3_SAMPLE_ERROR)wait->result=R3_WAIT_IO_ERROR;
    else if(sample==R3_SAMPLE_CANCEL)wait->result=R3_WAIT_CANCELLED;
    else if(sample!=R3_SAMPLE_PENDING && sample!=R3_SAMPLE_READY)wait->result=R3_WAIT_INVALID;
    else if(wait->elapsed>=wait->budget)wait->result=R3_WAIT_TIMEOUT;
    else if(sample==R3_SAMPLE_READY)wait->result=R3_WAIT_READY;
    else if(wait->samples>=wait->limit)wait->result=R3_WAIT_SAMPLE_LIMIT;
    return wait->result;
}

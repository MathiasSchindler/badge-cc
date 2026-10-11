#include "resources.h"

/* Exact-image data, not an activation policy; provenance is in README.md. */
static const pmu_timing_step_t timings[PMU_TIMING_STEPS]={
    {12u,0x00160002u},{2u,0x000d0004u},{7u,0x000a0001u},{8u,0x000d0004u},
    {10u,0x000d0004u},{13u,0x00070001u},{23u,0x00040004u},{27u,0x00010004u},
    {28u,0x00070001u},{0u,0x00020002u},{1u,0x00010001u},{3u,0x00020002u},
    {4u,0x00010001u},{5u,0x00010001u},{6u,0x00010001u},{9u,0x00050001u},
    {11u,0x00010001u},{12u,0x00300002u},{14u,0x00010001u},{15u,0x00010001u},
    {16u,0x00010001u},{17u,0x00010001u},{18u,0x00030003u},{19u,0x00010001u},
    {20u,0x00010001u},{21u,0x00010001u},{22u,0x00010001u},{24u,0x00130002u},
    {25u,0x00130002u},{26u,0x00130002u},{29u,0x00010001u},{30u,0x00010001u},
    {12u,0x00300002u}
};
static const uint32_t dependencies[PMU_RESOURCE_COUNT]={
    0u,1u,3u,0u,7u,0x1157u,0x117u,0x157u,3u,0u,3u,0x407u,0x157u,0x15d7u,
    0x0ec2bdf7u,0x0ec23df7u,0u,0x0ec03df7u,0x0ec23df7u,0x0ec23df7u,
    0x06c03df7u,0x0eda3df7u,0x06003df7u,0x06403df7u,0u,0x1d7u,0xdd7u,
    0x06c03df7u,0x0eca3df7u,0x1eca3df7u,0x3efa3df7u
};
pmu_plan_status_t pmu_resource_alp_hz(uint32_t control,uint32_t *out) {
    static const uint16_t khz[19]={12000u,13000u,14400u,15360u,16200u,16800u,19200u,
        19800u,20000u,24000u,25000u,26000u,30000u,33600u,37400u,38400u,40000u,48000u,52000u};
    uint32_t selector=(control&0x7cu)>>2u;
    if(!out||!selector||selector>19u)return PMU_PLAN_INPUT;
    *out=(uint32_t)khz[selector-1u]*1000u;return PMU_PLAN_OK;
}
uint32_t pmu_resource_timing_count(uint32_t reported_alp_hz) {
    return reported_alp_hz>37399999u?33u:32u;
}
pmu_plan_status_t pmu_resource_timing_for_clock(uint32_t reported_alp_hz,uint32_t step,pmu_timing_step_t *out) {
    if(!out||step>=pmu_resource_timing_count(reported_alp_hz))return PMU_PLAN_INPUT;
    const pmu_timing_step_t *row=&timings[PMU_TIMING_STEPS-1u-step];
    out->resource=row->resource;out->timing=row->timing;return PMU_PLAN_OK;
}
pmu_plan_status_t pmu_resource_timing(uint32_t step,pmu_timing_step_t *out) {
    return pmu_resource_timing_for_clock(37400000u,step,out);
}
pmu_plan_status_t pmu_resource_dependency(uint32_t resource,uint32_t *out) {
    if(!out||resource>=PMU_RESOURCE_COUNT)return PMU_PLAN_INPUT;
    *out=dependencies[resource];return PMU_PLAN_OK;
}
pmu_plan_status_t pmu_resource_expand(uint32_t requested,uint32_t transitive,uint32_t *out) {
    if(!out||(requested&~PMU_RESOURCE_MASK)||transitive>1u)return PMU_PLAN_INPUT;
    uint32_t result=requested;
    for(uint32_t pass=0u;pass<PMU_RESOURCE_COUNT;++pass) {
        uint32_t next=result;
        for(uint32_t i=0u;i<PMU_RESOURCE_COUNT;++i)if(result&(1u<<i))next|=dependencies[i];
        if(!transitive||next==result){*out=next;return PMU_PLAN_OK;}
        result=next;
    }
    return PMU_PLAN_PROGRESS;
}

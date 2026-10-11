#ifndef U26_PMU_RESOURCES_H
#define U26_PMU_RESOURCES_H
#include <stdint.h>
#define PMU_RESOURCE_COUNT 31u
#define PMU_TIMING_STEPS 33u
#define PMU_RESOURCE_MASK 0x7fffffffu
typedef enum { PMU_PLAN_OK,PMU_PLAN_INPUT,PMU_PLAN_PROGRESS } pmu_plan_status_t;
typedef struct { uint32_t resource,timing; } pmu_timing_step_t;
pmu_plan_status_t pmu_resource_alp_hz(uint32_t,uint32_t *);
uint32_t pmu_resource_timing_count(uint32_t);
pmu_plan_status_t pmu_resource_timing_for_clock(uint32_t,uint32_t,pmu_timing_step_t *);
pmu_plan_status_t pmu_resource_timing(uint32_t,pmu_timing_step_t *);
pmu_plan_status_t pmu_resource_dependency(uint32_t,uint32_t *);
pmu_plan_status_t pmu_resource_expand(uint32_t,uint32_t,uint32_t *);
#endif

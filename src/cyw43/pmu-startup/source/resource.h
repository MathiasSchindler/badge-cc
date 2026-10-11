#ifndef U26_PMU_RESOURCE_MODEL_H
#define U26_PMU_RESOURCE_MODEL_H
#include "pll.h"
#define PMU_RESOURCE_WAIT_TICKS 200000u
#define PMU_RESOURCE_MINIMUM_WAIT_US 2000u

typedef enum { PMUR_OVERRIDE_UNKNOWN,PMUR_OVERRIDE_ABSENT,PMUR_OVERRIDE_PRESENT } pmur_override_t;
typedef struct {
    uint32_t qualified,control,clock,sr0,sr1,counter,target_ticks;
} pmur_delay_evidence_t;
typedef struct {
    uint8_t firmware_sha256[32];
    uint32_t capabilities,xtal_khz,pll_verified,exclusive_owner;
    pmur_override_t overrides;
    pmur_delay_evidence_t delay;
} pmur_profile_t;
typedef enum {
    PMUR_OK,PMUR_INPUT,PMUR_PROFILE,PMUR_IO,PMUR_CANCELLED,PMUR_DEADLINE,
    PMUR_CALLBACK,PMUR_READBACK,PMUR_SETTLE_FAILED,PMUR_WAIT_FAILED,PMUR_REENTRY,PMUR_PREPARED
} pmur_status_t;
typedef enum {
    PMUR_VALIDATE,PMUR_GUARD,PMUR_TIMINGS,PMUR_DEPENDENCIES,PMUR_EXPAND,
    PMUR_PREMASK,PMUR_ENABLE_MAX,PMUR_INSTALL_MIN,PMUR_INSTALL_MAX,
    PMUR_SETTLE,PMUR_DELAY,PMUR_VERIFY,PMUR_DONE
} pmur_phase_t;
typedef struct {
    pmur_status_t status;
    pmur_phase_t phase;
    pmu_pll_operation_t operation;
    pmu_pll_callback_t callback_result;
    uint32_t attempted,started,elapsed,last_offset,last_value,index;
    uint32_t reads,writes_attempted,writes_completed,services;
    uint32_t timing_records,dependency_records,dependency_reads;
    uint32_t initial_min,initial_max,minimum,maximum,wait_ticks;
    uint32_t partial_write_uncertainty,external_recovery_required,subsequence_applied;
    uint32_t hardware_qualified,ht_ready,complete_cold_start,source_rx;
    pmu_pll_report_t settling;
} pmur_report_t;
typedef struct {
    void *context;
    pmu_pll_callback_t (*read)(void *,uint32_t,uint32_t,uint32_t *);
    pmu_pll_callback_t (*write)(void *,uint32_t,uint32_t,uint32_t);
    pmu_pll_callback_t (*service)(void *,uint32_t);
    pmu_pll_callback_t (*now)(void *,uint32_t *);
    pmu_pll_callback_t (*wait_ticks)(void *,uint32_t,uint32_t,uint32_t *);
} pmur_io_t;

/* One-shot, zero-initialized report; raw offsets in the ChipCommon window.
   The caller owns the resource selector and keeps D11 reset throughout.
   service must check that ownership/reset contract without using the selector.
   now supplies a bounded monotonic budget clock; callbacks must return within
   budget. wait_ticks waits qualified 18003090 ticks and reports its elapsed
   counter delta. Source-clock stoppage still requires external RP supervision.
   A failed store is uncertain: no subsequent programming or rollback is done.
   Vendor SR-enabled delay evidence does not qualify this no-SR profile. */
pmur_status_t pmu_resource_profile_check(const pmur_profile_t *);
pmur_status_t pmu_resource_model_run(const pmur_profile_t *,const pmu_pll_limits_t *,
                                    const pmur_io_t *,pmur_report_t *);
/* Diagnostic boundary only: applies tables/masks and settles, then returns
   PMUR_PREPARED before the unqualified delay. Recovery remains required.
   It never reports an applied complete subsequence or HT readiness. */
pmur_status_t pmu_resource_prepare_run(const pmur_profile_t *,const pmu_pll_limits_t *,
                                      const pmur_io_t *,pmur_report_t *);
#endif

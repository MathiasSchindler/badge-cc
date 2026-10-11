#ifndef U26_SOURCE_PLL_ACCESS_H
#define U26_SOURCE_PLL_ACCESS_H
#include "pll.h"

#ifndef PMU_PLL_ACCESS_JOURNAL
#define PMU_PLL_ACCESS_JOURNAL 64u
#endif
#define PMU_PLL_ACCESS_POLLS_MAX 65536u
typedef enum { PMU_PLL_OWNER_UNCONFIRMED,PMU_PLL_OWNER_EXCLUSIVE } pmu_pll_owner_t;
typedef enum {
    PMU_PLL_PREMASK_NONE,PMU_PLL_PREMASK_DISABLED,PMU_PLL_PREMASK_SR_OFF,
    PMU_PLL_PREMASK_PLL_CLEAR,PMU_PLL_PREMASK_READ_PLL,PMU_PLL_PREMASK_CLEAR_PLL,
    PMU_PLL_PREMASK_COMMIT,PMU_PLL_PREMASK_APPLIED
} pmu_pll_premask_t;
typedef struct {
    uint32_t sequence,call;
    pmu_pll_operation_t operation;
    uint32_t offset,value;
    pmu_pll_status_t status;
    pmu_pll_callback_t callback_result;
} pmu_pll_access_event_t;
typedef struct {
    pmu_pll_status_t status;
    pmu_pll_operation_t failed_operation;
    pmu_pll_callback_t callback_result;
    uint32_t failed_offset,elapsed,reads,writes_attempted,writes_acknowledged;
    uint32_t clocks,services,delays,barrier_observations,rmw_completed;
    uint32_t last_readback,pll_readbacks[PMU_PLL_WORDS],pll_readback_mask;
    uint32_t partial_write_uncertainty,external_recovery_required,poisoned;
    uint32_t calls,journal_count,journal_overwritten;
    pmu_pll_premask_t premask;
    uint32_t sr0,pll8_before,pll8_readback,commit_readback;
    uint32_t hardware_qualified,complete_cold_start,source_rx;
    pmu_pll_access_event_t journal[PMU_PLL_ACCESS_JOURNAL];
} pmu_pll_access_report_t;
typedef struct {
    uint32_t deadline_ticks,barrier_observations;
} pmu_pll_access_limits_t;

/* The caller must hold exclusive ownership of the bus/core and both indexed
   banks throughout the session. Service callbacks must not use those banks.
   Offsets are aligned tokens in a 4-KiB core window, not mapped addresses.
   All callbacks must be bounded; the supplied budget is in independent ticks.
   delay is required only when binding this adapter to the PLL model. */
typedef struct {
    void *context;
    pmu_pll_callback_t (*read)(void *,uint32_t,uint32_t,uint32_t *);
    pmu_pll_callback_t (*write)(void *,uint32_t,uint32_t,uint32_t);
    pmu_pll_callback_t (*service)(void *,uint32_t);
    pmu_pll_callback_t (*now)(void *,uint32_t *);
    pmu_pll_callback_t (*delay)(void *,uint32_t,uint32_t);
} pmu_pll_raw_io_t;
typedef struct {
    pmu_pll_access_report_t report;
    const pmu_pll_raw_io_t *raw;
    uint32_t initialized,active,revision,started,deadline,poll_limit;
    uint32_t call_started,call_limit,remaining;
} pmu_pll_access_t;

/* Zero-initialize once. Initialization cannot reset an existing/poisoned
   instance. A new instance after any partial failure requires external recovery,
   not merely another ownership assertion. Callback/profile storage is immutable
   and must outlive the session. No target lock acquisition is implied. */
pmu_pll_status_t pmu_pll_access_init(pmu_pll_access_t *,const pmu_pll_raw_io_t *,
                                    uint32_t,const pmu_pll_access_limits_t *,pmu_pll_owner_t);
pmu_pll_status_t pmu_pll_access_rmw(pmu_pll_access_t *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t *);
pmu_pll_status_t pmu_pll_access_bank(pmu_pll_access_t *,pmu_pll_bank_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t *);
pmu_pll_status_t pmu_pll_access_pre_mask(pmu_pll_access_t *,uint32_t,uint32_t);
pmu_pll_status_t pmu_pll_access_run(pmu_pll_access_t *,const pmu_pll_profile_t *,
                                   const pmu_pll_limits_t *,pmu_pll_report_t *);
#endif

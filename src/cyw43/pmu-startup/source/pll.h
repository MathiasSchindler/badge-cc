#ifndef U26_SOURCE_PLL_MODEL_H
#define U26_SOURCE_PLL_MODEL_H
#include <stdint.h>

#define PMU_PLL_WORDS 9u
#define PMU_PLL_HT_MASK 0x70000000u
#define PMU_PLL_CC_HT 0x00020000u
#define PMU_PLL_HT_OBSERVATIONS 1501u
#define PMU_PLL_PENDING_OBSERVATIONS_MAX 65536u

typedef enum {
    PMU_PLL_OVERRIDE_UNKNOWN,PMU_PLL_OVERRIDE_ABSENT,PMU_PLL_OVERRIDE_PRESENT
} pmu_pll_override_t;
typedef struct {
    uint8_t firmware_sha256[32];
    uint32_t capabilities,xtal_khz,spur;
    /* Effective pll0..pll8 provider values, regardless of storage origin. */
    pmu_pll_override_t pll_overrides;
} pmu_pll_profile_t;
typedef enum {
    PMU_PLL_OK,PMU_PLL_INPUT,PMU_PLL_PROFILE,PMU_PLL_IO,PMU_PLL_CANCELLED,
    PMU_PLL_DEADLINE,PMU_PLL_POLL_LIMIT,PMU_PLL_PENDING_TIMEOUT,
    PMU_PLL_TIMER_DISCONTINUITY,PMU_PLL_CALLBACK
} pmu_pll_status_t;
typedef enum {
    PMU_PLL_CB_OK,PMU_PLL_CB_FAILED,PMU_PLL_CB_CANCELLED,PMU_PLL_CB_TIMEOUT
} pmu_pll_callback_t;
typedef enum {
    PMU_PLL_CC_CLOCK=0x1e0u,PMU_PLL_CONTROL=0x600u,
    PMU_PLL_PENDING=0x610u,PMU_PLL_TIMER=0x614u,
    PMU_PLL_MIN=0x618u,PMU_PLL_MAX=0x61cu,
    PMU_PLL_SELECTOR=0x660u,PMU_PLL_DATA=0x664u
} pmu_pll_register_t;
typedef enum { PMU_PLL_BANK_PLL,PMU_PLL_BANK_REGULATOR } pmu_pll_bank_t;
typedef enum {
    PMU_PLL_MODEL_VALIDATE,PMU_PLL_MODEL_INITIALIZE_CONTROL,PMU_PLL_MODEL_MODIFYING_CHECK_PREP,
    PMU_PLL_MODEL_CHECK_CONTROL,PMU_PLL_MODEL_SAVE,
    PMU_PLL_MODEL_SETTLE,PMU_PLL_MODEL_CLEAR_MIN,PMU_PLL_MODEL_CLEAR_MAX,
    PMU_PLL_MODEL_WAIT_OFF,PMU_PLL_MODEL_DELAY_OFF,PMU_PLL_MODEL_PROGRAM_PREP,
    PMU_PLL_MODEL_TABLE,PMU_PLL_MODEL_COMMIT,PMU_PLL_MODEL_RESTORE_MAX,
    PMU_PLL_MODEL_RESTORE_MIN,PMU_PLL_MODEL_WAIT_RESTORE,PMU_PLL_MODEL_END
} pmu_pll_state_t;
typedef enum {
    PMU_PLL_OP_NONE,PMU_PLL_OP_READ,PMU_PLL_OP_WRITE,PMU_PLL_OP_MASKED,
    PMU_PLL_OP_DELAY,PMU_PLL_OP_SERVICE,PMU_PLL_OP_CLOCK
} pmu_pll_operation_t;
typedef struct {
    uint32_t deadline_ticks,pending_observations;
} pmu_pll_limits_t;
typedef struct {
    pmu_pll_status_t status;
    pmu_pll_state_t state;
    pmu_pll_operation_t operation;
    pmu_pll_callback_t callback_result;
    uint32_t last_register,last_value,last_mask,last_index;
    pmu_pll_bank_t last_bank;
    uint32_t started,elapsed,reads,writes_attempted,writes_completed;
    uint32_t masked_attempted,masked_completed,delay_calls,service_calls;
    uint32_t saved_min,saved_max,saved_clock,control_before,control_checked;
    uint32_t initial_control_before,initial_control_requested,initial_control_acknowledged;
    uint32_t pending_zero_observations,pending_quiet_observations,pending_restarts;
    uint32_t ht_off_observations,ht_on_observations,pll_words_written;
    uint32_t commit_attempted,commit_acknowledged,restore_max_written,restore_min_written;
    uint32_t ht_restore_requested,ht_restore_observed,subsequence_applied;
    uint32_t partial_write_uncertainty,external_recovery_required;
    uint32_t hardware_qualified,complete_cold_start,source_rx;
} pmu_pll_report_t;

/* Offsets and banks are model tokens, not mapped pointers. Every callback must
   return within its remaining independent-clock budget; now must be bounded.
   masked_update includes selector RMW and data RMW, each with final readback.
   access.c implements the profile-39 contract without a target MMIO binding.
   Delay arguments are vendor units, not qualified physical microseconds. */
typedef struct {
    void *context;
    pmu_pll_callback_t (*read)(void *,pmu_pll_register_t,uint32_t,uint32_t *);
    pmu_pll_callback_t (*write)(void *,pmu_pll_register_t,uint32_t,uint32_t);
    pmu_pll_callback_t (*masked_update)(void *,pmu_pll_bank_t,uint32_t,uint32_t,uint32_t,uint32_t);
    pmu_pll_callback_t (*delay)(void *,uint32_t,uint32_t);
    pmu_pll_callback_t (*service)(void *,uint32_t);
    pmu_pll_callback_t (*now)(void *,uint32_t *);
} pmu_pll_model_io_t;

extern const uint8_t pmu_pll_firmware_sha256[32];
pmu_pll_status_t pmu_pll_profile_check(const pmu_pll_profile_t *);
pmu_pll_status_t pmu_pll_word(const pmu_pll_profile_t *,uint32_t,uint32_t *);
pmu_pll_status_t pmu_pll_control(const pmu_pll_profile_t *,uint32_t,uint32_t *);
pmu_pll_status_t pmu_pll_model_run(const pmu_pll_profile_t *,const pmu_pll_limits_t *,
                                  const pmu_pll_model_io_t *,pmu_pll_report_t *);
/* Read-only pending/quiet-window operation shared with resource startup.
   Only read, service and now callbacks are used; this does not test HT. */
pmu_pll_status_t pmu_pll_settle_run(const pmu_pll_limits_t *,const pmu_pll_model_io_t *,pmu_pll_report_t *);
#endif

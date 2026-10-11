#ifndef R3_RADIO_STARTUP_H
#define R3_RADIO_STARTUP_H
#include "../phy-access/access.h"
#include "../rx-clock/wait.h"

typedef enum {
    R3_VCO_OK,R3_VCO_ARGUMENT,R3_VCO_RADIO_IO,R3_VCO_CLOCK_IO,R3_VCO_DELAY_IO,
    R3_VCO_CANCELLED,R3_VCO_TIMEOUT,R3_VCO_SAMPLE_LIMIT,R3_VCO_SERVICE_ERROR,R3_VCO_VALUE,
    R3_VCO_PHY_IO,R3_VCO_CORE_IO,R3_VCO_CORE_STATE,R3_VCO_READBACK
} r3_vco_error_t;
typedef enum { R3_VCO_VALIDATE,R3_VCO_TRIGGER,R3_VCO_POLL,R3_VCO_SETTLE,
               R3_VCO_RELEASE,R3_VCO_COMPLETE } r3_vco_phase_t;
typedef struct {
    void *context;
    int (*now)(void *,uint32_t *);
    int (*delay_argument)(void *,uint32_t);
    int (*cancelled)(void *);
} r3_vco_time_t;
typedef struct { uint32_t budget_ticks,sample_limit; } r3_vco_limits_t;
typedef struct {
    r3_vco_error_t error;
    r3_vco_phase_t phase;
    uint32_t radio_operations,io_completed,delay_attempts,delay_completed;
    uint32_t last_delay,nominal_delay_total,last_status,completion_bit_observed,reset_required;
    uint32_t clock_samples,cancel_checks;
    r3_wait_t wait;
    r3_phy_report_t radio;
    uint32_t phy_operations;
    r3_phy_report_t phy;
} r3_vco_report_t;
typedef enum {
    R3_D11_VALIDATE,R3_D11_IDLE,R3_D11_ASSERT,R3_D11_CLOCK,R3_D11_RELEASE,
    R3_D11_PHY_HELD,R3_D11_PHY_CLOCK,R3_D11_COMPLETE
} r3_d11_phase_t;
typedef struct {
    r3_vco_report_t transport;
    r3_d11_phase_t phase;
    uint32_t ioctrl,resetctrl,maccontrol,last_address,last_value,status_polls,writes;
    uint32_t core_prepared,phy_released;
} r3_d11_report_t;
typedef enum {
    R3_GCI_VALIDATE,R3_GCI_BEFORE,R3_GCI_WRITE,R3_GCI_READBACK,R3_GCI_COMPLETE
} r3_gci_phase_t;
typedef struct {
    r3_vco_report_t transport;
    r3_gci_phase_t phase;
    uint32_t before[4],requested[4],readback[4],steps,writes,last_address,last_value;
    uint32_t resetctrl,ioctrl,maccontrol,capabilities,capabilities_ext;
} r3_gci_report_t;

/* Fixed observed board branch only: PUB169=1, HW50.0=1/HW54.7=0/HW6c.29=1,
   SI18=0x10400000/SI1c=0x1e/CCrev=0x31, no PHY hold, cached channel 0x1001,
   enabled 2G/BW20. Not a generic radio-on API or a claim that RC is fixed.
   Requires exact chip/ROM identity and caller-sealed exclusive ChipCommon IO;
   bus.base still identifies D11, but callbacks access fixed CC addresses directly,
   not through the PHY guard bus. Caller physically checks PHY/PA guards before
   and after, with all six inhibits maintained and independent recovery.
   Planned placement after radio reset/fresh PA arm and before band/tables is a
   variant: stock notifies before generic reset; whole-firmware equivalence is
   not claimed. Preconditions read RESET=0, IO=0x45, MAC/PSM=0x04000404 and caps.
   time.now must be qualified and bounded; no delays are used (delay may be NULL).
   sample_limit must cover four steps (4..100). Full-word readbacks are required;
   any runtime failure/partial uncertainty requires reset, with no cleanup writes.
   Success records notification only, not authorization for radio initialization. */
r3_vco_error_t r3_gci_radio_notify(const r3_phy_bus_t *,const r3_vco_time_t *,
                                   const r3_vco_limits_t *,r3_gci_report_t *);

/* Captured core/PHY protocols with an explicit held-PHY preparation variant,
   not the whole stock reset orchestrator or hardware activation permission.
   Caller must establish chip/ROM identity, HT/clocks, bounded exclusive access,
   calibrated delay arguments, RF conditions and independent recovery.
   band_flags is explicitly 0 or 0x2000; board selection is not inferred.
   prepare requires cold RESET=1/IOCTRL=0 and holds PHY reset across core release.
   phy_start requires that prepared state and applies the target 20-MHz sequence.
   Both require MAC/PSM stopped. Reset invalidates prior PHY/radio guards:
   do not carry a pre-reset guard into later programming. prepare returns with
   reset_required still set: it is a staged endpoint, not completed PHY startup.
   Failure requires recovery; failed release is not retried like the vendor loop. */
r3_vco_error_t r3_d11_prepare(const r3_phy_bus_t *,const r3_vco_time_t *,
                             const r3_vco_limits_t *,uint32_t,r3_d11_report_t *);
r3_vco_error_t r3_d11_phy_start(const r3_phy_bus_t *,const r3_vco_time_t *,
                               const r3_vco_limits_t *,uint32_t,r3_d11_report_t *);

/* This implements the observed calibration protocol, NOT a PLL-lock or no-TX guarantee.
   Full all-ones status is rejected rather than accepted as completion.
   Caller must qualify radio state, exclusive access and delay-argument timing.
   now/delay callbacks return exactly 1 on success, 0 on failure; cancelled returns 0/1.
   All callbacks must be bounded; independent RP power-off is required if one never returns.
   No implicit time-unit conversion, retry or rollback is provided. */
r3_vco_error_t r3_vco_protocol_run(const r3_phy_bus_t *,const r3_vco_time_t *,
                                  const r3_vco_limits_t *,r3_vco_report_t *);
#endif

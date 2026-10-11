#ifndef R3_RX_AGC_H
#define R3_RX_AGC_H
#include "../tx-inhibit/guard.h"

typedef struct {
    /* Strict booleans, corresponding to captured private+0x628, private+0x634,
       and shared+0x12c being nonzero, respectively. No board defaults implied. */
    uint32_t alternate_gain_settings;
    uint32_t threshold_104;
    uint32_t aci_threshold_reset;
    /* Explicit low-eight-bit values from private halfwords +0x602/+0x604.
       Written to PHY 0x787/0x432 high bytes; reject values greater than 255. */
    uint32_t gain_787_high;
    uint32_t gain_432_high;
} r3_rx_agc_config_t;

typedef enum {
    R3_AGC_OK,R3_AGC_ARGUMENT,R3_AGC_GUARD,R3_AGC_PHY_IO,R3_AGC_RADIO_IO
} r3_agc_error_t;
typedef enum {
    R3_AGC_VALIDATE,R3_AGC_SETTINGS,R3_AGC_THRESHOLD,R3_AGC_GAIN,
    R3_AGC_ACI,R3_AGC_RESET,R3_AGC_DEAF,R3_AGC_FINALIZE,R3_AGC_CONFIGURED
} r3_agc_phase_t;
typedef struct {
    r3_agc_error_t error;
    r3_agc_phase_t phase;
    uint32_t phy_operations,radio_operations,io_completed,guard_checks,reset_required;
    r3_phy_report_t access;
} r3_agc_report_t;

/* Register configuration only: exact 0x86f2e8 AGC and 0x86e338 ACI init,
   including the optional ACI threshold reset and final RX reset script.
   alternate_gain_settings selects 0xa4f=97 (else 91), and, only when set,
   0x9b3 bit8=1 / 0x655=0x5000. threshold_104 selects both 0xa57 bytes
   as 104 (else 97); aci_threshold_reset subsequently sets both to 97.
   No opaque vendor object ABI, ACI software state/timestamps, runtime ACI
   adaptation, TX initialization/calibration, or receive-readiness claim.

   Requires qualified clocks/channel/RX tables and an initialized, armed PHY
   and radio-PA participating-writer guard, exclusive bounded bus access,
   stopped MAC/PSM/DMA and independent recovery. Configuration is validated
   and snapshotted before I/O. Stops at first failure; no rollback/retry.
   A failure after I/O starts requires cold recovery. Guard checks around
   reset are register invariants, NOT physical TX-inhibit proof. */
r3_agc_error_t r3_rx_agc_configure(r3_tx_guard_t *,const r3_rx_agc_config_t *,r3_agc_report_t *);
/* Separate receive-finalization primitives, with the same prerequisites.
   deaf is strictly 0/1; PHY 0x440 bit0 is set to !deaf.
   finalize applies only the five fixed receive channel-tail MODs
   (0x440/0x805/0x304/0x315/0x327), never the mixed 0x874a14 wrapper. */
r3_agc_error_t r3_rx_deaf_set(r3_tx_guard_t *,uint32_t deaf,r3_agc_report_t *);
r3_agc_error_t r3_rx_finalize(r3_tx_guard_t *,r3_agc_report_t *);
#endif

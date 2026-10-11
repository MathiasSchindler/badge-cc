#ifndef R3_RX_STARTUP_H
#define R3_RX_STARTUP_H
#include "rx_config.h"
#include "../tx-inhibit/guard.h"

typedef enum {
    R3_RX_PATH_OK,R3_RX_PATH_ARGUMENT,R3_RX_PATH_GUARD,R3_RX_PATH_CONFIG,
    R3_RX_PATH_PHY_IO,R3_RX_PATH_RADIO_IO,R3_RX_PATH_TABLE_IO
} r3_rx_path_error_t;
typedef enum {
    R3_RX_PATH_VALIDATE,R3_RX_PATH_TIMING,R3_RX_PATH_RFSEQ,R3_RX_PATH_POWER,
    R3_RX_PATH_DC,    R3_RX_PATH_RESET,R3_RX_PATH_TAIL,R3_RX_PATH_CONFIGURED,R3_RX_PATH_DEFAULTS,
    R3_RX_PATH_BAND
} r3_rx_path_phase_t;
typedef struct {
    r3_rx_path_error_t error;
    r3_rx_path_phase_t phase;
    uint32_t phy_operations,radio_operations,table_operations,entries_completed;
    uint32_t io_completed,guard_checks,reset_required;
    uint32_t explicit_policy_writes;
    r3_rx_config_report_t config;
    r3_phy_report_t access;
    r3_table_report_t table;
} r3_rx_path_report_t;

/* The fixed 0x8725a8 RX configuration block, not cold startup or RX readiness.
   Requires qualified power/clocks/channel, bounded exclusive bus access, an
   armed participating-writer guard and independent recovery. MAC/PSM/DMA must
   remain stopped. Reset effects and physical TX inhibition are not qualified.
   Failure requires cold recovery, not retry or guessed register restoration. */
r3_rx_path_error_t r3_rx_path_configure(r3_tx_guard_t *,uint32_t,r3_rx_path_report_t *);
/* Reviewed source variant, NOT the stock default-script replay:
   0x44c=0x4000 and 0x938=0x0800 replace the two zero writes. All other
   band/default-script operations are retained; the TX power-index tail is not
   part of this API. Requires the opt-in radio PA policy and all the same
   physical prerequisites as the RX block. No table initialization is implied. */
r3_rx_path_error_t r3_rx_defaults_apply(r3_tx_guard_t *,r3_rx_path_report_t *);
/* Split form for the cold composition: band (two MODs) BEFORE common/gain
   tables, then register defaults (21 operations). Both require armed PHY/PA.
   The combined defaults_apply API above retains its original 23-op behavior. */
r3_rx_path_error_t r3_rx_band_apply(r3_tx_guard_t *,r3_rx_path_report_t *);
r3_rx_path_error_t r3_rx_register_defaults_apply(r3_tx_guard_t *,r3_rx_path_report_t *);
/* Radio/LDO reset only: reuses the captured two-RMW pulse without inserting
   a delay or guard reads between its edges. Requires a fresh armed PHY guard
   with radio PA policy still unbound; arm PA only after this reset.
   PHY fields are checked before/after, not continuously through the pulse.
   Pulse timing and reset retention require independent hardware qualification.
   Success remains a staged endpoint with reset_required=1, not radio readiness. */
r3_rx_path_error_t r3_rx_radio_reset(r3_tx_guard_t *,r3_rx_path_report_t *);
#endif

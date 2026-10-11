#ifndef R3_RX_COLD_START_H
#define R3_RX_COLD_START_H
#include "board.h"
#include "rx_startup.h"

/* Conservative source-only reserve for current clang -Oz M3/M33 builds.
   Measured deepest additive path is 944 bytes (ADC programming through the
   guarded deadline/cancellation adapter). EXCLUDES caller, supplied callbacks
   and interrupt frames: a 1024-byte TOTAL target stack is not an approved fit.
   Recompute with the actual sealed transport/time backend before activation. */
#define R3_RX_COLD_SOURCE_STACK_RESERVE 1024u

typedef enum {
    R3_COLD_OK,R3_COLD_ARGUMENT,R3_COLD_BOARD,R3_COLD_GUARD,R3_COLD_BLOCK,
    R3_COLD_IO,R3_COLD_CLOCK_IO,R3_COLD_DELAY_IO,R3_COLD_CANCELLED,
    R3_COLD_SERVICE,R3_COLD_TIMEOUT,R3_COLD_CLOCK_LIMIT,R3_COLD_CLOCK_STALLED,
    R3_COLD_TABLE_MISMATCH,R3_COLD_DEFAULT_MISMATCH
} r3_rx_cold_error_t;
typedef enum {
    R3_COLD_VALIDATE,R3_COLD_RADIO_RESET,R3_COLD_PA_ARM,R3_COLD_BAND,
    R3_COLD_COMMON,R3_COLD_GAINS,R3_COLD_DEFAULTS,R3_COLD_TABLE_VERIFY,R3_COLD_DEFAULT_VERIFY,R3_COLD_RADIO_INIT,
    R3_COLD_DEAF,R3_COLD_CHANNEL,R3_COLD_RX_PATH,R3_COLD_DC,R3_COLD_AGC,
    R3_COLD_UNDEAF,R3_COLD_FINALIZE,R3_COLD_GUARD_CHECK,R3_COLD_CONFIGURED
} r3_rx_cold_phase_t;
typedef struct {
    uint32_t budget_ticks,max_clock_samples,max_stalled_samples;
    uint32_t calibration_samples,dc_samples;
} r3_rx_cold_limits_t;
typedef enum { R3_COLD_VERIFY_NONE,R3_COLD_VERIFY_TABLE,R3_COLD_VERIFY_PHY } r3_rx_cold_verify_kind_t;
typedef struct {
    r3_rx_cold_verify_kind_t kind;
    uint32_t table_id,index,width,expected,actual;
    uint32_t tables_verified,entries_verified,defaults_verified;
    /* Caller-owned read workspace; compare immediately after each <=16-entry
       read. On mismatch no further I/O occurs, including in later stages. */
    uint32_t values[16];
    r3_table_report_t table;
    r3_phy_report_t phy;
} r3_rx_cold_verify_t;
typedef struct {
    r3_rx_cold_error_t error;
    r3_rx_cold_phase_t phase;
    uint32_t completed_mask,reset_required;
    /* Physical callback counts, including a successful access rejected by its
       post-deadline check. These can exceed inner block/guard success counts. */
    uint32_t io_attempts,io_completed,write_attempts,writes_completed;
    uint32_t delay_attempts,delays_completed,last_delay;
    uint32_t clock_samples,cancel_checks,start,last,elapsed,stalled_samples;
    uint32_t guard_checks,phase_io[R3_COLD_CONFIGURED];
    r3_tx_guard_error_t guard_error;
    r3_rx_path_error_t path_error;
    r3_rx_config_error_t config_error;
    r3_vco_error_t radio_error;
    r3_agc_error_t agc_error;
    r3_rx_cold_verify_t verification;
    /* Only the member belonging to phase is current; retains the failing
       block's partial report. PA_ARM/verification/GUARD_CHECK/CONFIGURED have no new member.
       completed_mask preserves earlier completions. */
    union {
        r3_rx_path_report_t path;
        r3_rx_config_report_t config;
        r3_radio_init_report_t radio;
        r3_radio_channel_report_t channel;
        r3_dccal_report_t dc;
        r3_agc_report_t agc;
    } detail;
} r3_rx_cold_report_t;

/* Pure validation: exact bound profile, self-references and decoded data/CRCs.
   The board must remain immovable and immutable after load and throughout run. */
r3_rx_cold_error_t r3_rx_cold_validate(const r3_rx_board_t *);

/* Read-only recheck through a caller-sealed bus, without changing startup
   phase or writing table/default data. alias64 is an explicit expected
   mapping to test, not a new-domain qualification. Caller owns timing,
   guards, selector policy and sticky first-fault handling. */
r3_rx_cold_error_t r3_rx_cold_recheck(const r3_phy_bus_t *,const r3_rx_board_t *,
                                    r3_rx_cold_verify_t *,uint32_t alias64);

/* A callable source register/calibration composition, NOT RX-ready or hardware
   qualification. Entry is AFTER native D11/PHY release, with a fresh initialized
   PHY-armed guard and PA_UNBOUND. Caller seals chip identity, stopped MAC/PSM/DMA,
   exclusive transport, calibrated delay arguments and independent recovery.
   No PSM/DMA activation, TX table/PAPD/power-index or generic calibration calls.

   Order: radio reset -> fresh PA -> band -> common -> gains -> register defaults
   -> table readback -> exact 0x44c=0x4200 / 0x938=0x0800 readback
   -> full radio init -> deaf -> channel/VCO -> RX path -> DC -> AGC -> undeaf
   -> five receive-finalization MODs -> final guard check.
   All 908 common/gain entries are checked after the defaults' reset writes,
   using the existing guarded table read API in <=16-entry chunks.
   Only the two known static defaults get full
   register readback; self-clearing registers are not blanket-verified.
   A mismatch reports kind/table/index/expected/actual and blocks radio power.
   defaults_apply callers retain their original combined band/default API.

   Time starts after bounded pure input validation. budget_ticks is 1..INT32_MAX;
   max_clock_samples is 1..1000000; max_stalled_samples is 1..max_clock_samples;
   calibration_samples is 1..100, dc_samples is 1..10. now/delay return exactly1;
   cancelled returns0/1. All callbacks MUST be bounded. Wraparound is supported,
   not elapsed spans >=2^31. Frozen time is rejected by the consecutive-equal
   sample limit, and zero overall elapsed time cannot report success.

   The synchronous call temporarily interposes a deadline adapter BELOW the
   existing guard (g->raw), leaving g->bus and its identity intact. Every block
   uses g->bus; only that adapter forwards to the saved raw transport. Both time
   and I/O callbacks are checked before/after completion, including table/AGC
   work. No other writer or recursive use is allowed. Raw transport is restored
   on every exit, including failure; the guard lifecycle/counters are NOT reset.
   First failure is sticky, makes the guard FAULT, and requires caller recovery.
   No rollback, retries, automatic guard destruction or hardware cleanup.

   Report, board, guard, time and limits must be valid/nonoverlapping. Keep the
   report in caller-owned static/workspace storage, not a small target stack.
   Reserve R3_RX_COLD_SOURCE_STACK_RESERVE PLUS the caller/backend/IRQ budgets;
   this is not a claim that an existing 1024-byte firmware stack suffices. */
r3_rx_cold_error_t r3_rx_cold_start(r3_tx_guard_t *,const r3_rx_board_t *,
                                   const r3_vco_time_t *,const r3_rx_cold_limits_t *,
                                   r3_rx_cold_report_t *);
/* Explicit MODE14-bound policy, never a generic table-engine capacity rule.
   Caller must replay the recovered table20 mapping qualification before use.
   The same exact board validation and all256 original table20 writes remain.
   All256 logical readbacks compare against the final natural writer of each
   observed64-cell alias: data[192 + (index & 63)]. Other tables are unchanged.
   Sampled mapping evidence is not universal capacity proof; the full readback
   still tests every logical address after defaults and fails on any mismatch. */
r3_rx_cold_error_t r3_rx_cold_start_table20_alias64(r3_tx_guard_t *,const r3_rx_board_t *,
                                   const r3_vco_time_t *,const r3_rx_cold_limits_t *,
                                   r3_rx_cold_report_t *);
#endif

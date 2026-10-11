#ifndef R3_RX_CONFIG_H
#define R3_RX_CONFIG_H
#include "table.h"

#define R3_RX_TIMING_OPS 7u
#define R3_RX_GAINVAL_COUNT 26u
#define R3_RX_GAIN_COUNT 96u
#define R3_RX_GAINIDX_COUNT 76u
#define R3_RX_COMMON_TABLES 5u
typedef enum { R3_RX_CONFIG_OK,R3_RX_CONFIG_ARGUMENT,R3_RX_CONFIG_VALUE,
               R3_RX_CONFIG_PHY_IO,R3_RX_CONFIG_TABLE_IO } r3_rx_config_error_t;
typedef struct {
    uint32_t channel,frequency_mhz,farrow_mu,drift_period;
    r3_phy_op_t operations[R3_RX_TIMING_OPS];
} r3_rx_timing_t;
typedef struct {
    const uint32_t *gainval,*gain,*gainidx;
    uint32_t gainval_capacity,gain_capacity,gainidx_capacity;
} r3_rx_gains_t;
typedef struct {
    const uint32_t *data[R3_RX_COMMON_TABLES];
    uint32_t capacity[R3_RX_COMMON_TABLES];
} r3_rx_common_t;
typedef struct {
    r3_rx_config_error_t error;
    uint32_t phy_operations,table_operations,entries_completed,io_completed,reset_required;
    r3_phy_report_t phy;
    r3_table_report_t table;
} r3_rx_config_report_t;

/* Computing channel 1..14 values is not regulatory or hardware activation approval. */
r3_rx_config_error_t r3_rx_timing_plan(uint32_t,r3_rx_timing_t *);
/* Qualified, bounded, exclusive bus access and independent recovery are caller prerequisites.
   These are individual RX configuration blocks, not a complete initializer or no-TX gate. */
r3_rx_config_error_t r3_rx_timing_apply(const r3_phy_bus_t *,uint32_t,r3_rx_config_report_t *);
r3_rx_config_error_t r3_rx_rfseq_write(const r3_phy_bus_t *,r3_rx_config_report_t *);
/* All supplied arrays must be stable, valid and non-overlapping for the complete call. */
r3_rx_config_error_t r3_rx_gains_write(const r3_phy_bus_t *,const r3_rx_gains_t *,r3_rx_config_report_t *);
/* Fixed common descriptor order: 2/1/20/23/0, before RX gains/defaults.
   Table0 includes IQLOCAL data: loading it is not a TX request or proof of
   receive-only electrical behavior. No PAPD/TX-gain initializer is invoked.
   Every input is validated before I/O; data suitability remains a prerequisite. */
r3_rx_config_error_t r3_rx_common_write(const r3_phy_bus_t *,const r3_rx_common_t *,r3_rx_config_report_t *);
#endif

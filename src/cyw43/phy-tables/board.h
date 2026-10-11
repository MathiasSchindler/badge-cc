#ifndef R3_RX_BOARD_H
#define R3_RX_BOARD_H
#include "rx_config.h"
#include "radio_calibration.h"
#include "agc.h"

#define R3_BOARD_ROM_START 0x1e09020cu
#define R3_BOARD_PACKED_BYTES 2044u
typedef enum { R3_BOARD_OK,R3_BOARD_ARGUMENT,R3_BOARD_IO,R3_BOARD_CHANGED,R3_BOARD_CHECKSUM } r3_board_error_t;
typedef int (*r3_board_read_t)(void *,uint32_t,uint32_t *);
typedef struct {
    uint32_t ready,core_flags,xtal_hz;
    r3_rx_common_t common;
    r3_rx_gains_t gains;
    r3_radio_channel_t channel;
    r3_rx_agc_config_t agc;
    uint32_t common_values[710],gainval[26],gain[96],gainidx[76];
    uint8_t tia8[52];
    uint16_t tia16[56],channel_words[31];
    uint8_t packed[R3_BOARD_PACKED_BYTES];
} r3_rx_board_t;
typedef struct {
    r3_board_error_t error;
    uint32_t reads,bytes_verified,address,value,checksums[5];
} r3_board_report_t;

/* Fixed research-badge profile: observed core_flags0/xtal37400000, channel1,
   and schema8 AGC effective inputs {0,0,1,187,181}.
   Requires independently verified chip/ROM identity and bounded exclusive ROM
   reads. The callback may read only the fixed aligned backplane ROM span.
   Two copies and per-dataset CRCs bind configuration, not executable services.
   All inputs/outputs must be valid and non-overlapping for the complete call.
   On failure ready stays0; no consumer may use partially populated arrays.
   After success the object must stay at its original address and immutable:
   its views point into its own storage. No peripheral writes or RX permission. */
r3_board_error_t r3_rx_board_load(r3_board_read_t,void *,r3_rx_board_t *,r3_board_report_t *);
#endif

#ifndef PICO2W_SX1262_H
#define PICO2W_SX1262_H

#include "types.h"

#define SX1262_INIT_ERROR (-3001)
#define SX1262_TIMER_ERROR (-3002)
#define SX1262_BUSY_ERROR (-3003)
#define SX1262_SPI_ERROR (-3004)
#define SX1262_RESPONSE_ERROR (-3005)
#define SX1262_DEVICE_ERROR (-3006)
#define SX1262_CANCELLED (-3007)

typedef struct {
    int32_t error;
    uint8_t samples[16];
} sx1262_result_t;

/* service returns zero to continue, or an error to cancel the probe. */
void sx1262_probe(sx1262_result_t *result, int32_t (*service)(void));

/*
 * Shared bounded transport. delay_us != 0 waits that long; delay_us == 0
 * waits for BUSY low (100 ms deadline). service runs on every iteration and
 * a nonzero result cancels. Iterations are capped, so a stopped timer
 * cannot hang.
 */
int32_t sx1262_wait(int32_t (*service)(void), uint32_t delay_us);

/*
 * One NSS-framed command: waits BUSY low, clocks out[0..length) receiving
 * into in, then clocks tail_length zero bytes receiving into tail, deselects
 * and waits BUSY low again. NSS is released on every path.
 */
int32_t sx1262_transfer(const uint8_t *out, uint8_t *in, uint32_t length,
                        uint8_t *tail, uint32_t tail_length,
                        int32_t (*service)(void));

#endif
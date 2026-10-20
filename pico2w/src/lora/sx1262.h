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

#endif

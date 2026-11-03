#ifndef LOGI_HOST_BOARD_H
#define LOGI_HOST_BOARD_H

#include <stdint.h>

#define LOGI_DP 12u
#define LOGI_DM 13u
#define LOGI_PIO 0x50200000u
#define LOGI_STATUS_MAGIC 0x3144484cu
#define LOGI_SAMPLE_COUNT 16u
#define LOGI_MODE_PASSIVE 1u

typedef struct {
    void *context;
    uint32_t (*read)(void *context, uint32_t address);
    void (*write)(void *context, uint32_t address, uint32_t value);
    uint32_t (*now)(void *context);
} logi_io_t;

typedef struct {
    logi_io_t io;
    uint32_t captures, samples, changes, errors, histogram[4], last_word;
    uint32_t started;
    uint8_t previous, pending;
    int32_t error;
} logi_board_t;

int32_t logi_board_init(logi_board_t *board, const logi_io_t *io);
void logi_board_poll(logi_board_t *board);
void logi_board_status(const logi_board_t *board, uint8_t status[64],
                       uint32_t build, uint32_t uptime_ms, uint32_t usb_errors);
const char *logi_line_name(uint32_t state);

#endif

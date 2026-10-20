#ifndef PICO2W_BOOTROM_H
#define PICO2W_BOOTROM_H

#include "types.h"

int32_t rp2350_chip_id(uint32_t id[2]);
int32_t rp2350_schedule_bootsel(uint32_t delay_ms);

#endif

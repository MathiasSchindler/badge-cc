#ifndef PICO2W_LORA_HARDWARE_H
#define PICO2W_LORA_HARDWARE_H

#include "types.h"

int32_t sx1262_hw_init(void);
uint32_t sx1262_hw_time_us(void);
uint8_t sx1262_hw_busy(void);
void sx1262_hw_reset(uint8_t asserted);
void sx1262_hw_select(uint8_t selected);
int32_t sx1262_hw_byte(uint8_t out, uint8_t *in);
void sx1262_hw_release(void);

#endif

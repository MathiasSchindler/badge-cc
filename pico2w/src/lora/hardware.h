#ifndef PICO2W_LORA_HARDWARE_H
#define PICO2W_LORA_HARDWARE_H

#include "types.h"

/*
 * Idempotent: if clk_ref/clk_peri already run from XOSC /1 and TIMER1 ticks
 * at 1 MHz, returns 0 without writing anything (never resets a live TIMER1).
 * Otherwise enables XOSC if needed (same sequence as usb_hw_init) and waits
 * for it to be stable, switches clk_ref and clk_peri to it, resets and
 * starts TIMER1. Touches no GPIO/HAT pins; usable before USB/Wi-Fi init.
 */
int32_t sx1262_hw_clock_init(void);
/* Clock init if needed, then SPI1 reset/configuration and HAT pins. */
int32_t sx1262_hw_init(void);
/* Raw TIMER1 low word in microseconds; wraps about every 71.6 minutes. */
uint32_t sx1262_hw_time_us(void);
uint8_t sx1262_hw_busy(void);
void sx1262_hw_reset(uint8_t asserted);
void sx1262_hw_select(uint8_t selected);
int32_t sx1262_hw_byte(uint8_t out, uint8_t *in);
void sx1262_hw_release(void);

#endif

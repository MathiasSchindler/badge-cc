#ifndef UNIVERSE26_NAMETAG_CLOCK_H
#define UNIVERSE26_NAMETAG_CLOCK_H

#include <stdint.h>

void nametag_clock_snapshot(uint32_t words[12]);
/* Keep the CPU clock source/rate; establish XOSC/12 TIMER0 ticks. */
int32_t nametag_clock_reference(void);
int32_t nametag_clock_frequency(uint32_t source, uint32_t *khz_q5);

#endif

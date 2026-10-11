#ifndef UNIVERSE26_WIFI_BOARD_H
#define UNIVERSE26_WIFI_BOARD_H

#include "../../radio/wifi_gspi.h"

void wifi_board_init(void);
const wifi_wire_t *wifi_board_wire(void);
uint8_t wifi_board_buttons(void);
void wifi_board_trace(uint8_t output[WIFI_TRACE_SIZE]);
void wifi_board_trace_clock_sample(uint32_t ticks, uint32_t frames);
void wifi_board_select_pio(uint8_t enabled);

#endif

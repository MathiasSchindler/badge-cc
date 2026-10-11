#ifndef UNIVERSE26_WIFI_PIO_H
#define UNIVERSE26_WIFI_PIO_H

#include "../../radio/wifi_gspi.h"

wifi_bus_result_t wifi_pio_transfer(const wifi_wire_t *wire, const uint8_t *tx,
                                    uint32_t tx_length, uint8_t *rx, uint32_t rx_length);
const uint32_t *wifi_pio_profile(void);

#endif

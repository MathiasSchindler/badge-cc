#ifndef PICO2W_WIFI_GSPI_H
#define PICO2W_WIFI_GSPI_H

#include "types.h"

#define WIFI_GSPI_MAX_PACKET 2060u
#define WIFI_GSPI_BACKPLANE_PAD 16u

typedef enum {
    WIFI_SIGNAL_ENABLE,
    WIFI_SIGNAL_CLOCK,
    WIFI_SIGNAL_SELECT,
    WIFI_SIGNAL_DATA
} wifi_signal_t;

typedef enum {
    WIFI_ORDER_RESET16,
    WIFI_ORDER_NORMAL32
} wifi_word_order_t;

typedef struct {
    void *context;
    void (*write)(void *context, wifi_signal_t signal, uint8_t high);
    void (*data_output)(void *context, uint8_t enabled);
    uint8_t (*read_data)(void *context);
    uint32_t (*now_us)(void *context);
    void (*service)(void *context);
} wifi_wire_t;

void wifi_wire_init(wifi_wire_t *wire);
int wifi_wire_delay(const wifi_wire_t *wire, uint32_t duration_us);
int wifi_wire_transfer(const wifi_wire_t *wire, const uint8_t *tx, uint32_t tx_length,
                       uint8_t *rx, uint32_t rx_length);
void wifi_wire_power_off(const wifi_wire_t *wire);
int wifi_gspi_read(const wifi_wire_t *wire, uint8_t function, uint32_t address,
                   uint8_t size, wifi_word_order_t order, uint32_t *value);
int wifi_gspi_write(const wifi_wire_t *wire, uint8_t function, uint32_t address,
                    uint8_t size, wifi_word_order_t order, uint32_t value);

#endif

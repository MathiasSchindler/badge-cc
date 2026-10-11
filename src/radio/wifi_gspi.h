#ifndef UNIVERSE26_WIFI_GSPI_H
#define UNIVERSE26_WIFI_GSPI_H

#include <stdint.h>
#include "wifi_trace.h"

#define WIFI_GSPI_TEST_PATTERN 0xfeedbeadu
#define WIFI_GSPI_BACKPLANE_PAD 16u
#define WIFI_GSPI_BUS_CONFIG 0x000200a3u
#define WIFI_GSPI_CONFIG_MASK 0x00ffff7fu

typedef enum {
    WIFI_SIGNAL_ENABLE,
    WIFI_SIGNAL_CLOCK,
    WIFI_SIGNAL_SELECT,
    WIFI_SIGNAL_DATA
} wifi_signal_t;

typedef enum {
    WIFI_BUS_OK,
    WIFI_BUS_BAD_ARGUMENT,
    WIFI_BUS_TIMER_STALLED,
    WIFI_BUS_TRANSFER_TIMEOUT,
    WIFI_BUS_TEST_MISMATCH,
    WIFI_BUS_CONFIG_MISMATCH,
    WIFI_BUS_CLOCK_TIMEOUT,
    WIFI_BUS_BACKPLANE_INVALID,
    WIFI_BUS_RADIO_OFF
} wifi_bus_result_t;

typedef struct wifi_wire {
    void *context;
    void (*write)(void *context, wifi_signal_t signal, uint8_t high);
    void (*data_output)(void *context, uint8_t enabled);
    uint8_t (*read_data)(void *context);
    uint32_t (*now_us)(void *context);
    void (*observe)(void *context, wifi_trace_event_t event,
                    const uint8_t *bytes, uint32_t length);
    wifi_bus_result_t (*transfer)(const struct wifi_wire *wire, const uint8_t *tx,
                                  uint32_t tx_length, uint8_t *rx, uint32_t rx_length);
} wifi_wire_t;

typedef enum {
    WIFI_ORDER_RESET16,
    WIFI_ORDER_NORMAL32
} wifi_word_order_t;

wifi_bus_result_t wifi_wire_delay(const wifi_wire_t *wire, uint32_t duration_us);
void wifi_wire_idle(const wifi_wire_t *wire);
void wifi_wire_power_off(const wifi_wire_t *wire);
wifi_bus_result_t wifi_wire_transfer(const wifi_wire_t *wire,
                                     const uint8_t *tx, uint32_t tx_length,
                                     uint8_t *rx, uint32_t rx_length);
wifi_bus_result_t wifi_gspi_command(uint8_t command[4], uint8_t write,
                                    uint8_t function, uint32_t address,
                                    uint8_t size, wifi_word_order_t order);
void wifi_gspi_encode_word(uint8_t output[4], uint32_t value, wifi_word_order_t order);
uint32_t wifi_gspi_decode_word(const uint8_t input[4], wifi_word_order_t order);
wifi_bus_result_t wifi_gspi_read(const wifi_wire_t *wire, uint8_t function,
                                 uint32_t address, uint8_t size,
                                 wifi_word_order_t order, uint32_t *value);
wifi_bus_result_t wifi_gspi_write(const wifi_wire_t *wire, uint8_t function,
                                  uint32_t address, uint8_t size,
                                  wifi_word_order_t order, uint32_t value);
const char *wifi_bus_result_name(wifi_bus_result_t result);

#endif

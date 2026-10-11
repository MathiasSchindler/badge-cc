#include "wifi_gspi.h"

#define WIFI_HALF_CLOCK_US 2u
#define WIFI_TRANSFER_LIMIT_US 100000u
#define WIFI_STAGNANT_LIMIT 100000u

static int valid_wire(const wifi_wire_t *wire) {
    return wire != 0 && wire->write != 0 && wire->data_output != 0 &&
           wire->read_data != 0 && wire->now_us != 0;
}

wifi_bus_result_t wifi_wire_delay(const wifi_wire_t *wire, uint32_t duration_us) {
    if (!valid_wire(wire) || duration_us > 1000000u) return WIFI_BUS_BAD_ARGUMENT;
    uint32_t start = wire->now_us(wire->context);
    uint32_t previous = start;
    uint32_t stagnant = 0u;
    while (1) {
        uint32_t now = wire->now_us(wire->context);
        if (now - start >= duration_us) return WIFI_BUS_OK;
        if (now == previous) {
            if (++stagnant >= WIFI_STAGNANT_LIMIT) return WIFI_BUS_TIMER_STALLED;
        } else {
            stagnant = 0u;
            previous = now;
        }
    }
}

void wifi_wire_idle(const wifi_wire_t *wire) {
    wire->write(wire->context, WIFI_SIGNAL_SELECT, 1u);
    wire->write(wire->context, WIFI_SIGNAL_CLOCK, 0u);
    wire->data_output(wire->context, 0u);
}

void wifi_wire_power_off(const wifi_wire_t *wire) {
    wifi_wire_idle(wire);
    wire->write(wire->context, WIFI_SIGNAL_ENABLE, 0u);
}

static wifi_bus_result_t half_clock(const wifi_wire_t *wire, uint32_t started) {
    wifi_bus_result_t result = wifi_wire_delay(wire, WIFI_HALF_CLOCK_US);
    if (result != WIFI_BUS_OK) return result;
    return wire->now_us(wire->context) - started >= WIFI_TRANSFER_LIMIT_US ?
           WIFI_BUS_TRANSFER_TIMEOUT : WIFI_BUS_OK;
}

static void observe(const wifi_wire_t *wire, wifi_trace_event_t event,
                     const uint8_t *bytes, uint32_t length) {
    if (wire->observe != 0) wire->observe(wire->context, event, bytes, length);
}

wifi_bus_result_t wifi_wire_transfer(const wifi_wire_t *wire,
                                     const uint8_t *tx, uint32_t tx_length,
                                     uint8_t *rx, uint32_t rx_length) {
    if (!valid_wire(wire)) return WIFI_BUS_BAD_ARGUMENT;
    if (tx == 0 || tx_length == 0u || tx_length > 8u ||
        rx_length > 20u || (rx_length != 0u && rx == 0)) {
        wifi_wire_idle(wire);
        return WIFI_BUS_BAD_ARGUMENT;
    }
    if (wire->transfer != 0) return wire->transfer(wire, tx, tx_length, rx, rx_length);
    for (uint32_t i = 0; i < rx_length; ++i) rx[i] = 0u;
    wifi_wire_idle(wire);
    observe(wire, WIFI_TRACE_IDLE, tx, tx_length);
    uint32_t started = wire->now_us(wire->context);
    wifi_bus_result_t result = half_clock(wire, started);
    if (result != WIFI_BUS_OK) goto finished;
    wire->write(wire->context, WIFI_SIGNAL_SELECT, 0u);
    observe(wire, WIFI_TRACE_SELECTED, 0, 0u);
    result = half_clock(wire, started);
    if (result != WIFI_BUS_OK) goto finished;
    wire->write(wire->context, WIFI_SIGNAL_DATA, 0u);
    wire->data_output(wire->context, 1u);
    observe(wire, WIFI_TRACE_DRIVING, 0, 0u);
    for (uint32_t i = 0; i < tx_length; ++i) {
        for (uint32_t bit = 0; bit < 8u; ++bit) {
            wire->write(wire->context, WIFI_SIGNAL_DATA,
                        (uint8_t)((tx[i] >> (7u - bit)) & 1u));
            result = half_clock(wire, started);
            if (result != WIFI_BUS_OK) goto finished;
            wire->write(wire->context, WIFI_SIGNAL_CLOCK, 1u);
            result = half_clock(wire, started);
            if (result != WIFI_BUS_OK) goto finished;
            wire->write(wire->context, WIFI_SIGNAL_CLOCK, 0u);
        }
    }
    /* Turn around the shared data wire at clock-low, without adding a clock. */
    wire->data_output(wire->context, 0u);
    observe(wire, WIFI_TRACE_RELEASED, 0, 0u);
    for (uint32_t i = 0; i < rx_length; ++i) {
        for (uint32_t bit = 0; bit < 8u; ++bit) {
            result = half_clock(wire, started);
            if (result != WIFI_BUS_OK) goto finished;
            /* Normal-speed radio output advances on the rising edge. */
            rx[i] = (uint8_t)((rx[i] << 1u) | (wire->read_data(wire->context) & 1u));
            wire->write(wire->context, WIFI_SIGNAL_CLOCK, 1u);
            result = half_clock(wire, started);
            if (result != WIFI_BUS_OK) goto finished;
            wire->write(wire->context, WIFI_SIGNAL_CLOCK, 0u);
        }
    }
finished:
    wifi_wire_idle(wire);
    observe(wire, WIFI_TRACE_FINISHED, rx, rx_length);
    return result;
}

void wifi_gspi_encode_word(uint8_t output[4], uint32_t value, wifi_word_order_t order) {
    for (uint32_t i = 0; i < 4u; ++i) {
        uint32_t byte = order == WIFI_ORDER_RESET16 ? (i ^ 1u) : i;
        output[i] = (uint8_t)(value >> (byte * 8u));
    }
}

uint32_t wifi_gspi_decode_word(const uint8_t input[4], wifi_word_order_t order) {
    uint32_t value = 0u;
    for (uint32_t i = 0; i < 4u; ++i) {
        uint32_t byte = order == WIFI_ORDER_RESET16 ? (i ^ 1u) : i;
        value |= (uint32_t)input[i] << (byte * 8u);
    }
    return value;
}

wifi_bus_result_t wifi_gspi_command(uint8_t command[4], uint8_t write,
                                    uint8_t function, uint32_t address,
                                    uint8_t size, wifi_word_order_t order) {
    if (command == 0 || write > 1u || function > 1u || address > 0x1ffffu ||
        (size != 1u && size != 2u && size != 4u) ||
        (order != WIFI_ORDER_RESET16 && order != WIFI_ORDER_NORMAL32) ||
        (order == WIFI_ORDER_RESET16 && (function != 0u || size != 4u))) {
        return WIFI_BUS_BAD_ARGUMENT;
    }
    uint32_t value = ((uint32_t)write << 31u) | (1u << 30u) |
                     ((uint32_t)function << 28u) | (address << 11u) | size;
    wifi_gspi_encode_word(command, value, order);
    return WIFI_BUS_OK;
}

wifi_bus_result_t wifi_gspi_read(const wifi_wire_t *wire, uint8_t function,
                                 uint32_t address, uint8_t size,
                                 wifi_word_order_t order, uint32_t *value) {
    if (value == 0) return WIFI_BUS_BAD_ARGUMENT;
    *value = 0u;
    uint8_t command[4];
    uint8_t reply[4u + WIFI_GSPI_BACKPLANE_PAD];
    wifi_bus_result_t result = wifi_gspi_command(command, 0u, function, address, size, order);
    if (result != WIFI_BUS_OK) return result;
    uint32_t padding = function == 1u ? WIFI_GSPI_BACKPLANE_PAD : 0u;
    result = wifi_wire_transfer(wire, command, sizeof(command), reply, padding + 4u);
    if (result != WIFI_BUS_OK) return result;
    *value = wifi_gspi_decode_word(reply + padding, order);
    if (size == 1u) *value &= 0xffu;
    if (size == 2u) *value &= 0xffffu;
    return WIFI_BUS_OK;
}

wifi_bus_result_t wifi_gspi_write(const wifi_wire_t *wire, uint8_t function,
                                  uint32_t address, uint8_t size,
                                  wifi_word_order_t order, uint32_t value) {
    uint8_t frame[8];
    wifi_bus_result_t result = wifi_gspi_command(frame, 1u, function, address, size, order);
    if (result != WIFI_BUS_OK) return result;
    if (size == 1u && value > 0xffu) return WIFI_BUS_BAD_ARGUMENT;
    if (size == 2u && value > 0xffffu) return WIFI_BUS_BAD_ARGUMENT;
    wifi_gspi_encode_word(frame + 4u, value, order);
    return wifi_wire_transfer(wire, frame, sizeof(frame), 0, 0u);
}

const char *wifi_bus_result_name(wifi_bus_result_t result) {
    switch (result) {
        case WIFI_BUS_OK: return "BUS CHECKS OK";
        case WIFI_BUS_BAD_ARGUMENT: return "INVALID BUS REQUEST";
        case WIFI_BUS_TIMER_STALLED: return "TIMER NOT ADVANCING";
        case WIFI_BUS_TRANSFER_TIMEOUT: return "TRANSFER TIMEOUT";
        case WIFI_BUS_TEST_MISMATCH: return "TEST PATTERN MISMATCH";
        case WIFI_BUS_CONFIG_MISMATCH: return "CONFIG READBACK ERROR";
        case WIFI_BUS_CLOCK_TIMEOUT: return "ALP CLOCK TIMEOUT";
        case WIFI_BUS_BACKPLANE_INVALID: return "BACKPLANE READ ERROR";
        case WIFI_BUS_RADIO_OFF: return "RADIO OFF";
    }
    return "UNKNOWN BUS ERROR";
}

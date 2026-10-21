#include "wifi_gspi.h"

#define GPIO_IN 0xd0000004u
#define GPIO_OUT_SET 0xd0000018u
#define GPIO_OUT_CLEAR 0xd0000020u
#define GPIO_OE_SET 0xd0000038u
#define GPIO_OE_CLEAR 0xd0000040u
#define GPIO_CTRL(pin) (0x40028004u + (pin) * 8u)
#define GPIO_PAD(pin) (0x40038004u + (pin) * 4u)
#define TIMER1_US 0x400b8028u
#define GPIO_REG(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define HALF_CLOCK_US 2u
#define TRANSFER_LIMIT_US 100000u
#define STAGNANT_LIMIT 100000u

static const uint8_t radio_pins[4] = {23u, 29u, 25u, 24u};

static void write_pin(void *context, wifi_signal_t signal, uint8_t high) {
    (void)context;
    uint32_t mask = 1u << radio_pins[signal];
    GPIO_REG(high ? GPIO_OUT_SET : GPIO_OUT_CLEAR) = mask;
}

static void data_direction(void *context, uint8_t output) {
    (void)context;
    if (output) GPIO_REG(GPIO_OE_SET) = 1u << 24u;
    else GPIO_REG(GPIO_OE_CLEAR) = 1u << 24u;
}

static uint8_t data_read(void *context) {
    (void)context;
    return (uint8_t)((GPIO_REG(GPIO_IN) >> 24u) & 1u);
}

static uint32_t timer_read(void *context) {
    (void)context;
    return GPIO_REG(TIMER1_US);
}

static void init_output_pin(uint32_t pin, uint8_t high) {
    GPIO_REG(GPIO_OE_CLEAR) = 1u << pin;
    GPIO_REG(high ? GPIO_OUT_SET : GPIO_OUT_CLEAR) = 1u << pin;
    GPIO_REG(GPIO_CTRL(pin)) = 5u;
    GPIO_REG(GPIO_PAD(pin)) = 0x52u;
    GPIO_REG(GPIO_OE_SET) = 1u << pin;
}

void wifi_wire_init(wifi_wire_t *wire) {
    if (wire == 0) return;
    wire->context = 0;
    wire->write = write_pin;
    wire->data_output = data_direction;
    wire->read_data = data_read;
    wire->now_us = timer_read;
    wire->service = 0;
    init_output_pin(23u, 0u);
    init_output_pin(25u, 1u);
    init_output_pin(29u, 0u);
    GPIO_REG(GPIO_OE_CLEAR) = 1u << 24u;
    GPIO_REG(GPIO_OUT_CLEAR) = 1u << 24u;
    GPIO_REG(GPIO_CTRL(24u)) = 5u;
    GPIO_REG(GPIO_PAD(24u)) = 0x46u;
}

static int valid_wire(const wifi_wire_t *wire) {
    return wire != 0 && wire->write != 0 && wire->data_output != 0 &&
           wire->read_data != 0 && wire->now_us != 0;
}

int wifi_wire_delay(const wifi_wire_t *wire, uint32_t duration_us) {
    uint32_t start, previous, stagnant = 0u;
    if (!valid_wire(wire) || duration_us > 1000000u) return 0;
    start = wire->now_us(wire->context);
    previous = start;
    uint32_t serviced = start;
    for (;;) {
        uint32_t now = wire->now_us(wire->context);
        if (wire->service != 0 && now - serviced >= 1000u) {
            serviced = now;
            wire->service(wire->context);
        }
        if (now - start >= duration_us) return 1;
        if (now == previous) {
            if (++stagnant >= STAGNANT_LIMIT) return 0;
        } else {
            stagnant = 0u;
            previous = now;
        }
    }
}

static void idle(const wifi_wire_t *wire) {
    wire->write(wire->context, WIFI_SIGNAL_SELECT, 1u);
    wire->write(wire->context, WIFI_SIGNAL_CLOCK, 0u);
    wire->data_output(wire->context, 0u);
}

void wifi_wire_power_off(const wifi_wire_t *wire) {
    if (!valid_wire(wire)) return;
    idle(wire);
    wire->write(wire->context, WIFI_SIGNAL_ENABLE, 0u);
}

static int half_clock(const wifi_wire_t *wire, uint32_t start) {
    if (!wifi_wire_delay(wire, HALF_CLOCK_US)) return 0;
    return wire->now_us(wire->context) - start < TRANSFER_LIMIT_US;
}

int wifi_wire_transfer(const wifi_wire_t *wire, const uint8_t *tx, uint32_t tx_length,
                       uint8_t *rx, uint32_t rx_length) {
    uint32_t start;
    if (!valid_wire(wire) || tx == 0 || tx_length == 0u ||
        tx_length > WIFI_GSPI_MAX_PACKET || rx_length > WIFI_GSPI_MAX_PACKET ||
        (rx_length != 0u && rx == 0)) return 0;
    start = wire->now_us(wire->context);
    idle(wire);
    if (!half_clock(wire, start)) goto failed;
    wire->write(wire->context, WIFI_SIGNAL_SELECT, 0u);
    if (!half_clock(wire, start)) goto failed;
    wire->write(wire->context, WIFI_SIGNAL_DATA, 0u);
    wire->data_output(wire->context, 1u);
    for (uint32_t i = 0u; i < tx_length; ++i) {
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            wire->write(wire->context, WIFI_SIGNAL_DATA,
                        (uint8_t)((tx[i] >> (7u - bit)) & 1u));
            if (!half_clock(wire, start)) goto failed;
            wire->write(wire->context, WIFI_SIGNAL_CLOCK, 1u);
            if (!half_clock(wire, start)) goto failed;
            wire->write(wire->context, WIFI_SIGNAL_CLOCK, 0u);
        }
    }
    wire->data_output(wire->context, 0u);
    for (uint32_t i = 0u; i < rx_length; ++i) {
        rx[i] = 0u;
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            if (!half_clock(wire, start)) goto failed;
            rx[i] = (uint8_t)((rx[i] << 1u) |
                              (wire->read_data(wire->context) & 1u));
            wire->write(wire->context, WIFI_SIGNAL_CLOCK, 1u);
            if (!half_clock(wire, start)) goto failed;
            wire->write(wire->context, WIFI_SIGNAL_CLOCK, 0u);
        }
    }
    idle(wire);
    if (wire->service != 0) wire->service(wire->context);
    return 1;
failed:
    idle(wire);
    if (wire->service != 0) wire->service(wire->context);
    return 0;
}

static void encode(uint8_t output[4], uint32_t value, wifi_word_order_t order) {
    for (uint32_t i = 0u; i < 4u; ++i) {
        uint32_t byte = order == WIFI_ORDER_RESET16 ? (i ^ 1u) : i;
        output[i] = (uint8_t)(value >> (byte * 8u));
    }
}

static uint32_t decode(const uint8_t input[4], wifi_word_order_t order) {
    uint32_t value = 0u;
    for (uint32_t i = 0u; i < 4u; ++i) {
        uint32_t byte = order == WIFI_ORDER_RESET16 ? (i ^ 1u) : i;
        value |= (uint32_t)input[i] << (byte * 8u);
    }
    return value;
}

static int command(uint8_t output[4], uint8_t write, uint8_t function,
                   uint32_t address, uint8_t size, wifi_word_order_t order) {
    uint32_t value;
    if (write > 1u || function > 2u || address > 0x1ffffu ||
        (size != 1u && size != 2u && size != 4u) ||
        (order != WIFI_ORDER_RESET16 && order != WIFI_ORDER_NORMAL32) ||
        (order == WIFI_ORDER_RESET16 && (function != 0u || size != 4u))) return 0;
    value = ((uint32_t)write << 31u) | (1u << 30u) |
            ((uint32_t)function << 28u) | (address << 11u) | size;
    encode(output, value, order);
    return 1;
}

int wifi_gspi_read(const wifi_wire_t *wire, uint8_t function, uint32_t address,
                   uint8_t size, wifi_word_order_t order, uint32_t *value) {
    uint8_t request[4], reply[4u + WIFI_GSPI_BACKPLANE_PAD];
    uint32_t padding = function == 1u ? WIFI_GSPI_BACKPLANE_PAD : 0u;
    if (value == 0 || !command(request, 0u, function, address, size, order)) return 0;
    if (!wifi_wire_transfer(wire, request, 4u, reply, padding + 4u)) return 0;
    *value = decode(reply + padding, order);
    if (size == 1u) *value &= 0xffu;
    if (size == 2u) *value &= 0xffffu;
    return 1;
}

int wifi_gspi_write(const wifi_wire_t *wire, uint8_t function, uint32_t address,
                    uint8_t size, wifi_word_order_t order, uint32_t value) {
    uint8_t request[8];
    if ((size == 1u && value > 0xffu) || (size == 2u && value > 0xffffu) ||
        !command(request, 1u, function, address, size, order)) return 0;
    encode(request + 4u, value, order);
    return wifi_wire_transfer(wire, request, 8u, 0, 0u);
}

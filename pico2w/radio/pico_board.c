#include "../../src/platform/badger/wifi_board.h"
#include "../../src/platform/badger/wifi_pio.h"

#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))

static void write_signal(void *context, wifi_signal_t signal, uint8_t high) {
    (void)context;
    static const uint8_t pins[] = {23u, 29u, 25u, 24u};
    REG(high ? 0xd0000018u : 0xd0000020u) = 1u << pins[signal];
}

static void data_output(void *context, uint8_t enabled) {
    (void)context;
    REG(enabled ? 0xd0000038u : 0xd0000040u) = 1u << 24u;
}

static uint8_t read_data(void *context) {
    (void)context;
    return (uint8_t)((REG(0xd0000004u) >> 24u) & 1u);
}

static uint32_t time_us(void *context) {
    (void)context;
    return REG(0x400b000cu);
}

static wifi_wire_t wire = {
    .write = write_signal, .data_output = data_output,
    .read_data = read_data, .now_us = time_us,
};

void wifi_board_init(void) {
    static const uint8_t pins[] = {23u, 29u, 25u, 24u};
    REG(0xd0000040u) = (1u << 23u) | (1u << 29u) | (1u << 25u) | (1u << 24u);
    REG(0xd0000020u) = (1u << 23u) | (1u << 29u) | (1u << 24u);
    REG(0xd0000018u) = 1u << 25u;
    for (uint32_t i = 0u; i < sizeof(pins); ++i) {
        REG(0x40028004u + pins[i] * 8u) = 5u;
        REG(0x40038004u + pins[i] * 4u) = pins[i] == 24u ? 0x46u : 0x52u;
    }
    REG(0xd0000038u) = (1u << 23u) | (1u << 29u) | (1u << 25u);
    wire.transfer = 0;
}

void wifi_board_select_pio(uint8_t enabled) {
    wire.transfer = enabled ? wifi_pio_transfer : 0;
}

const wifi_wire_t *wifi_board_wire(void) { return &wire; }

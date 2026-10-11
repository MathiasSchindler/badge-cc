#include "wifi_board.h"
#include "wifi_pio.h"

#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define PAD(pin) REG(0x40038004u + (pin) * 4u)
#define CTRL(pin) REG(0x40028004u + (pin) * 8u)
#define OUT_SET REG(0xd0000018u)
#define OUT_CLEAR REG(0xd0000020u)
#define OE_SET REG(0xd0000038u)
#define OE_CLEAR REG(0xd0000040u)
#define INPUT REG(0xd0000004u)

static const uint8_t radio_pins[] = {23u, 29u, 25u, 24u};
static uint32_t trace[WIFI_TRACE_WORDS];
static uint8_t trace_started;
static uint8_t trace_finished;
static wifi_wire_t wire;

static void snapshot(uint32_t index) {
    uint32_t base = 29u + index * 5u;
    trace[base] = REG(0x400b000cu);
    trace[base+1u] = INPUT;
    trace[base+2u] = REG(0xd0000010u);
    trace[base+3u] = REG(0xd0000030u);
    trace[base+4u] = REG(0x40028000u + 24u * 8u);
    trace[1] |= 1u << index;
}

static void write_signal(void *context, wifi_signal_t signal, uint8_t high) {
    (void)context;
    uint32_t mask = 1u << radio_pins[signal];
    if (high) OUT_SET = mask;
    else OUT_CLEAR = mask;
    if (signal == WIFI_SIGNAL_ENABLE && high && !trace_started) snapshot(0u);
    if (signal == WIFI_SIGNAL_CLOCK && high && trace_started && !trace_finished &&
        (REG(0xd0000030u) & (1u << 24u))) {
        trace[60] = (trace[60] << 1u) | ((INPUT >> 24u) & 1u);
        ++trace[61];
    }
}

static void data_output(void *context, uint8_t enabled) {
    (void)context;
    if (enabled) OE_SET = 1u << 24u;
    else OE_CLEAR = 1u << 24u;
}

static uint8_t read_data(void *context) {
    (void)context;
    return (uint8_t)((INPUT >> 24u) & 1u);
}

static uint32_t time_us(void *context) {
    (void)context;
    return REG(0x400b000cu);
}

static void observe(void *context, wifi_trace_event_t event, const uint8_t *bytes, uint32_t length) {
    (void)context;
    if (trace_finished) return;
    if (event == WIFI_TRACE_IDLE) {
        trace_started = 1u;
        trace[2] = length;
        for (uint32_t i = 0u; i < length && i < 8u; ++i) trace[6u + i/4u] |= (uint32_t)bytes[i] << (8u * (i%4u));
    }
    if (!trace_started) return;
    snapshot((uint32_t)event + 1u);
    if (event == WIFI_TRACE_FINISHED) {
        trace[3] = length;
        trace[4] = trace[54u] - trace[34u];
        for (uint32_t i = 0u; i < length && i < 20u; ++i) trace[8u+i/4u] |= (uint32_t)bytes[i] << (8u*(i%4u));
        trace_finished = 1u;
        trace[1] |= 1u << 31u;
        wire.observe = 0;
    }
}

static wifi_wire_t wire = {
    .context = 0,
    .write = write_signal,
    .data_output = data_output,
    .read_data = read_data,
    .now_us = time_us,
    .observe = observe,
    .transfer = 0,
};

static void output_pin(uint32_t pin, uint8_t high) {
    OE_CLEAR = 1u << pin;
    if (high) OUT_SET = 1u << pin;
    else OUT_CLEAR = 1u << pin;
    CTRL(pin) = 5u;
    PAD(pin) = 0x52u;
    OE_SET = 1u << pin;
}

void wifi_board_init(void) {
    wire.transfer = 0;
    wire.observe = observe;
    for (uint32_t i = 0u; i < WIFI_TRACE_WORDS; ++i) trace[i] = 0u;
    trace_started = trace_finished = 0u;
    trace[0] = WIFI_TRACE_MAGIC;
    output_pin(23u, 0u);
    output_pin(25u, 1u);
    output_pin(29u, 0u);
    OE_CLEAR = 1u << 24u;
    OUT_CLEAR = 1u << 24u;
    CTRL(24u) = 5u;
    PAD(24u) = 0x46u;
    trace[15] = REG(0x40010030u);
    trace[16] = REG(0x40010034u);
    trace[17] = REG(0x4001003cu);
    trace[18] = REG(0x40010040u);
    trace[19] = REG(0x4010801cu);
    trace[20] = REG(0x40108034u);
    trace[59] = REG(0x40108018u);
    for (uint32_t i = 0u; i < 4u; ++i) {
        trace[21u + i*2u] = CTRL(radio_pins[i]);
        trace[22u + i*2u] = PAD(radio_pins[i]);
    }

    const uint8_t buttons[] = {7u, 9u};
    for (uint32_t i = 0u; i < sizeof(buttons); ++i) {
        OE_CLEAR = 1u << buttons[i];
        CTRL(buttons[i]) = 5u;
        PAD(buttons[i]) = 0xcau;
    }
}

void wifi_board_select_pio(uint8_t enabled) {
    wire.transfer = enabled ? wifi_pio_transfer : 0;
    trace[5] = enabled != 0u;
}

void wifi_board_trace(uint8_t output[WIFI_TRACE_SIZE]) {
    for (uint32_t i = 0u; i < WIFI_TRACE_WORDS; ++i) {
        for (uint32_t byte = 0u; byte < 4u; ++byte) output[i*4u+byte] = (uint8_t)(trace[i] >> (byte*8u));
    }
}

void wifi_board_trace_clock_sample(uint32_t ticks, uint32_t frames) {
    trace[13] = ticks;
    trace[14] = frames;
}
const wifi_wire_t *wifi_board_wire(void) {
    return &wire;
}

uint8_t wifi_board_buttons(void) {
    uint32_t inputs = INPUT;
    return (uint8_t)(((inputs & (1u << 7u)) == 0u ? 1u : 0u) |
                     ((inputs & (1u << 9u)) == 0u ? 2u : 0u));
}

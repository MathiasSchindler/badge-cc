#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lora/sx1262.h"
#include "lora/hardware.h"

enum {
    NORMAL, INIT_FAIL, STOPPED_TIMER, BUSY_HIGH, SPI_FAIL, ZERO_RESPONSE,
    FF_RESPONSE, BAD_STATUS, BAD_TYPE, DEVICE_ERROR, UNSTABLE_SYNC, CANCEL,
    BUSY_AFTER
};

static const uint8_t commands[12][6] = {
    {0x8a, 1}, {0xc0, 0}, {0x11, 0, 0}, {0x17, 0, 0, 0},
    {0x12, 0, 0, 0}, {0x1d, 7, 0x40, 0, 0, 0},
    {0x8a, 0}, {0xc0, 0}, {0x11, 0, 0}, {0x17, 0, 0, 0},
    {0x12, 0, 0, 0}, {0x1d, 7, 0x40, 0, 0, 0}
};
static const uint8_t lengths[12] = {2, 2, 3, 4, 4, 6, 2, 2, 3, 4, 4, 6};
static int fault, selected, reset, released;
static uint32_t now, reset_at, released_at, transactions, byte_index, services;
static uint8_t packet_type;

int32_t sx1262_hw_init(void) {
    return fault == INIT_FAIL ? SX1262_INIT_ERROR : 0;
}

uint32_t sx1262_hw_time_us(void) {
    if (fault != STOPPED_TIMER) now += 10u;
    return now;
}

uint8_t sx1262_hw_busy(void) {
    assert(!reset && !selected);
    return fault == BUSY_HIGH || (fault == BUSY_AFTER && transactions != 0u);
}

void sx1262_hw_reset(uint8_t asserted) {
    assert(!selected);
    reset = asserted;
    if (asserted) reset_at = now;
    else {
        if (fault != CANCEL && fault != STOPPED_TIMER)
            assert(now - reset_at >= 20000u);
        released_at = now;
    }
}

void sx1262_hw_select(uint8_t value) {
    assert(value != selected && !reset);
    selected = value;
    if (value) {
        assert(transactions < 12u);
        assert(now - released_at >= 10000u);
        byte_index = 0u;
    } else {
        if (fault != SPI_FAIL) assert(byte_index == lengths[transactions]);
        ++transactions;
    }
}

int32_t sx1262_hw_byte(uint8_t out, uint8_t *in) {
    assert(selected && transactions < 12u);
    assert(byte_index < lengths[transactions]);
    assert(out == commands[transactions][byte_index]);
    if (fault == SPI_FAIL) return SX1262_SPI_ERROR;
    uint8_t opcode = commands[transactions][0];
    uint8_t value = 0u;
    if (opcode == 0x8au && byte_index == 1u) packet_type = out;
    if (opcode == 0xc0u && byte_index == 1u)
        value = fault == BAD_STATUS ? 0x28u : 0x24u;
    if (opcode == 0x11u && byte_index == 2u)
        value = fault == BAD_TYPE ? 1u : packet_type;
    if (opcode == 0x17u && byte_index == 3u && fault == DEVICE_ERROR) value = 0x20u;
    if (opcode == 0x1du && byte_index == 4u) value = 0x14u;
    if (opcode == 0x1du && byte_index == 5u)
        value = fault == UNSTABLE_SYNC && transactions == 11u ? 0x25u : 0x24u;
    if (fault == ZERO_RESPONSE) value = 0u;
    if (fault == FF_RESPONSE) value = 0xffu;
    *in = value;
    ++byte_index;
    return 0;
}

void sx1262_hw_release(void) {
    assert(!selected && !reset);
    released = 1;
}

static int32_t service(void) {
    ++services;
    return fault == CANCEL ? SX1262_CANCELLED : 0;
}

static sx1262_result_t run_probe(int scenario, uint32_t start) {
    sx1262_result_t result;
    memset(&result, 0xff, sizeof(result));
    fault = scenario;
    selected = reset = released = 0;
    now = start;
    transactions = byte_index = services = 0u;
    packet_type = 0u;
    sx1262_probe(&result, service);
    assert(released && !selected && !reset);
    return result;
}

int main(void) {
    for (uint32_t wrap = 0u; wrap < 2u; ++wrap) {
        sx1262_result_t result = run_probe(NORMAL, wrap ? 0xfffff000u : 0u);
        assert(result.error == 0 && transactions == 12u && packet_type == 0u);
        static const uint8_t expected[16] = {
            0x24, 1, 0, 0, 0, 0, 0x14, 0x24,
            0x24, 0, 0, 0, 0, 0, 0x14, 0x24
        };
        assert(memcmp(result.samples, expected, sizeof(expected)) == 0);
    }
    static const struct { int scenario; int32_t error; } cases[] = {
        {INIT_FAIL, SX1262_INIT_ERROR}, {STOPPED_TIMER, SX1262_TIMER_ERROR},
        {BUSY_HIGH, SX1262_BUSY_ERROR}, {SPI_FAIL, SX1262_SPI_ERROR},
        {ZERO_RESPONSE, SX1262_RESPONSE_ERROR}, {FF_RESPONSE, SX1262_RESPONSE_ERROR},
        {BAD_STATUS, SX1262_RESPONSE_ERROR}, {BAD_TYPE, SX1262_RESPONSE_ERROR},
        {DEVICE_ERROR, SX1262_DEVICE_ERROR}, {UNSTABLE_SYNC, SX1262_RESPONSE_ERROR},
        {CANCEL, SX1262_CANCELLED}, {BUSY_AFTER, SX1262_BUSY_ERROR}
    };
    for (uint32_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        sx1262_result_t result = run_probe(cases[i].scenario, 0u);
        assert(result.error == cases[i].error);
        if (fault == STOPPED_TIMER) assert(services == 1000000u);
        if (fault == BUSY_HIGH || fault == BUSY_AFTER) assert(services < 20000u);
        if (fault == INIT_FAIL) {
            assert(services == 0u && transactions == 0u);
            for (uint32_t j = 0u; j < 16u; ++j) assert(result.samples[j] == 0u);
        }
    }
    puts("PASS: SX1262 exact non-RF command frames, timing, wrap, faults and cleanup");
    return 0;
}

#include <assert.h>
#include <stdio.h>
#include "board.h"
#include "input.h"

static clicker_input_t state;
static uint32_t tick;
static uint8_t report[CLICKER_REPORT_SIZE];

static void sample(uint8_t mask, uint32_t elapsed) {
    tick += elapsed;
    clicker_input_sample(&state, mask, tick);
}

static void stable(uint8_t mask) {
    sample(mask, 1u);
    sample(mask, CLICKER_DEBOUNCE_TICKS);
}

static void expect(uint8_t key) {
    assert(clicker_input_peek(&state, report));
    for (uint32_t i = 0u; i < CLICKER_REPORT_SIZE; ++i) {
        assert(report[i] == (i == 2u ? key : 0u));
    }
    clicker_input_accept(&state);
}

static void begin(uint8_t initial) {
    tick = 0u;
    clicker_input_init(&state);
    clicker_input_set_ready(&state, 1u);
    sample(initial, 0u);
    sample(initial, CLICKER_DEBOUNCE_TICKS);
    expect(0u);
}

static void no_report(void) {
    assert(!clicker_input_peek(&state, report));
}

int main(void) {
    static const uint8_t expected_keys[5] = {
        CLICKER_KEY_PREVIOUS, CLICKER_KEY_BLANK, CLICKER_KEY_NEXT,
        CLICKER_KEY_PREVIOUS, CLICKER_KEY_NEXT
    };
    assert(clicker_button_pins[0] == 7u);
    assert(clicker_button_pins[4] == 6u);
#if CLICKER_BOARD == 2025
    assert(clicker_button_pins[1] == 8u && clicker_button_pins[2] == 9u);
    assert(clicker_button_pins[3] == 10u);
#else
    assert(clicker_button_pins[1] == 10u && clicker_button_pins[2] == 9u);
    assert(clicker_button_pins[3] == 11u);
#endif
    for (uint32_t button = 0u; button < 5u; ++button) {
        begin(0u);
        stable((uint8_t)(1u << button));
        for (uint32_t retry = 0u; retry < 100u; ++retry) {
            assert(clicker_input_peek(&state, report));
            assert(report[2] == expected_keys[button]);
        }
        expect(expected_keys[button]);
        for (uint32_t retry = 0u; retry < 100u; ++retry) {
            assert(clicker_input_peek(&state, report));
            assert(report[2] == 0u);
        }
        expect(0u);
        sample((uint8_t)(1u << button), 1000000u);
        no_report();
        stable(0u);
        stable((uint8_t)(1u << button));
        expect(expected_keys[button]);
        expect(0u);
        no_report();
    }

    begin(0x1fu);
    sample(0x1fu, 1000000u);
    no_report();
    stable(0u);
    stable(0x1fu);
    for (uint32_t i = 0u; i < 5u; ++i) {
        expect(expected_keys[i]);
        expect(0u);
    }
    no_report();

    begin(0u);
    sample(1u, 1u);
    sample(0u, CLICKER_DEBOUNCE_TICKS - 1u);
    stable(0u);
    no_report();
    sample(1u, 1u);
    sample(1u, CLICKER_DEBOUNCE_TICKS - 1u);
    no_report();
    sample(1u, 1u);
    expect(CLICKER_KEY_PREVIOUS);
    stable(0u);
    stable(4u);
    expect(0u);
    expect(CLICKER_KEY_NEXT);
    expect(0u);

    begin(0u);
    tick = 0xfffffff0u;
    sample(1u, 0u);
    sample(1u, CLICKER_DEBOUNCE_TICKS);
    expect(CLICKER_KEY_PREVIOUS);
    expect(0u);

    begin(0u);
    stable(1u);
    expect(CLICKER_KEY_PREVIOUS);
    clicker_input_set_ready(&state, 0u);
    no_report();
    stable(4u);
    clicker_input_set_ready(&state, 1u);
    expect(0u);
    sample(4u, CLICKER_DEBOUNCE_TICKS);
    no_report();
    stable(0u);
    stable(4u);
    expect(CLICKER_KEY_NEXT);
    expect(0u);
    no_report();

    begin(0u);
    for (uint32_t i = 0u; i < CLICKER_QUEUE_SIZE; ++i) {
        stable(1u);
        stable(0u);
    }
    assert(state.count == CLICKER_QUEUE_SIZE && !state.overflow);
    stable(1u);
    assert(state.overflow && state.count == 0u);
    expect(0u);
    stable(0u);
    stable(4u);
    no_report();
    begin(0u);
    assert(!state.overflow);
    clicker_chord_t chord={0u,0u,0u,0u};
    assert(!clicker_chord_update(&chord,0x18u,0x18u,0u) && !chord.blocked);
    assert(!clicker_chord_update(&chord,0x19u,0x19u,100u) && chord.blocked);
    assert(!clicker_chord_update(&chord,0x19u,0x19u,1000099u));
    assert(clicker_chord_update(&chord,0x19u,0x19u,1000100u));
    assert(!clicker_chord_update(&chord,0x19u,0x19u,2000100u));
    assert(!clicker_chord_update(&chord,0u,0u,2000200u) && !chord.blocked);
    assert(!clicker_chord_update(&chord,0x19u,0x19u,3000000u));
    assert(!clicker_chord_update(&chord,0x09u,0x09u,3500000u));
    assert(!clicker_chord_update(&chord,0x19u,0x19u,4000000u));
    assert(!clicker_chord_update(&chord,0x19u,0x19u,4500000u));
    assert(clicker_chord_update(&chord,0x19u,0x19u,5000000u));
    puts("PASS: board profile, five keys, debounce threshold, startup-held suppression,");
    puts("      tap/release ordering, backpressure, hold, reconnect, wrap and overflow");
    return 0;
}

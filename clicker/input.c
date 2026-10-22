#include "input.h"

void clicker_input_init(clicker_input_t *state) {
    for (uint32_t i = 0u; i < CLICKER_BUTTON_COUNT; ++i) state->changed_at[i] = 0u;
    for (uint32_t i = 0u; i < CLICKER_QUEUE_SIZE; ++i) state->keys[i] = 0u;
    state->candidate = state->stable = state->armed = state->ready = 0u;
    state->initialized = state->head = state->count = state->overflow = 0u;
    state->release_pending = 1u;
}

void clicker_input_set_ready(clicker_input_t *state, uint8_t ready) {
    ready = ready != 0u;
    if (ready == state->ready) return;
    state->ready = ready;
    state->armed = state->head = state->count = 0u;
    state->release_pending = 1u;
}

void clicker_input_sample(clicker_input_t *state, uint8_t pressed, uint32_t tick) {
    static const uint8_t keys[CLICKER_BUTTON_COUNT] = {
        CLICKER_KEY_PREVIOUS, CLICKER_KEY_BLANK, CLICKER_KEY_NEXT,
        CLICKER_KEY_PREVIOUS, CLICKER_KEY_NEXT
    };
    if (!state->initialized) {
        state->initialized = 1u;
        state->candidate = state->stable = pressed & 0x1fu;
        for (uint32_t i = 0u; i < CLICKER_BUTTON_COUNT; ++i) {
            state->changed_at[i] = tick;
        }
        return;
    }
    for (uint32_t i = 0u; i < CLICKER_BUTTON_COUNT; ++i) {
        uint8_t bit = (uint8_t)(1u << i);
        if ((pressed & bit) != (state->candidate & bit)) {
            state->candidate ^= bit;
            state->changed_at[i] = tick;
        }
        if (tick - state->changed_at[i] < CLICKER_DEBOUNCE_TICKS) continue;
        if ((pressed & bit) == 0u) {
            state->stable &= (uint8_t)~bit;
            state->armed |= bit;
        } else if ((state->stable & bit) == 0u) {
            state->stable |= bit;
            if (state->ready && (state->armed & bit) != 0u && !state->overflow) {
                if (state->count == CLICKER_QUEUE_SIZE) {
                    state->overflow = 1u;
                    state->head = state->count = 0u;
                    state->release_pending = 1u;
                } else {
                    uint32_t tail = (state->head + state->count) % CLICKER_QUEUE_SIZE;
                    state->keys[tail] = keys[i];
                    ++state->count;
                }
            }
            state->armed &= (uint8_t)~bit;
        }
    }
}

uint8_t clicker_input_peek(const clicker_input_t *state,
                           uint8_t report[CLICKER_REPORT_SIZE]) {
    for (uint32_t i = 0u; i < CLICKER_REPORT_SIZE; ++i) report[i] = 0u;
    if (!state->ready) return 0u;
    if (state->release_pending) return 1u;
    if (state->count == 0u) return 0u;
    report[2] = state->keys[state->head];
    return 1u;
}

void clicker_input_accept(clicker_input_t *state) {
    if (!state->ready) return;
    if (state->release_pending) {
        state->release_pending = 0u;
    } else if (state->count != 0u) {
        state->head = (uint8_t)((state->head + 1u) % CLICKER_QUEUE_SIZE);
        --state->count;
        state->release_pending = 1u;
    }

    uint8_t clicker_chord_update(clicker_chord_t *chord, uint8_t raw,
                                  uint8_t stable, uint32_t tick) {
        uint8_t all=(raw&0x19u)==0x19u;
        if (all) chord->blocked=1u;
        if (all && !chord->all) chord->started=tick;
        chord->all=all;
        if (raw==0u) {chord->blocked=chord->fired=0u;return 0u;}
        if (all && !chord->fired && (stable&0x19u)==0x19u &&
            tick-chord->started>=1000000u) {chord->fired=1u;return 1u;}
        return 0u;
    }
}

#ifndef CLICKER_BOARD_H
#define CLICKER_BOARD_H

#include <stdint.h>

#if CLICKER_BOARD == 2025
#define CLICKER_BOARD_NAME "UNIVERSE 2025"
/* Conference-specific wiring, not the retail Tufty 2350 profile. */
static const uint8_t clicker_button_pins[5] = {7u, 8u, 9u, 10u, 6u};
static const char *const clicker_button_names[5] = {"A", "B", "C", "UP", "DOWN"};
#elif CLICKER_BOARD == 2026
#define CLICKER_BOARD_NAME "UNIVERSE 2026"
static const uint8_t clicker_button_pins[5] = {7u, 10u, 9u, 11u, 6u};
static const char *const clicker_button_names[5] = {
    "LEFT", "ENTER", "RIGHT", "UP", "DOWN"
};
#else
#error Select CLICKER_BOARD=2025 or CLICKER_BOARD=2026
#endif

#endif

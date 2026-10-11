#ifndef UNIVERSE26_DIAGNOSTICS_LOGIC_H
#define UNIVERSE26_DIAGNOSTICS_LOGIC_H

#include <stdint.h>

#define DIAG_BUTTON_COUNT 6u
#define DIAG_LEFT 1u
#define DIAG_RIGHT 2u
#define DIAG_ENTER 4u
#define DIAG_UP 8u
#define DIAG_DOWN 16u
#define DIAG_HOME 32u

typedef struct {
    uint32_t changed_at[DIAG_BUTTON_COUNT];
    uint16_t presses[DIAG_BUTTON_COUNT];
    uint8_t candidate;
    uint8_t stable;
    uint8_t seen;
} diag_buttons_t;

uint8_t diag_buttons_update(diag_buttons_t *state, uint8_t raw, uint32_t now);
uint32_t diag_adc_mv(uint16_t raw, uint32_t multiplier);
uint32_t diag_crc32(const uint8_t *data, uint32_t length);
/* Uncomplemented running state; initialize to UINT32_MAX, complement at end. */
uint32_t diag_crc32_update(uint32_t crc, const uint8_t *data, uint32_t length);
uint32_t diag_sqrt(uint32_t value);
void diag_decimal(char output[12], int32_t value);
void diag_hex(char output[9], uint32_t value, uint32_t digits);

#endif

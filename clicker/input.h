#ifndef CLICKER_INPUT_H
#define CLICKER_INPUT_H

#include <stdint.h>

#define CLICKER_BUTTON_COUNT 5u
#define CLICKER_QUEUE_SIZE 16u
#define CLICKER_DEBOUNCE_TICKS 10000u
#define CLICKER_REPORT_SIZE 8u
#define CLICKER_KEY_PREVIOUS 0x4bu
#define CLICKER_KEY_NEXT 0x4eu
#define CLICKER_KEY_BLANK 0x05u

typedef struct {
    uint32_t changed_at[CLICKER_BUTTON_COUNT];
    uint8_t candidate, stable, armed, ready, initialized;
    uint8_t keys[CLICKER_QUEUE_SIZE];
    uint8_t head, count, release_pending, overflow;
} clicker_input_t;

void clicker_input_init(clicker_input_t *state);
/* A new connection discards offline clicks and waits for stable button releases. */
void clicker_input_set_ready(clicker_input_t *state, uint8_t ready);
void clicker_input_sample(clicker_input_t *state, uint8_t pressed, uint32_t tick);
/* Peek is repeatable under backpressure. Accept only after transport acceptance.
   Reports exclude any Report ID: a future GATT characteristic identifies it. */
uint8_t clicker_input_peek(const clicker_input_t *state,
                           uint8_t report[CLICKER_REPORT_SIZE]);
void clicker_input_accept(clicker_input_t *state);

#endif

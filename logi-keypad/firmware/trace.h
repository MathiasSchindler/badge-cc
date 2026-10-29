#ifndef U26_KEYPAD_TRACE_H
#define U26_KEYPAD_TRACE_H

#include <stdint.h>
#include "../../src/shared/bytes.h"
#include "../../src/tools/diagnostics_logic.h"
#include "../keypad-input.h"

#define KEYPAD_TRACE_SUBMIT 0x70u
#define KEYPAD_TRACE_STATUS 0x71u
#define KEYPAD_TRACE_CAPACITY 64u
#define KEYPAD_TRACE_RECORD 1u
#define KEYPAD_TRACE_CLEAR 2u
#define KEYPAD_TRACE_MAGIC 0x3152544bu
#define KEYPAD_STATUS_MAGIC 0x3153544bu

typedef struct {
    uint8_t wire[64];
    int8_t key, navigation;
} keypad_trace_event_t;

typedef struct {
    keypad_trace_event_t events[KEYPAD_TRACE_CAPACITY];
    uint32_t total, unknown, selected, error;
    uint8_t next, count, frozen;
} keypad_trace_t;

void keypad_trace_init(keypad_trace_t *trace);
void keypad_trace_clear_wire(uint8_t wire[64]);
int keypad_trace_record(uint8_t wire[64], const uint8_t *raw, uint32_t length,
                         uint8_t feature, uint32_t timestamp_ms, uint32_t sequence);
int keypad_trace_wire_valid(const uint8_t wire[64]);
int keypad_trace_accept(keypad_trace_t *trace, const uint8_t wire[64]);
const keypad_trace_event_t *keypad_trace_selected(const keypad_trace_t *trace);
void keypad_trace_browse(keypad_trace_t *trace, int direction);
void keypad_trace_status(const keypad_trace_t *trace, uint8_t status[64],
                          uint32_t build, uint32_t rejected);

#endif

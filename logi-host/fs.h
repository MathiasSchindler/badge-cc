#ifndef LOGI_HOST_FS_H
#define LOGI_HOST_FS_H

#include "board.h"
#include "../logi-keypad/keypad-descriptors.h"
#include "../logi-keypad/keypad-input.h"

#define FS_TX_WORDS 64u
#define FS_RX_BITS 640u
#define FS_PACKET_BYTES 68u
#define FS_REPORT_MAGIC 0x31505346u
#define FS_TRACE_MAGIC 0x31545346u
#define FS_KEYPAD_MAGIC 0x31504b46u
#define FS_KEYPAD_EVENT_MAGIC 0x31454b46u
#define FS_KEYPAD_EVENTS 32u
#define FS_SYS_HZ 144000000u
#if defined(__arm__) && !defined(LOGI_FS_TEST)
#define FS_CRITICAL __attribute__((section(".time_critical.fs")))
#define FS_INLINE_RX __attribute__((always_inline))
#else
#define FS_CRITICAL
#define FS_INLINE_RX
#endif

enum {
    FS_TIMEOUT = -4501, FS_OVERFLOW = -4502, FS_ENCODING = -4503,
    FS_CRC = -4504, FS_PID = -4505, FS_LENGTH = -4506,
    FS_STALL = -4507, FS_TOGGLE = -4508, FS_DESCRIPTOR = -4509,
    FS_LINE = -4510, FS_CLOCK = -4511, FS_FIFO = -4512,
    FS_DEADLINE = -4513, FS_HID_REPLY = -4514, FS_CANCELLED = -4515
};
enum { FS_OUT = 0xe1, FS_IN = 0x69, FS_SOF = 0xa5, FS_SETUP = 0x2d,
       FS_DATA0 = 0xc3, FS_DATA1 = 0x4b, FS_ACK = 0xd2,
       FS_NAK = 0x5a, FS_STALL_PID = 0x1e };

typedef struct { uint32_t words[FS_TX_WORDS]; uint16_t count; uint8_t packets; } fs_tx_t;
static inline void fs_tx_init(fs_tx_t *tx) { tx->count = 0u; tx->packets = 0u; }
typedef struct {
    uint8_t bytes[FS_PACKET_BYTES];
    uint32_t pending, length, data_pid;
    /* RP2350 is little-endian: one load checks the final CRC and pending-bit count. */
    union {
        struct { uint16_t crc; uint8_t bits, state; };
        uint32_t tail;
    };
    int32_t error;
} fs_rx_t;
typedef struct {
    uint8_t pid, length, data[64];
} fs_packet_t;
typedef struct {
    void *context;
    uint32_t (*now)(void *context);
    int32_t (*reset)(void *context);
    int32_t (*exchange)(void *context, const fs_tx_t *tx, uint8_t receive, fs_packet_t *packet);
} fs_bus_t;
typedef struct {
    int32_t error;
    uint32_t tx, rx, timeouts, naks, wire_errors;
    uint16_t generation;
    uint8_t stage, descriptor_length, mps, descriptor[18], last_pid, clock_ready, reset_levels, phase;
    uint32_t trace_words[8];
    uint8_t trace_count, trace_pid;
} fs_result_t;
typedef struct {
    const fs_bus_t *bus;
    fs_result_t *result;
    uint32_t started, last_sof, budget;
    uint16_t frame;
} fs_host_t;
typedef struct {
    fs_host_t host;
    keypad_endpoints_t endpoints;
    uint32_t last_input, last_output, query_started, polls, reports, duplicates, buttons, report_crc, naks;
    int32_t error, cleanup_error;
    uint8_t stage, phase, configured, input_pid, query_sent, query_done, feature, repeated;
} fs_keypad_t;

uint8_t fs_crc5(uint16_t bits);
uint16_t fs_crc16(const uint8_t *bytes, uint16_t length);
int32_t fs_token(fs_tx_t *tx, uint8_t pid, uint8_t address, uint8_t endpoint);
int32_t fs_sof(fs_tx_t *tx, uint16_t frame);
int32_t fs_data(fs_tx_t *tx, uint8_t pid, const uint8_t *bytes, uint8_t length);
int32_t fs_handshake(fs_tx_t *tx, uint8_t pid);
void fs_wire_init(void);
void fs_rx_init(fs_rx_t *rx);
FS_CRITICAL FS_INLINE_RX int32_t fs_rx_feed(fs_rx_t *rx, uint16_t samples, uint8_t count);
FS_CRITICAL int32_t fs_rx_validate(const fs_rx_t *rx, fs_packet_t *packet);
int32_t fs_rx_finish(fs_rx_t *rx, fs_packet_t *packet);
int32_t fs_probe(const fs_bus_t *bus, fs_result_t *result);
int32_t fs_keypad_start(const fs_bus_t *bus, fs_result_t *result, fs_keypad_t *keypad);
int32_t fs_keypad_step(fs_keypad_t *keypad, fs_packet_t *packet);
int32_t fs_keypad_stop(fs_keypad_t *keypad);
void fs_keypad_status(const fs_keypad_t *keypad, uint32_t build, uint16_t generation, uint8_t bytes[64]);
void fs_keypad_event(const fs_keypad_t *keypad, const fs_packet_t *packet, uint32_t build,
                    uint16_t generation, uint8_t bytes[64]);
void fs_report(const fs_result_t *result, uint32_t build, uint8_t line, uint8_t bytes[64]);
void fs_trace_report(const fs_result_t *result, uint32_t build, uint8_t bytes[64]);

/* PIO0 SM0 TX, SM1 RX, SM2 preloaded ACK. PIO1 SM0 input-only trace. */
extern const uint16_t fs_pio_program[32];
extern const uint16_t fs_trace_program[6];
int32_t fs_pio_clock(void);
int32_t fs_pio_reset(void *context);
FS_CRITICAL int32_t fs_pio_exchange(void *context, const fs_tx_t *tx, uint8_t receive, fs_packet_t *packet);
void fs_pio_stop(void);
void fs_pio_trace_finish(fs_result_t *result);
uint32_t fs_pio_now(void *context);

#endif

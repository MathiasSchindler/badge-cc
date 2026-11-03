#ifndef LOGI_HOST_FS_H
#define LOGI_HOST_FS_H

#include "board.h"

#define FS_TX_WORDS 64u
#define FS_RX_BITS 640u
#define FS_PACKET_BYTES 68u
#define FS_REPORT_MAGIC 0x31505346u
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
    FS_DEADLINE = -4513
};
enum { FS_OUT = 0xe1, FS_IN = 0x69, FS_SOF = 0xa5, FS_SETUP = 0x2d,
       FS_DATA0 = 0xc3, FS_DATA1 = 0x4b, FS_ACK = 0xd2,
       FS_NAK = 0x5a, FS_STALL_PID = 0x1e };

typedef struct { uint32_t words[FS_TX_WORDS]; uint16_t count; uint8_t packets; } fs_tx_t;
static inline void fs_tx_init(fs_tx_t *tx) { tx->count = 0u; tx->packets = 0u; }
typedef struct {
    uint8_t bytes[FS_PACKET_BYTES], length;
    uint16_t crc, pending;
    uint8_t bits, state;
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
} fs_result_t;

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
void fs_report(const fs_result_t *result, uint32_t build, uint8_t line, uint8_t bytes[64]);

/* SM0 TX, SM1 RX. Twelve system clocks per full-speed bit at 144 MHz. */
extern const uint16_t fs_pio_program[32];
int32_t fs_pio_clock(void);
int32_t fs_pio_reset(void *context);
FS_CRITICAL int32_t fs_pio_exchange(void *context, const fs_tx_t *tx, uint8_t receive, fs_packet_t *packet);
void fs_pio_stop(void);
uint32_t fs_pio_now(void *context);

#endif

#ifndef CLICKER_BLUETOOTH_H
#define CLICKER_BLUETOOTH_H

#include <stdint.h>

#define CLICKER_BT_PACKET_MAX 512u
#define CLICKER_BT_RING_SIZE 4096u

typedef enum {
    CLICKER_BT_OK, CLICKER_BT_ARGUMENT, CLICKER_BT_IO,
    CLICKER_BT_PATCH, CLICKER_BT_TIMEOUT, CLICKER_BT_POINTER,
    CLICKER_BT_PACKET, CLICKER_BT_COMMAND, CLICKER_BT_CANCELLED
} clicker_bt_error_t;

typedef enum {
    CLICKER_BT_IDLE, CLICKER_BT_PATCHING, CLICKER_BT_READY,
    CLICKER_BT_BUFFERS, CLICKER_BT_AWAKE, CLICKER_BT_HCI
} clicker_bt_stage_t;

typedef struct {
    void *context;
    int (*read32)(void *context, uint32_t address, uint32_t *value);
    int (*write32)(void *context, uint32_t address, uint32_t value);
    int (*read)(void *context, uint32_t address, uint8_t *data, uint32_t length);
    int (*write)(void *context, uint32_t address, const uint8_t *data, uint32_t length);
    uint32_t (*now)(void *context);
    int (*service)(void *context);
} clicker_bt_io_t;

typedef struct {
    clicker_bt_io_t io;
    clicker_bt_stage_t stage;
    clicker_bt_error_t error;
    uint32_t address, expected, actual, patch_bytes;
    uint32_t base, host_control, tx_packets, rx_packets;
    uint16_t opcode;
    uint8_t event, status, credits, started;
    uint8_t frame[CLICKER_BT_PACKET_MAX + 4u];
} clicker_bt_t;

int clicker_bt_start(clicker_bt_t *bt, const clicker_bt_io_t *io,
                      const uint8_t *patch, uint32_t length, uint32_t crc);
/* 1 accepted/received, 0 backpressure/no complete packet, -1 explicit error.
   Buffers contain HCI packets without H4 or the controller's bus header. */
int clicker_bt_send(clicker_bt_t *bt, uint8_t type, const uint8_t *packet, uint32_t length);
int clicker_bt_receive(clicker_bt_t *bt, uint8_t *type, uint8_t *packet,
                        uint32_t capacity, uint32_t *length);
/* Immediate Command Complete operations only; asynchronous HCI procedures
   require a separate host event state machine. Reply includes status byte. */
int clicker_bt_command(clicker_bt_t *bt, uint16_t opcode,
                        const uint8_t *parameters, uint8_t parameter_length,
                        uint8_t *reply, uint32_t capacity, uint32_t *reply_length);

#endif

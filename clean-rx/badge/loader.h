#ifndef CLEAN_RX_LOADER_H
#define CLEAN_RX_LOADER_H

#include "../../src/radio/wifi_gspi.h"

#define CLEAN_MBOX 0x20000u
#define CLEAN_MULTI 0x20d80u
#define CLEAN_DETAIL_MAGIC 0x31585243u

typedef struct {
    const wifi_wire_t *wire;
    int (*service)(void *context);
    void *context;
    uint32_t started;
    uint32_t window;
    uint32_t error;
    uint32_t address;
    uint32_t actual;
    uint32_t verified;
    uint32_t stage;
    uint32_t rx_checked;
    uint32_t rx_resyncs;
    uint32_t rx_status;
    uint8_t window_valid;
    uint8_t rx_stalled;
    uint8_t rx_retries;
} clean_loader_t;

enum {
    CLEAN_OK, CLEAN_BUS, CLEAN_TIMEOUT, CLEAN_CANCELLED,
    CLEAN_READBACK, CLEAN_CONTROLLER, CLEAN_INPUT
};

void clean_loader_init(clean_loader_t *loader, const wifi_wire_t *wire,
                       int (*service)(void *), void *context);
int clean_read(clean_loader_t *loader, uint32_t address, uint32_t *value);
int clean_write(clean_loader_t *loader, uint32_t address, uint32_t value);
int clean_block(clean_loader_t *loader, uint32_t address, uint8_t *bytes,
                uint32_t length, uint8_t write);
int clean_boot(clean_loader_t *loader, const uint8_t *image, uint32_t length,
               uint32_t channel, uint32_t generation, uint8_t tx_enabled);
int clean_wait_state(clean_loader_t *loader, uint32_t expected, uint32_t timeout);
int clean_service(clean_loader_t *loader);
int clean_receive(clean_loader_t *loader, uint8_t packet[544], uint32_t *length);

#endif

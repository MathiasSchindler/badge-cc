#ifndef WIFI_HANDOVER_BLE_PROVISION_H
#define WIFI_HANDOVER_BLE_PROVISION_H

#include "experiment.h"

typedef enum {
    BLE_STARTING, BLE_IDLE, BLE_ADVERTISING, BLE_SCANNING, BLE_CONNECTING,
    BLE_READING, BLE_COMPLETE, BLE_ERROR
} ble_provision_phase_t;

typedef struct {
    ble_provision_phase_t phase;
    bool stack_ready;
    uint8_t host_count;
    uint8_t selected; /* Zero based; UINT8_MAX when nothing is selected. */
    uint32_t session_id;
    int8_t rssi;
    uint8_t local_address[6];
    int error_code; /* Positive HCI/ATT code, or BLE_PROVISION_ERR_* below. */
    char status[40];
} ble_provision_view_t;

enum {
    BLE_PROVISION_ERR_NOT_READY = -1,
    BLE_PROVISION_ERR_BUSY = -2,
    BLE_PROVISION_ERR_NO_HOST = -3,
    BLE_PROVISION_ERR_TIMEOUT = -4,
    BLE_PROVISION_ERR_PROTOCOL = -5,
    BLE_PROVISION_ERR_PEER = -6,
    BLE_PROVISION_ERR_DISCONNECTED = -7,
    BLE_PROVISION_ERR_SESSION = -8
};

typedef bool (*ble_offer_fn)(const uint8_t address[6],
                             handover_credentials_t *out);
typedef void (*ble_received_fn)(const handover_credentials_t *credentials);

/* All APIs and callbacks run exclusively on core0. Initialize once AFTER
 * cyw43_arch_init(); this module owns HCI_POWER_ON. The parent must call
 * cyw43_arch_poll(), then ble_provision_poll(now_ms), every loop. No BTstack
 * run_loop_execute() is needed. Do not invoke user operations before ready.
 * HCI startup has a 15-second deadline; failure latches BLE_ERROR until a
 * badge reset, without powering down the shared radio. now_ms must use the
 * Pico/BTstack boot-time millisecond clock (including its uint32_t wrap).
 *
 * Lab-only unauthenticated, unencrypted provisioning: credentials are NOT
 * private. The offer callback reserves/reuses one of three slots; it runs at
 * most once per connection. received must copy the DTO before returning.
 * Reentrant stop is supported in callbacks; other operations reject a busy
 * connection. Never call these APIs or share the view directly from core1. */
void ble_provision_init(ble_offer_fn offer, ble_received_fn received);
/* Explicit physical host selection only. Each successful call reopens
 * admission. Busy calls are rejected without changing the active operation.
 * Advertising resumes between sequential clients until stop/full/error. */
void ble_provision_host(uint32_t session_id);
void ble_provision_scan(void);
void ble_provision_next(void);
void ble_provision_connect(void);
/* Also cancels initiation/disconnects. Never powers down HCI or CYW43, since
 * Wi-Fi shares the chip. Retry operations after pending teardown completes. */
void ble_provision_stop(void);
void ble_provision_poll(uint32_t now_ms);
const ble_provision_view_t *ble_provision_view(void);

#endif

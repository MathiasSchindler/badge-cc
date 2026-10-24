#ifndef CLEAN_RX_LOADER_H
#define CLEAN_RX_LOADER_H

#include "../../src/radio/wifi_gspi.h"
#include "../src/sdio_ring.h"
#include "../src/frame_limits.h"
#include "../../src/shared/bytes.h"

#define CLEAN_MBOX 0x20000u
#define CLEAN_MULTI 0x20d80u
#define CLEAN_DETAIL_MAGIC 0x31585243u

#define CLEAN_LOADER_DEFAULT_BUDGET_US 6000000u
#define CLEAN_LOADER_STOP_TIMEOUT_US 200000u
#define CLEAN_LOADER_SNAPSHOT_BUDGET_US 1000000u
#define CLEAN_SNAPSHOT_DETAIL 2048u
#define CLEAN_SNAPSHOT_BYTES 44u
#define CLEAN_SNAPSHOT_MAGIC 0x31504e53u
#define CLEAN_SNAPSHOT_FROZEN 1u
#define CLEAN_SNAPSHOT_MAILBOX 2u
#define CLEAN_SNAPSHOT_MULTI 4u
#define CLEAN_SNAPSHOT_HT 8u
#define CLEAN_SNAPSHOT_AMPDU 16u
#define CLEAN_SNAPSHOT_INTERFACE 32u
#define CLEAN_HT_TIMING_DETAIL 1800u
#define CLEAN_HT_TIMING_MAGIC 0x31545448u
#define CLEAN_HT_WITNESS_ACK_DETAIL 1832u
#define CLEAN_HT_WITNESS_ACK_MAGIC 0x31574148u
#define CLEAN_HT_OBSERVER_DETAIL 1856u
#define CLEAN_HT_OBSERVER_MAGIC 0x31534f48u
#define CLEAN_HT_F2_DETAIL 1860u
#define CLEAN_HT_F2_MAGIC 0x31463248u
#define CLEAN_SNAPSHOT_AGGREGATE (CLEAN_SNAPSHOT_FROZEN | CLEAN_SNAPSHOT_MAILBOX | \
                                  CLEAN_SNAPSHOT_MULTI | CLEAN_SNAPSHOT_HT | CLEAN_SNAPSHOT_AMPDU)

static inline uint32_t clean_request_generation(const uint8_t nonce[16]) {
    return badger_bytes_load_u32(nonce) | 1u;
}

static inline int clean_snapshot_available(const uint8_t *detail, const uint8_t nonce[16],
                                           uint32_t required) {
    const uint8_t *s = detail + CLEAN_SNAPSHOT_DETAIL;
    return badger_bytes_load_u32(s) == CLEAN_SNAPSHOT_MAGIC &&
        badger_bytes_load_u32(s + 4u) == 1u && !badger_bytes_load_u32(s + 8u) &&
        (badger_bytes_load_u32(s + 20u) & required) == required &&
        badger_bytes_equal(s + 24u, nonce, 16u) &&
        badger_bytes_load_u32(s + 40u) == clean_request_generation(nonce) &&
        badger_bytes_load_u32(detail + 32u) == 0x4f505258u &&
        badger_bytes_load_u32(detail + 64u) == badger_bytes_load_u32(s + 40u);
}
#define CLEAN_AMPDU_RX_WINDOW_US 4200000u
#define CLEAN_AMPDU_REPORT_BUDGET_US 1000000u
#define CLEAN_AMPDU_RX_BUDGET_US \
    (CLEAN_LOADER_DEFAULT_BUDGET_US + CLEAN_AMPDU_RX_WINDOW_US + \
     CLEAN_LOADER_STOP_TIMEOUT_US + CLEAN_AMPDU_REPORT_BUDGET_US)
#define CLEAN_BADGE_RECOVERY_MS 8000u
#define CLEAN_BADGE_RECOVERY_REFRESH_US 4000000u

_Static_assert(CLEAN_BADGE_RECOVERY_REFRESH_US < CLEAN_BADGE_RECOVERY_MS * 1000u,
               "Recovery must be refreshed before its watchdog expires");
typedef struct {
    const wifi_wire_t *wire;
    int (*service)(void *context);
    void *context;
    uint32_t started;
    uint32_t serviced_at;
    uint64_t elapsed_us;
    uint64_t budget_us;
    uint32_t window;
    uint32_t error;
    uint32_t address;
    uint32_t actual;
    uint32_t verified;
    uint32_t stage;
    uint32_t rx_checked;
    uint32_t rx_resyncs;
    uint32_t rx_status;
    uint32_t rx_consumed;
    uint8_t window_valid;
    uint8_t rx_stalled;
    uint8_t rx_retries;
    uint8_t rx_credit;
} clean_loader_t;

enum {
    CLEAN_OK, CLEAN_BUS, CLEAN_TIMEOUT, CLEAN_CANCELLED,
    CLEAN_READBACK, CLEAN_CONTROLLER, CLEAN_INPUT
};

enum {
    CLEAN_SNAPSHOT_COMPLETE = 1u, CLEAN_SNAPSHOT_FAILED, CLEAN_SNAPSHOT_SKIPPED
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
int clean_wait_command(clean_loader_t *loader, uint32_t command, uint32_t timeout);
int clean_service(clean_loader_t *loader);
int clean_loader_begin_aggregate_rx(clean_loader_t *loader);
int clean_loader_begin_snapshot(const clean_loader_t *operation, clean_loader_t *snapshot);
void clean_loader_finish_snapshot(clean_loader_t *operation, clean_loader_t *snapshot,
                                  uint8_t report[CLEAN_SNAPSHOT_BYTES]);
int clean_snapshot_block(clean_loader_t *loader, uint32_t address, uint8_t *bytes,
                          uint32_t length, uint32_t *sections, uint32_t section);
int clean_check_layout(clean_loader_t *loader);
int clean_receive(clean_loader_t *loader, uint8_t packet[CLEAN_RX_PACKET_BYTES], uint32_t *length);

#endif

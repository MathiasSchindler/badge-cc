#ifndef PICO2W_LORA_LINK_TEST_H
#define PICO2W_LORA_LINK_TEST_H

#include "receiver.h"

#define LINK_FREQUENCY_HZ 869850000u
#define LINK_PACKET_SIZE 36u
#define LINK_INTERVAL_MS 20000u
#define LINK_MAX_ATTEMPTS 6u
#define LINK_STATUS_MAGIC 0x4c573250u
#define LINK_LIMIT_ERROR (-3020)
#define LINK_TX_TIMEOUT (-3021)
#define LINK_PACKET_MAGIC 0x544c3250u

typedef struct {
    uint32_t attempts, transmitted, last_attempt_ms;
    int32_t error, restore_error;
} link_test_t;

int32_t link_test_configure(sx1262_receiver_t *rx);
/* Fixed 36-byte/0 dBm/150 ms TX, then private-profile RX restore.
   Caller must enforce airtime spacing; not linked into receive-only monitor. */
int32_t link_radio_send(sx1262_receiver_t *rx, const uint8_t payload[LINK_PACKET_SIZE],
                        int32_t *restore_error);
int32_t link_test_send(link_test_t *test, sx1262_receiver_t *rx, uint32_t now_ms,
                       const uint32_t chip_id[2], const uint8_t nonce[16]);
void link_test_payload(uint8_t *payload, const uint32_t chip_id[2],
                        uint32_t sequence, const uint8_t nonce[16]);

#endif

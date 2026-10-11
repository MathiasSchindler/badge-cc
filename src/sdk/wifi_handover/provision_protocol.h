#ifndef WIFI_HANDOVER_PROVISION_PROTOCOL_H
#define WIFI_HANDOVER_PROVISION_PROTOCOL_H

#include <stddef.h>
#include "experiment.h"

#define PROVISION_VERSION 1u
#define PROVISION_WIRE_SIZE 73u

/* Fixed wire layout: version/length/slot/channel (one byte each), LE32 session,
 * LE64 token, zero-padded SSID[24], and hexadecimal PSK[32] plus NUL.
 * This experimental transport is unauthenticated and exposes credentials. */
bool provision_encode(const handover_credentials_t *credentials,
                      uint8_t *wire, size_t capacity);
bool provision_decode(const uint8_t *wire, size_t length,
                      handover_credentials_t *credentials);

typedef struct {
    uint8_t wire[PROVISION_WIRE_SIZE];
    size_t length;
    bool failed;
} provision_rx_t;

void provision_rx_init(provision_rx_t *rx);
/* Only contiguous, in-order fragments are accepted; an error is sticky. */
bool provision_rx_append(provision_rx_t *rx, size_t offset,
                         const uint8_t *bytes, size_t length);
/* Call only on query completion. Requires ATT success and selected session. */
bool provision_rx_finish(const provision_rx_t *rx, bool att_success,
                         uint32_t selected_session,
                         handover_credentials_t *credentials);

#endif

#ifndef CLEAN_PEER_LIFECYCLE_H
#define CLEAN_PEER_LIFECYCLE_H

#include "../../src/shared/bytes.h"

#define CLEAN_LIFECYCLE_MODE 32u
#define CLEAN_LIFECYCLE_AP_MODE 3u
#define CLEAN_LIFECYCLE_ETHERNET_BYTES 62u
#define CLEAN_LIFECYCLE_PAYLOAD_BYTES 48u
#define CLEAN_LIFECYCLE_AP_DETAIL 88u
#define CLEAN_LIFECYCLE_AP_MAGIC 0x31504142u

typedef struct {
    uint32_t magic, state, error, cycles, associations, received, echoed, channel;
    uint32_t last_epoch, last_sequence;
    uint8_t mac[6], peer[6], session[6], reserved[2];
    uint32_t epochs[16];
    uint32_t operation, firmware_stage, address, expected, actual;
    uint32_t link_error, command, controller_status;
    uint32_t rx_header, startup_us, elapsed_us;
} clean_lifecycle_ap_report_t;
_Static_assert(CLEAN_LIFECYCLE_AP_DETAIL + sizeof(clean_lifecycle_ap_report_t) <= 256u,
               "Lifecycle AP evidence must fit the existing vendor peer trace");

static inline void clean_lifecycle_payload(uint8_t p[CLEAN_LIFECYCLE_PAYLOAD_BYTES],
        const uint8_t session[6], uint32_t epoch, uint32_t sequence, uint8_t reply) {
    static const uint8_t magic[8] = {'U','2','6','B','1','D','A','T'};
    badger_bytes_copy(p, magic, sizeof(magic));
    badger_bytes_copy(p + 8u, session, 6u);
    badger_bytes_store_u32(p + 14u, epoch);
    badger_bytes_store_u32(p + 18u, sequence);
    p[22] = reply;
    for (uint32_t i = 23u; i < CLEAN_LIFECYCLE_PAYLOAD_BYTES; ++i)
        p[i] = (uint8_t)(i * 29u + session[i % 6u] + epoch + sequence);
}

static inline int clean_lifecycle_payload_valid(const uint8_t *p, uint32_t n,
        const uint8_t session[6], uint32_t epoch, uint32_t sequence, uint8_t reply) {
    uint8_t expected[CLEAN_LIFECYCLE_PAYLOAD_BYTES];
    if (n != sizeof(expected) || !epoch || reply > 1u) return 0;
    clean_lifecycle_payload(expected, session, epoch, sequence, reply);
    return badger_bytes_equal(p, expected, sizeof(expected));
}

#endif

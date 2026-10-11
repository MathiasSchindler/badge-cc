#ifndef R3_RX_VALIDATE_H
#define R3_RX_VALIDATE_H
#include <stdint.h>

#define R3_RX_SLOTS 32u
#define R3_RX_RAM_END 0x80000u
typedef enum {
    R3_RX_ARGUMENT, R3_RX_GEOMETRY, R3_RX_INDEX, R3_RX_EPOCH,
    R3_RX_UNAVAILABLE, R3_RX_READ_ERROR, R3_RX_STALE, R3_RX_COMPLETION,
    R3_RX_SLOT, R3_RX_BUFFER, R3_RX_ALIAS, R3_RX_FORMAT, R3_RX_LENGTH,
    R3_RX_EMPTY, R3_RX_CANDIDATE
} r3_rx_status_t;
typedef enum { R3_RX_NOT_READ, R3_RX_READ_FAILED, R3_RX_READ_OK } r3_rx_read_t;
typedef enum { R3_RX_UNKNOWN_FORMAT, R3_RX_ORDINARY_LENGTH_MODEL } r3_rx_format_t;
typedef struct {
    uint32_t arena_base,arena_bytes,ring_base,ring_bytes,count;
    uint32_t buffer_bytes,rx_offset;
} r3_rx_layout_t;
typedef struct {
    uint32_t consumer,producer,epoch,revision;
} r3_rx_snapshot_t;
typedef struct {
    r3_rx_read_t read;
    uint32_t epoch,revision,completion_index;
} r3_rx_sample_t;
/* Project-owned ledger, not a vendor packet-object layout. */
typedef struct {
    uint32_t id,address,capacity,epoch;
} r3_rx_slot_t;
typedef struct {
    r3_rx_status_t status;
    uint32_t completed,slot,next_consumer,buffer_id,buffer_address;
} r3_rx_candidate_t;
typedef struct { r3_rx_status_t status;uint32_t total_bytes; } r3_rx_length_t;

/* Inputs must be stable, trusted local copies. Addresses remain integer data. */
r3_rx_status_t r3_rx_validate_layout(const r3_rx_layout_t *);
r3_rx_candidate_t r3_rx_validate(const r3_rx_layout_t *,const r3_rx_snapshot_t *,
                                const r3_rx_sample_t *,const r3_rx_slot_t *,uint32_t);
r3_rx_length_t r3_rx_validate_length(r3_rx_format_t,uint32_t,uint32_t,uint32_t);
const char *r3_rx_status_name(r3_rx_status_t);
#endif

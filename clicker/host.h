#ifndef CLICKER_HOST_H
#define CLICKER_HOST_H
#include "bluetooth.h"
#include "bond.h"
#include "gatt.h"
typedef struct { uint16_t opcode; uint8_t length, tag, data[32]; } clicker_command_t;
typedef struct { uint16_t length, offset; uint8_t data[CLICKER_GATT_MTU+4u]; } clicker_acl_t;
typedef struct {
    clicker_bt_t *bt;
    clicker_bonds_t *bonds;
    clicker_gatt_t gatt;
    clicker_command_t commands[8];
    clicker_acl_t output[8];
    uint8_t command_head, command_count, output_head, output_count, waiting, command_tag;
    uint16_t command_opcode, handle, acl_size, outstanding, acl_credit, acl_total;
    uint8_t connected, encrypted, authenticated, advertising, error, last_event, last_status;
    uint8_t own[6], peer[6], peer_type, new_only, closing;
    int8_t session_bond;
    uint32_t notifications, connections, disconnects, pairing_started, switch_started, rejected;
    uint32_t command_started, incoming_started;
    uint8_t request[7], response[7], tk[16], random[16], confirm[16], stk[16], entropy[64];
    uint8_t rng_bytes, pairing, peer_keys, keys_sent, dirty, pair_error;
    uint8_t last_smp, stopped_pairing, encryption_status, disconnect_reason;
    uint32_t passkey;
    clicker_bond_t candidate;
    uint8_t incoming[512];
    uint16_t incoming_size, incoming_expected;
} clicker_host_t;
/* Errors: 1 malformed HCI/L2CAP, 2 bounded queue/credit invariant, 3 HCI command,
   4 transport, 5 storage, 6 entropy/address. Pairing failures are separate. */
int clicker_host_init(clicker_host_t *host, clicker_bt_t *bt, clicker_bonds_t *bonds,
                       const uint8_t address[6], uint16_t acl_size, uint8_t acl_packets);
int clicker_host_pump(clicker_host_t *host);
int clicker_host_packet(clicker_host_t *host, uint8_t type, const uint8_t *packet, uint32_t length);
int clicker_host_ready(const clicker_host_t *host);
int clicker_host_notify(clicker_host_t *host, const uint8_t report[8]);
void clicker_host_new_computer(clicker_host_t *host);
int clicker_host_persist(clicker_host_t *host, const clicker_bond_io_t *io);
#endif

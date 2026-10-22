#ifndef PICOCTL_LORAWAN_H
#define PICOCTL_LORAWAN_H

#include <stddef.h>
#include <stdint.h>

/*
 * Host-side, receive-only LoRaWAN 1.0.x/1.1 PHYPayload header decoding for
 * picoctl listen. Only plaintext fields are shown; FRMPayload stays
 * encrypted and the MIC is not verified (no keys are involved).
 */

typedef struct {
    uint8_t mtype, major, valid;
    uint32_t dev_addr;
    uint8_t fctrl, fopts_length, has_fport, fport;
    uint16_t fcnt16;
    uint32_t frm_length;
    uint8_t join_eui[8], dev_eui[8]; /* big-endian display order */
    uint16_t dev_nonce;
} lorawan_frame_t;

int lorawan_parse(const uint8_t *frame, uint32_t length, lorawan_frame_t *out);
/* NetID type (0..7) and NwkID bits encoded in a DevAddr prefix. */
void lorawan_netid(uint32_t dev_addr, uint32_t *type, uint32_t *nwk_id);
/* Known operator for the DevAddr prefix, or NULL. */
const char *lorawan_operator(uint32_t dev_addr);
const char *lorawan_mtype_name(uint32_t mtype);
/* One printable line without newline. Returns 1 for a structurally valid frame. */
int lorawan_describe(const uint8_t *frame, uint32_t length, char *output, size_t capacity);

#endif

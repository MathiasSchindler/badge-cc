#ifndef CLEAN_PEER_CCK_MONITOR_H
#define CLEAN_PEER_CCK_MONITOR_H

#include "protocol.h"
#include "frame.h"
#include "../src/mac_soak.h"
#include "../../src/shared/bytes.h"

#define CLEAN_CCK_MONITOR_MODE 9u
#define CLEAN_CCK_MONITOR_MAGIC 0x314b4343u

static inline void clean_cck_monitor_record(uint8_t trace[CLEAN_PEER_TRACE_BYTES],
        const uint8_t *m, uint32_t bytes, uint32_t at, uint32_t payload,
        uint32_t role, uint32_t index) {
    badger_bytes_store_u32(trace+56u,usb_report_u32(trace+56u)+1u);
    if (payload>usb_report_u32(trace+60u)) badger_bytes_store_u32(trace+60u,payload);
    uint32_t offset=64u+4u*(role-1u);
    badger_bytes_store_u32(trace+offset,usb_report_u32(trace+offset)|(1u<<(index%10u)));
    badger_bytes_store_u32(trace+72u,bytes);
    badger_bytes_store_u32(trace+76u,usb_report_u32(m+bytes-4u));
    badger_bytes_store_u32(trace+80u,at);
    badger_bytes_zero(trace+128u,128u);
    uint32_t retained=bytes<64u?bytes:64u;
    badger_bytes_copy(trace+128u,m,retained);
    badger_bytes_copy(trace+192u,m+bytes-retained,retained);
}

/* Validate complete fixture MPDUs, including FCS, inside opaque vendor envelopes.
 * This deliberately makes no inference about PHY rate from the payload. */
static inline void clean_cck_monitor_packet(uint8_t trace[CLEAN_PEER_TRACE_BYTES],
        const uint8_t nonce[14], const uint8_t *packet, uint32_t length) {
    badger_bytes_store_u32(trace+32u,usb_report_u32(trace+32u)+1u);
    if (length>usb_report_u32(trace+36u)) badger_bytes_store_u32(trace+36u,length);
    for (uint32_t at=12u; at+60u<=length; ++at) {
        const uint8_t *m=packet+at;
        if (clean_peer_cck(nonce)) {
            if ((m[0]!=8u && m[0]!=0x88u) || (m[1]&~8u)!=2u) continue;
            uint32_t header=m[0]==8u?24u:26u, ethernet=clean_peer_ethernet_length(nonce);
            if (!ethernet || !badger_bytes_equal(m+header+8u,(const uint8_t *)"CHTP",4u) ||
                !badger_bytes_equal(m+header+12u,nonce,14u)) continue;
            uint32_t bytes=header+ethernet-2u;
            badger_bytes_store_u32(trace+40u,usb_report_u32(trace+40u)+1u);
            if (bytes>length-at) {
                badger_bytes_store_u32(trace+44u,usb_report_u32(trace+44u)+1u);
                continue;
            }
            if (diag_crc32(m,bytes-4u)!=usb_report_u32(m+bytes-4u)) {
                badger_bytes_store_u32(trace+52u,usb_report_u32(trace+52u)+1u);
                continue;
            }
            int sequence=clean_peer_mpdu_sequence(m,bytes,nonce);
            if (sequence<0) {
                badger_bytes_store_u32(trace+48u,usb_report_u32(trace+48u)+1u);
                continue;
            }
            clean_cck_monitor_record(trace,m,bytes,at,ethernet-14u,1u,(uint32_t)sequence);
            continue;
        }
        if (m[0]!=8u || (m[1]&~8u)!=1u ||
            usb_report_u32(m+24u)!=CLEAN_SOAK_MAGIC) continue;
        uint32_t index=usb_report_u32(m+28u);
        if (index>=CLEAN_SOAK_MAX_FRAMES) continue;
        uint8_t own[6], peer[6];
        uint32_t role=m[15];
        if (role!=1u && role!=2u) continue;
        clean_soak_address(own,nonce,(uint8_t)role);
        clean_soak_address(peer,nonce,(uint8_t)(3u-role));
        if (!badger_bytes_equal(m+4u,peer,6u) || !badger_bytes_equal(m+10u,own,6u) ||
            !badger_bytes_equal(m+16u,peer,6u)) continue;
        uint32_t payload=clean_soak_payload_size(nonce,index), bytes=payload+28u;
        if (payload<32u) continue;
        uint32_t checked=8u;
        while (checked<32u &&
               m[24u+checked]==clean_soak_pattern(nonce,index,checked)) ++checked;
        if (checked!=32u) continue;
        badger_bytes_store_u32(trace+40u,usb_report_u32(trace+40u)+1u);
        if (bytes>length-at) {
            badger_bytes_store_u32(trace+44u,usb_report_u32(trace+44u)+1u);
            continue;
        }
        while (checked<payload &&
               m[24u+checked]==clean_soak_pattern(nonce,index,checked)) ++checked;
        if (checked!=payload) {
            badger_bytes_store_u32(trace+48u,usb_report_u32(trace+48u)+1u);
            continue;
        }
        uint32_t fcs=diag_crc32(m,bytes-4u);
        if (fcs!=usb_report_u32(m+bytes-4u)) {
            badger_bytes_store_u32(trace+52u,usb_report_u32(trace+52u)+1u);
            continue;
        }
        clean_cck_monitor_record(trace,m,bytes,at,payload,role,index);
    }
}
#endif

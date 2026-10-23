#ifndef CLEAN_PEER_PROTECTED_AP_H
#define CLEAN_PEER_PROTECTED_AP_H
#include "qualification.h"

#define CLEAN_PROTECTED_AP_MODE 6u
#define CLEAN_GROUP_AP_MODE 7u
#define CLEAN_REKEY_AP_MODE 8u
static inline int clean_protected_ap_mode(uint32_t mode) {
    return mode==CLEAN_PROTECTED_AP_MODE || mode==CLEAN_GROUP_AP_MODE ||
        mode==CLEAN_REKEY_AP_MODE;
}
#define CLEAN_PROTECTED_AP_MAGIC 0x31415045u
#define CLEAN_PROTECTED_AP_DETAIL 88u
typedef struct {
    uint32_t magic, state, error, authorized, received, echoed, channel, wpa_auth;
    uint8_t mac[6], peer[6], token[16];
    uint32_t operation;
    uint32_t broadcast_tx, multicast_tx;
    int32_t gtk_rekey_status, ptk_rekey_status;
    uint32_t auth_state, auth_error, group_rekeys, pairwise_rekeys;
    uint32_t m2, m4, g2;
    int32_t controller_status;
    uint32_t unicast_rate, group_rate;
    uint32_t group_probed, pairwise_verified;
} clean_protected_ap_report_t;
_Static_assert(CLEAN_PROTECTED_AP_DETAIL+sizeof(clean_protected_ap_report_t)<=256u,
               "Protected AP evidence must fit the peer trace");

static inline int clean_protected_ap_take_secret(uint8_t pmk[32], uint8_t secret[64],
        uint8_t *ready, const uint8_t nonce[16]) {
    uint8_t digest[20];
    wpa_secret_binding(secret,nonce,nonce[15],digest);
    int valid=*ready && clean_protected_ap_mode(nonce[14]) &&
        wpa_equal(digest,nonce+6u,8u) && clean_qualification_channel(nonce[15]) &&
        nonce[5]>=12u && nonce[5]<=120u;
    wpa_wipe(pmk,32u);
    if (valid) badger_bytes_copy(pmk,secret,32u);
    wpa_wipe(secret,64u); *ready=0u; wpa_wipe(digest,sizeof(digest));
    return valid;
}

static inline int clean_protected_ap_echo(uint8_t *frame, uint32_t length,
        const uint8_t mac[6], const uint8_t peer[6]) {
    static const uint8_t prefix[8] = {'U','2','6','W','P','A','2',0u};
    if (length != 38u || !badger_bytes_equal(frame,mac,6u) ||
        !badger_bytes_equal(frame+6u,peer,6u) || frame[12]!=136u ||
        frame[13]!=181u || !badger_bytes_equal(frame+14u,prefix,8u)) return 0;
    badger_bytes_copy(frame,peer,6u); badger_bytes_copy(frame+6u,mac,6u);
    frame[21]=1u;
    return 1;
}
#endif

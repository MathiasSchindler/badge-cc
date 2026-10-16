#ifndef CLEAN_BADGE_WPA2_H
#define CLEAN_BADGE_WPA2_H

#include "station.h"
#include "../src/wpa2_crypto.h"
#include "../src/mac.h"

enum { WPA_WAIT_M1, WPA_WAIT_M3, WPA_KEYS, WPA_M4_SENT, WPA_PROTECTED };
enum { WPA_OK, WPA_FORMAT, WPA_REPLAY, WPA_NONCE, WPA_MIC, WPA_UNWRAP,
       WPA_RSN, WPA_GTK, WPA_EXHAUSTED, WPA_TIMEOUT, WPA_TRANSPORT, WPA_ASSOCIATION,
       WPA_FRAGMENT };
typedef struct {
    uint8_t pmk[32], snonce[32], anonce[32], ptk[48], gtk[16], gtk_id;
    uint8_t replay[8], m3_hash[20], tx_pn[6], rx_pn[17][6];
    uint8_t mac[6], bssid[6], rsn[128], rsn_length;
    uint8_t stage, error, installed;
    uint32_t m1, m3, m2, m4, protected_rx;
    uint32_t gtk_info;
    uint16_t sequence;
    clean_mac_rx_t rx_mac;
    uint8_t rx_mpdu[CLEAN_MPDU_BYTES], fragment_pn[6];
} clean_wpa_t;

void clean_wpa_init(clean_wpa_t *s, const uint8_t secret[64], const uint8_t mac[6],
                     const uint8_t ap[6], const uint8_t *rsn, uint8_t rn);
/* 1 response produced, 0 unrelated frame, -1 explicit protocol failure. */
int clean_wpa_receive(clean_wpa_t *s, const uint8_t *m, uint32_t n,
                       uint8_t response[512], uint32_t *length);
uint32_t clean_wpa_probe(clean_wpa_t *s, uint8_t frame[512]);
int clean_wpa_protected(clean_wpa_t *s, const uint8_t *m, uint32_t n, uint32_t now);

#endif

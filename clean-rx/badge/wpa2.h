#ifndef CLEAN_BADGE_WPA2_H
#define CLEAN_BADGE_WPA2_H

#include "station.h"
#include "../src/wpa2_crypto.h"
#include "../src/mac.h"
#include "../src/block_ack.h"

enum { WPA_WAIT_M1, WPA_WAIT_M3, WPA_KEYS, WPA_M4_SENT, WPA_PROTECTED };
enum { WPA_OK, WPA_FORMAT, WPA_REPLAY, WPA_NONCE, WPA_MIC, WPA_UNWRAP,
       WPA_RSN, WPA_GTK, WPA_EXHAUSTED, WPA_TIMEOUT, WPA_TRANSPORT, WPA_ASSOCIATION,
       WPA_FRAGMENT };
#define CLEAN_WPA_DEADLINE_US 8000000u
#define CLEAN_WPA_RETRY_US 500000u
#define CLEAN_WPA_MAX_RETRIES 4u
typedef struct {
    uint8_t pmk[32], snonce[32], anonce[32], ptk[48], gtk[16], gtk_id;
    uint8_t replay[8], tx_pn[6], rx_pn[17][6];
    uint8_t mac[6], bssid[6], rsn[128], rsn_length;
    uint8_t stage, error, installed, tx_qos, rx_amsdu;
    uint32_t m1, m3, m2, m4, protected_rx;
    uint32_t gtk_info;
    uint32_t amsdu_rx, amsdu_subframes;
    uint16_t sequence;
    uint8_t timer_started, eapol_version, retries;
    uint32_t started, retry_at, clock, retransmissions;
    clean_mac_rx_t rx_mac;
    clean_qos_rx_t rx_qos;
    uint8_t rx_mpdu[CLEAN_MPDU_BYTES], fragment_pn[6];
    uint8_t qos_fragment_pn[16][6];
} clean_wpa_t;

void clean_wpa_init(clean_wpa_t *s, const uint8_t secret[64], const uint8_t mac[6],
                     const uint8_t ap[6], const uint8_t *rsn, uint8_t rn);
/* Call before RX and TX service, including while a controller TX is pending. */
int clean_wpa_poll(clean_wpa_t *s, uint32_t now, uint8_t response[512], uint32_t *length);
int clean_wpa_abort(clean_wpa_t *s, uint8_t error);
/* 1 response produced, 0 unrelated frame, -1 explicit protocol failure. */
int clean_wpa_receive(clean_wpa_t *s, const uint8_t *m, uint32_t n,
                       uint8_t response[512], uint32_t *length);
uint32_t clean_wpa_probe(clean_wpa_t *s, uint8_t frame[512]);
int clean_wpa_protected(clean_wpa_t *s, const uint8_t *m, uint32_t n, uint32_t now);
void clean_wpa_rx_expire(clean_wpa_t *s, uint32_t now);
/* Agreement/controller ownership remains with the caller. Ciphertext is
 * buffered first; per-TID MIC/PN processing runs only on ordered release. */
int clean_wpa_ba_receive(clean_wpa_t *s, clean_ba_rx_t *ba, const uint8_t *mpdu,
                         uint32_t length, uint32_t now);
int clean_wpa_ba_poll(clean_wpa_t *s, clean_ba_rx_t *ba, uint32_t now);
int clean_wpa_ba_bar(clean_wpa_t *s, clean_ba_rx_t *ba, const uint8_t *mpdu,
                     uint32_t length, uint32_t now);
int clean_wpa_ampdu_feed(clean_wpa_t *s, clean_ba_rx_t *ba, clean_ampdu_stream_t *stream,
                         const uint8_t *bytes, uint32_t length, uint32_t *used,
                         uint32_t now);

#endif

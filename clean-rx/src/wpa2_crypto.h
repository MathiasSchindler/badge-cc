#ifndef CLEAN_WPA2_CRYPTO_H
#define CLEAN_WPA2_CRYPTO_H

#include "../../src/shared/stdint.h"

typedef struct {
    uint32_t h[5], bytes;
    uint8_t block[64];
} wpa_sha1_t;

typedef struct {
    uint8_t round[176], box[256], inverse[256];
} wpa_aes_t;

void wpa_wipe(void *p, uint32_t n);
int wpa_equal(const uint8_t *a, const uint8_t *b, uint32_t n);
void wpa_sha1_init(wpa_sha1_t *s);
void wpa_sha1_update(wpa_sha1_t *s, const uint8_t *p, uint32_t n);
void wpa_sha1_final(wpa_sha1_t *s, uint8_t out[20]);
void wpa_sha1(const uint8_t *p, uint32_t n, uint8_t out[20]);
void wpa_secret_binding(const uint8_t secret[64], const uint8_t bssid[6],
                         uint8_t channel, uint8_t out[20]);
void wpa_hmac(const uint8_t *key, uint32_t kn, const uint8_t *p, uint32_t n, uint8_t out[20]);
int wpa_pmk(const uint8_t *password, uint32_t n, const uint8_t *ssid, uint32_t sn, uint8_t pmk[32]);
int wpa_psk(const uint8_t *credential, uint32_t n, const uint8_t *ssid, uint32_t sn, uint8_t pmk[32]);
void wpa_ptk(const uint8_t pmk[32], const uint8_t ap[6], const uint8_t sta[6],
             const uint8_t anonce[32], const uint8_t snonce[32], uint8_t ptk[48]);
void wpa_aes_init(wpa_aes_t *a, const uint8_t key[16]);
void wpa_aes_encrypt(const wpa_aes_t *a, const uint8_t in[16], uint8_t out[16]);
int wpa_unwrap(const uint8_t key[16], const uint8_t *in, uint32_t n, uint8_t *out);
/* Ordinary payloads are bounded to 2304 bytes; unfragmented QoS A-MSDUs to
 * 3839. MIC verification does not authenticate the A-MSDU header bit: callers
 * must validate the entire plaintext aggregate before exposing subframes. */
int wpa_ccmp(const uint8_t key[16], const uint8_t *header, uint32_t hn,
              const uint8_t pn[6], const uint8_t *in, uint32_t n,
              uint8_t *out, uint8_t tag[8], int decrypt);

#endif

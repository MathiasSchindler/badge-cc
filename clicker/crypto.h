#ifndef CLICKER_CRYPTO_H
#define CLICKER_CRYPTO_H
#include <stdint.h>
void clicker_aes128(const uint8_t key[16], const uint8_t input[16], uint8_t output[16]);
void clicker_c1(const uint8_t key[16], const uint8_t random[16],
                const uint8_t request[7], const uint8_t response[7],
                uint8_t initiator_type, const uint8_t initiator[6],
                uint8_t responder_type, const uint8_t responder[6], uint8_t output[16]);
void clicker_s1(const uint8_t key[16], const uint8_t responder[16],
                const uint8_t initiator[16], uint8_t output[16]);
int clicker_resolves(const uint8_t irk[16], const uint8_t address[6]);
int clicker_secret_equal(const uint8_t *a, const uint8_t *b, uint32_t length);
void clicker_secret_clear(void *data, uint32_t length);
#endif

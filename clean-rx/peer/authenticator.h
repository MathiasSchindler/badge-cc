#ifndef CLEAN_PEER_AUTHENTICATOR_H
#define CLEAN_PEER_AUTHENTICATOR_H
#include "../host/wpa2_crypto.h"
#include "../../src/shared/bytes.h"

#define CLEAN_AUTH_RETRY_US 500000u
#define CLEAN_AUTH_TIMEOUT_US 8000000u
enum { CLEAN_AUTH_IDLE, CLEAN_AUTH_M2, CLEAN_AUTH_M4, CLEAN_AUTH_READY, CLEAN_AUTH_G2 };
enum { CLEAN_AUTH_OK, CLEAN_AUTH_FORMAT, CLEAN_AUTH_MIC, CLEAN_AUTH_REPLAY,
       CLEAN_AUTH_NONCE, CLEAN_AUTH_TIMEOUT, CLEAN_AUTH_BACKEND };
typedef struct {
    void *context;
    int (*send)(void *, const uint8_t *, uint32_t);
    int (*install)(void *, const uint8_t *, uint8_t, const uint8_t *);
    int (*authorize)(void *, const uint8_t *);
} clean_auth_ops_t;
typedef struct {
    clean_auth_ops_t ops;
    uint8_t pmk[32], seed[32], mac[6], peer[6], anonce[32], snonce[32];
    uint8_t ptk[48], next_ptk[48], gtk[16], next_gtk[16], gtk_id, next_id;
    uint8_t replay[8], rsn[128], rsn_length, state, error, retries, installed;
    uint8_t message[512];
    uint32_t message_length, started, retry_at, sequence;
    uint32_t group_rekeys, pairwise_rekeys, m2, m4, g2;
} clean_auth_t;
void clean_auth_init(clean_auth_t *, const clean_auth_ops_t *, const uint8_t[32],
    const uint8_t[32], const uint8_t[6], const uint8_t *, uint8_t);
int clean_auth_start(clean_auth_t *, const uint8_t[6], int, uint32_t);
int clean_auth_receive(clean_auth_t *, const uint8_t *, uint32_t, uint32_t);
int clean_auth_poll(clean_auth_t *, uint32_t);
void clean_auth_clear(clean_auth_t *);
#endif

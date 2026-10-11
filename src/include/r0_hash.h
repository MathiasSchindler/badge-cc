#ifndef CYW43_R0_HASH_H
#define CYW43_R0_HASH_H
#include <stdint.h>

typedef struct {
    uint32_t words[8];
    uint32_t bytes;
    uint32_t used;
    uint8_t block[64];
} r0_sha256_t;

void r0_sha256_init(r0_sha256_t *hash);
void r0_sha256_update(r0_sha256_t *hash, const uint8_t *bytes, uint32_t length);
void r0_sha256_finish(r0_sha256_t *hash, uint8_t digest[32]);
uint32_t r0_crc32_update(uint32_t crc, const uint8_t *bytes, uint32_t length);
#endif

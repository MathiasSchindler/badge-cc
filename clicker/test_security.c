#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "crypto.h"
#include "bond.h"

static uint8_t flash[8192];
static uint32_t operations, fail_at;
static uint32_t torn_remaining;
static uint8_t torn;
static int read_flash(void *context, uint32_t offset, uint8_t *data, uint32_t length) {
    (void)context;
    assert(offset >= CLICKER_BOND_OFFSET && offset + length <= CLICKER_BOND_OFFSET + sizeof(flash));
    if (++operations == fail_at) return 0;
    memcpy(data, flash + offset - CLICKER_BOND_OFFSET, length);
    return 1;
}
static int erase_flash(void *context, uint32_t offset) {
    (void)context;
    assert(offset == CLICKER_BOND_OFFSET || offset == CLICKER_BOND_OFFSET + 4096u);
    if (++operations == fail_at) return 0;
    memset(flash + offset - CLICKER_BOND_OFFSET, 255, 4096u);
    return 1;
}
static int program_flash(void *context, uint32_t offset, const uint8_t *data, uint32_t length) {
    (void)context;
    assert(offset >= CLICKER_BOND_OFFSET && offset + length <= CLICKER_BOND_OFFSET + sizeof(flash));
    if (++operations == fail_at) return 0;
    uint32_t written=torn && torn_remaining<length?torn_remaining:length;
    for (uint32_t i = 0u; i < written; ++i) {
        uint8_t *byte = &flash[offset - CLICKER_BOND_OFFSET + i];
        assert((*byte & data[i]) == data[i]);
        *byte &= data[i];
    }
    if (torn) torn_remaining-=written;
    return written==length;
}
static const clicker_bond_io_t io = {0, erase_flash, program_flash, read_flash};

static uint8_t nibble(char c) { return (uint8_t)(c >= 'a' ? c - 'a' + 10 : c - '0'); }
static void hex(uint8_t *out, const char *text, uint32_t n, uint8_t reverse) {
    for (uint32_t i = 0u; i < n; ++i) {
        out[reverse ? n - 1u - i : i] = (uint8_t)((nibble(text[i * 2u]) << 4u) | nibble(text[i * 2u + 1u]));
    }
}
int main(void) {
    uint8_t key[16], input[16], output[16], expected[16];
    hex(key, "000102030405060708090a0b0c0d0e0f", 16u, 0u);
    hex(input, "00112233445566778899aabbccddeeff", 16u, 0u);
    hex(expected, "69c4e0d86a7b0430d8cdb78070b4c55a", 16u, 0u);
    clicker_aes128(key, input, output);
    assert(memcmp(output, expected, 16u) == 0);
    memset(key, 0, 16u);
    uint8_t request[7], response[7], ia[6], ra[6];
    hex(input, "5783d52156ad6f0e6388274ec6702ee0", 16u, 1u);
    hex(request, "07071000000101", 7u, 1u);
    hex(response, "05000800000302", 7u, 1u);
    hex(ia, "a1a2a3a4a5a6", 6u, 1u); hex(ra, "b1b2b3b4b5b6", 6u, 1u);
    hex(expected, "1e1e3fef878988ead2a74dc5bef13b86", 16u, 1u);
    clicker_c1(key, input, request, response, 1u, ia, 0u, ra, output);
    assert(memcmp(output, expected, 16u) == 0);
    uint8_t r1[16], r2[16];
    hex(r1, "00000000000000001122334455667788", 16u, 1u);
    hex(r2, "010203040506070899aabbccddeeff00", 16u, 1u);
    hex(expected, "9a1fe1f0e8b0f49b5b4216ae796da062", 16u, 1u);
    clicker_s1(key, r1, r2, output);
    assert(memcmp(output, expected, 16u) == 0);
    hex(key, "ec0234a357c8ad05341010a60a397d9b", 16u, 1u);
    hex(ia, "7081940dfbaa", 6u, 1u);
    assert(clicker_resolves(key, ia));
    ia[0] ^= 1u; assert(!clicker_resolves(key, ia));

    clicker_bonds_t bonds, recovered;
    memset(flash, 255, sizeof(flash)); fail_at = 0u; operations = 0u;
    assert(clicker_bonds_load(&bonds, &io) && !bonds.initialized);
    assert(clicker_bonds_save(&bonds, &io));
    uint8_t original[8192];
    memcpy(original, flash, sizeof(flash));
    for (uint32_t fault = 1u; fault <= 5u; ++fault) {
        memcpy(flash, original, sizeof(flash)); operations = 0u; fail_at = 0u;
        assert(clicker_bonds_load(&bonds, &io));
        bonds.peers[0].valid = 1u; bonds.peers[0].ltk[0] = 42u;
        operations = 0u; fail_at = fault;
        assert(!clicker_bonds_save(&bonds, &io));
        fail_at = 0u;
        assert(clicker_bonds_load(&recovered, &io));
        assert(recovered.generation == 1u || recovered.generation == 2u);
        if (recovered.generation == 1u) assert(!recovered.peers[0].valid);
        else assert(recovered.peers[0].valid && recovered.peers[0].ltk[0] == 42u);
    }
    for (uint32_t cut=0u;cut<=768u;++cut) {
        memcpy(flash,original,sizeof(flash));operations=0u;fail_at=0u;torn=0u;
        assert(clicker_bonds_load(&bonds,&io));
        bonds.peers[0].valid=1u;bonds.peers[0].ltk[0]=42u;
        torn=1u;torn_remaining=cut;
        int saved=clicker_bonds_save(&bonds,&io);
        assert(saved==(cut==768u));
        torn=0u;
        assert(clicker_bonds_load(&recovered,&io));
        assert(recovered.generation==1u || recovered.generation==2u);
        if (recovered.generation==1u) assert(!recovered.peers[0].valid);
        else assert(recovered.peers[0].valid && recovered.peers[0].ltk[0]==42u);
    }
    operations = 0u; fail_at = 0u;
    assert(clicker_bonds_save(&recovered, &io));
    assert(clicker_bonds_load(&bonds, &io));
    memset(flash, 0, sizeof(flash));
    assert(!clicker_bonds_load(&bonds, &io));
    puts("PASS: AES/NIST, BLE c1/s1/ah vectors, bond journal I/O failures and all 769 torn-write boundaries");
    return 0;
}

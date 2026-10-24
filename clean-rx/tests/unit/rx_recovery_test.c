#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rx_contract.h"
#include "psm_ack.h"

extern const uint8_t psm_ucode_bytes[];

#if CLEAN_RX_HT_ORDERED_FLUSH
static uint64_t instruction(uint32_t pc) {
    uint64_t w = 0u;
    for (uint32_t i = 0u; i < 8u; ++i)
        w |= (uint64_t)psm_ucode_bytes[8u*pc+i] << (8u*i);
    return w;
}

static uint16_t value(const uint16_t *r, uint32_t a) {
    if (a < 0x1800u) return r[a];
    uint16_t v = (uint16_t)(a & 0x7ffu);
    return (uint16_t)((v & 0x400u) ? v | 0xf800u : v);
}

static uint32_t execute(uint16_t *r, uint32_t pc, uint32_t busy_polls,
                        uint32_t *writes, uint32_t *write_count) {
    uint32_t polls = 0u;
    for (uint32_t steps = 0u; steps < 400000u; ++steps) {
        if (pc == 275u || pc == 290u || pc == 137u || pc == 145u || pc == 266u)
            return pc;
        uint64_t w = 0u;
        for (uint32_t i = 0u; i < 8u; ++i)
            w |= (uint64_t)psm_ucode_bytes[8u*pc+i] << (8u*i);
        uint32_t op = (uint32_t)(w>>39u), a = (uint32_t)(w>>26u)&0x1fffu;
        uint32_t b = (uint32_t)(w>>13u)&0x1fffu, d = (uint32_t)w&0x1fffu;
        uint16_t av = value(r,a), bv = value(r,b);
        ++pc;
        int store = 1;
        uint16_t result = 0u;
        if (op == 0x3f0u) result = av;
        else if (op == 0x160u) result = (uint16_t)(av|bv);
        else if (op == 0x370u) result = (uint16_t)((bv&0xff00u)|(av&255u));
        else if (op == 0x306u) result = (uint16_t)((bv&~0x40u)|((av&1u)<<6u));
        else if (op == 0x378u) result = (uint16_t)((bv&255u)|(av<<8u));
        else if (op == 0x1c0u) result = (uint16_t)(av+bv);
        else if (op == 0x1d0u) result = (uint16_t)(av-bv);
        else {
            store = 0;
            if (op == 0x77fu) pc = d;
            else if (op == 0xd1u) { if (av != bv) pc = d; }
            else if (op == 0xdau) { if (av < bv) pc = d; }
            else if (op == 0x40cu) { if (!(av&0x1000u)) pc = d; }
            else { assert(op == 0x60au); if (polls++ >= busy_polls) pc = d; }
        }
        if (store && d != 0x1800u) {
            r[d] = result;
            if (d == 0x1004u || d == 0x11e0u || d == 0x11e8u ||
                d == 0x11ecu || d == 0x1049u) {
                assert(*write_count < 16u);
                writes[(*write_count)++] = (d<<16u)|result;
            }
        }
    }
    assert(!"Recovery diagnostic exceeded instruction bound");
    return 0u;
}

static void ordered(void) {
    assert(((instruction(RX_ORDERED_DELAY)>>26u)&0x1fffu) == 0x180au);
    assert((instruction(RX_ORDERED_BARRIER1)&0x1fffu) == 0x1800u);
    assert((instruction(RX_ORDERED_BARRIER2)&0x1fffu) == 0x1800u);
    assert((instruction(RX_ORDERED_CRYPTO_BARRIER)&0x1fffu) == 0x1800u);
    assert((instruction(RX_ORDERED_RESET_BARRIER)&0x1fffu) == 0x1800u);
    for (uint32_t fault = 0u; fault < 3u; ++fault) {
        uint16_t r[0x1800] = {0};
        uint32_t writes[16], count = 0u;
        r[0x1004u] = 0x804u; r[0x100du] = 0x4000u; r[0x100eu] = 0x4140u;
        r[0x1024u] = 0x0c01u;
        r[0x1048u] = 0xa5ffu;
        r[0x1782u] = 0x1234u; r[0x1783u] = 0x5678u;
        uint32_t busy = fault == 2u ? 65536u : fault == 1u ? 7u : 0u;
        uint32_t next = execute(r,RX_ORDERED_START,busy,writes,&count);
        assert(r[CLEAN_RX_RECOVERY_SHM] == 1u);
        assert(r[CLEAN_RX_RECOVERY_SHM+2u] == 0x804u);
        assert(r[CLEAN_RX_RECOVERY_SHM+5u] == 0x4000u);
        assert(r[CLEAN_RX_RECOVERY_SHM+6u] == 0x4140u);
        assert(r[CLEAN_RX_RECOVERY_SHM+3u] == (fault == 2u ? 65535u : busy));
        assert(r[0x1024u] == 0x0c02u && !r[0x102au]);
        assert(r[0x1048u] == 0xa5bfu);
        assert(r[0x1782u] == 0x1234u && r[0x1783u] == 0x5678u);
        if (fault == 2u) {
            assert(next == 290u && r[0x602u] == 9u && count == 1u);
            assert(r[CLEAN_RX_RECOVERY_SHM+1u] == 4u);
        } else {
            const uint32_t expected[] = {0x10040014u,0x11e08300u,0x11e80309u,
                0x11e80300u,0x10040014u,0x11ec0009u,0x11ec0000u};
            assert(next == 275u && !r[0x602u] && count == 7u);
            assert(!memcmp(writes,expected,sizeof(expected)));
            assert(r[CLEAN_RX_RECOVERY_SHM+1u] == 3u);
        }
    }
}
#if CLEAN_RX_HT_PREFIX_RECOVERY
static void prefix(void) {
    assert(RX_PREFIX_RESTORE == RX_PREFIX_NOP_0+10u);
    for (uint32_t pc = RX_PREFIX_NOP_0; pc < RX_PREFIX_RESTORE; ++pc)
        assert(instruction(pc) == (((uint64_t)0x3f0u<<39u) |
            ((uint64_t)0x1780u<<26u) | ((uint64_t)0x1800u<<13u) | 0x1780u));
    for (uint32_t scenario = 0u; scenario < 6u; ++scenario) {
        uint16_t r[0x1800] = {0};
        uint32_t writes[16], count = 0u;
        r[0x1119u] = scenario == 5u ? 0xff80u : 100u;
        r[0x1782u] = 0x1234u; r[0x1783u] = 0x5678u;
        assert(execute(r,RX_PREFIX_TIMER,0u,writes,&count) == 137u);
        uint16_t start = r[0x1119u];
        uint16_t elapsed = scenario == 0u ? 249u : scenario == 1u ? 250u : 251u;
        r[0x1119u] = (uint16_t)(start+elapsed);
        r[0x100cu] = scenario == 3u ? 3u : 2u;
        r[0x100du] = scenario == 4u ? 0u : 0x1000u;
        r[0x1049u] = 0x52a1u;
        uint32_t next = execute(r,RX_PREFIX_CHECK,0u,writes,&count);
        int trigger = scenario != 0u && scenario != 3u && scenario != 4u;
        assert(next == (trigger ? 266u : 145u));
        assert(r[CLEAN_RX_RECOVERY_SHM+8u] == 1u);
        assert(r[CLEAN_RX_RECOVERY_SHM+10u] == trigger);
        assert(r[0x1049u] == 0x52a1u);
        assert(r[0x1782u] == 0x1234u && r[0x1783u] == 0x5678u);
        assert(count == (trigger ? 2u : 0u));
        if (trigger) {
            assert(writes[0] == 0x104952afu && writes[1] == 0x104952a1u);
            assert(r[CLEAN_RX_RECOVERY_SHM+12u] == 2u);
            assert(r[CLEAN_RX_RECOVERY_SHM+13u] == 0x1000u);
            assert(r[CLEAN_RX_RECOVERY_SHM+15u] == elapsed);
        } else assert(r[0x1780u] == 65534u);
    }
}
#endif
#endif

int main(void) {
#if CLEAN_RX_HT_ORDERED_FLUSH
    ordered();
#if CLEAN_RX_HT_PREFIX_RECOVERY
    prefix();
#endif
    puts("RX recovery emitted-program bounds, write ordering, prefix deadline and TSF wrap pass; NOT RF");
#else
    puts("RX recovery diagnostic disabled; no recovery program selected");
#endif
    return 0;
}

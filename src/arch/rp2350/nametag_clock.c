#include "nametag_clock.h"

#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))

static int wait_mask(uintptr_t address, uint32_t mask, uint32_t value) {
    for (uint32_t i = 0u; i < 2000000u; ++i)
        if ((REG(address) & mask) == value) return 1;
    return 0;
}

void nametag_clock_snapshot(uint32_t words[12]) {
    static const uintptr_t addresses[12] = {
        0x4001003cu, 0x40010040u, 0x40010044u,
        0x40010030u, 0x40010034u, 0x40010038u,
        0x40108018u, 0x4010801cu, 0x40108030u, 0x40108034u,
        0x40048000u, 0x40048004u
    };
    for (uint32_t i = 0u; i < 12u; ++i) words[i] = REG(addresses[i]);
}

int32_t nametag_clock_reference(void) {
    if (!(REG(0x40048004u) & (1u << 12u))) {
        REG(0x4004800cu) = 196u;
        REG(0x40048000u) = 0x00fabaa0u;
    }
    if (!wait_mask(0x40048004u, 0x80000000u, 0x80000000u)) return -3001;
    uint32_t sys = REG(0x4001003cu);
    if ((sys & 1u) == 0u) {
        /* ROSC_PH and ROSC have the same frequency; preserve the SYS divider. */
        if ((REG(0x40010030u) & 3u) != 0u || REG(0x40010034u) != 0x10000u) return -3002;
        if (!wait_mask(0x40010044u, 3u, 1u)) return -3003;
        REG(0x4001003cu) = (sys & ~(7u << 5u)) | (2u << 5u);
        REG(0x4001003cu) |= 1u;
        if (!wait_mask(0x40010044u, 3u, 2u)) return -3004;
    }
    REG(0x40010034u) = 0x10000u;
    REG(0x40010030u) = (REG(0x40010030u) & ~3u) | 2u;
    if (!wait_mask(0x40010038u, 15u, 4u)) return -3005;
    REG(0x40108018u) = 0u;
    if (!wait_mask(0x40108018u, 2u, 0u)) return -3006;
    REG(0x4010801cu) = 12u;
    REG(0x40108018u) = 1u;
    if (!wait_mask(0x40108018u, 3u, 3u)) return -3007;
    __asm__ volatile ("dsb sy\nisb sy" ::: "memory");
    return 0;
}

int32_t nametag_clock_frequency(uint32_t source, uint32_t *khz_q5) {
    if (khz_q5 == 0 || (source != 8u && source != 9u)) return -3010;
    REG(0x400100a0u) = 0u;
    if (!wait_mask(0x400100a4u, 0x100u, 0u)) return -3011;
    REG(0x4001008cu) = 12000u;
    REG(0x40010090u) = 0u;
    REG(0x40010094u) = 0x1ffffffu;
    REG(0x40010098u) = 1u;
    REG(0x4001009cu) = 10u;
    REG(0x400100a0u) = source;
    if (!wait_mask(0x400100a4u, 0x10u, 0x10u)) return -3012;
    *khz_q5 = REG(0x400100a8u) & 0x3fffffffu;
    REG(0x400100a0u) = 0u;
    return *khz_q5 != 0u ? 0 : -3013;
}

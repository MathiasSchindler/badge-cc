#include "../../src/shared/stdint.h"

#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))

static int wait_mask(uintptr_t address, uint32_t mask, uint32_t expected) {
    for (uint32_t i = 0u; i < 2000000u; ++i)
        if ((REG(address) & mask) == expected) return 1;
    return 0;
}

int32_t pr_clock_sys(void) {
    /* Run from the qualified 12 MHz reference before resetting PLL_SYS. */
    REG(0x4001003cu) &= ~1u;
    if (!wait_mask(0x40010044u, 3u, 1u)) return -3101;
    REG(0x40022000u) = 1u << 14u;
    REG(0x40023000u) = 1u << 14u;
    if (!wait_mask(0x40020008u, 1u << 14u, 1u << 14u)) return -3102;
    REG(0x40050000u) = 1u;
    REG(0x40050008u) = 100u;
    REG(0x40050004u) = 0x0cu;
    if (!wait_mask(0x40050000u, 1u << 31u, 1u << 31u)) return -3103;
    REG(0x4005000cu) = (5u << 16u) | (2u << 12u);
    REG(0x40050004u) = 4u;
    REG(0x40010040u) = 0x10000u;
    REG(0x4001003cu) &= ~(7u << 5u);
    REG(0x4001003cu) |= 1u;
    if (!wait_mask(0x40010044u, 3u, 2u)) return -3104;
    __asm__ volatile ("dsb sy\nisb sy" ::: "memory");
    return 0;
}

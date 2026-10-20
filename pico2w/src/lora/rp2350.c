#include "hardware.h"
#include "sx1262.h"

#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define CLOCK(offset) REG(0x40010000u + (offset))
#define SPI(offset) REG(0x40088000u + (offset))
#define SIO(offset) REG(0xd0000000u + (offset))
#define CS (1u << 3u)
#define RESET (1u << 15u)
#define OUTPUTS (CS | RESET)
#define RESETS ((1u << 19u) | (1u << 24u))
#define GPIO_RESETS ((1u << 6u) | (1u << 9u))

static uint8_t pins_ready;

static int wait_mask(volatile uint32_t *reg, uint32_t mask, uint32_t wanted) {
    for (uint32_t loops = 0u; loops < 100000u; ++loops)
        if ((*reg & mask) == wanted) return 1;
    return 0;
}

static void pin(uint32_t gpio, uint32_t function, uint8_t output) {
    REG(0x40028004u + gpio * 8u) = function;
    /* Remove RP2350 pad isolation only after the mux and output latch are safe. */
    REG(0x40038004u + gpio * 4u) = output ? 0x52u : 0xd2u;
}

uint32_t sx1262_hw_time_us(void) {
    return REG(0x400b8028u);
}

int32_t sx1262_hw_init(void) {
    pins_ready = 0u;
    if ((REG(0x40048004u) & 0x80001000u) != 0x80001000u)
        return SX1262_INIT_ERROR;

    /* Probe-only clock setup; USB has its own PLL and clock slice. */
    CLOCK(0x30u) = (CLOCK(0x30u) & ~3u) | 2u;
    if (!wait_mask(&CLOCK(0x38u), 15u, 4u)) return SX1262_INIT_ERROR;
    CLOCK(0x34u) = 1u << 16u;
    CLOCK(0x48u) &= ~(1u << 11u);
    if (!wait_mask(&CLOCK(0x48u), 1u << 28u, 0u)) return SX1262_INIT_ERROR;
    CLOCK(0x4cu) = 1u << 16u;
    CLOCK(0x48u) = (4u << 5u) | (1u << 11u);
    if (!wait_mask(&CLOCK(0x48u), 1u << 28u, 1u << 28u))
        return SX1262_INIT_ERROR;

    REG(0x40022000u) = RESETS;
    REG(0x40023000u) = RESETS | GPIO_RESETS;
    if (!wait_mask(&REG(0x40020008u), RESETS | GPIO_RESETS, RESETS | GPIO_RESETS))
        return SX1262_INIT_ERROR;
    REG(0x40108024u) = 0u;
    REG(0x40108028u) = 12u;
    REG(0x40108024u) = 1u;
    REG(0x400b8038u) = 0u;
    REG(0x400b8030u) = 0u;
    REG(0x400b802cu) = 0u;
    uint32_t start = sx1262_hw_time_us();
    uint32_t loops;
    for (loops = 0u; loops < 100000u; ++loops)
        if (sx1262_hw_time_us() - start >= 10u) break;
    if (loops == 100000u) return SX1262_TIMER_ERROR;

    SPI(0x04u) = 0u;
    SPI(0x10u) = 12u;
    SPI(0x00u) = 7u;
    SPI(0x04u) = 2u;
    SIO(0x40u) = OUTPUTS | (1u << 2u) | (1u << 20u) |
                 (1u << 10u) | (1u << 11u) | (1u << 12u);
    SIO(0x20u) = RESET;
    SIO(0x18u) = CS;
    pin(3u, 5u, 1u);
    pin(15u, 5u, 1u);
    pin(2u, 5u, 0u);
    pin(20u, 5u, 0u);
    pin(10u, 1u, 1u);
    pin(11u, 1u, 1u);
    pin(12u, 1u, 0u);
    SIO(0x38u) = OUTPUTS;
    pins_ready = 1u;
    return 0;
}

uint8_t sx1262_hw_busy(void) {
    return (SIO(0x04u) & (1u << 2u)) != 0u;
}

void sx1262_hw_reset(uint8_t asserted) {
    /* Open-drain reset: never drive the module's reset line high. */
    SIO(asserted ? 0x38u : 0x40u) = RESET;
}

void sx1262_hw_select(uint8_t selected) {
    SIO(selected ? 0x20u : 0x18u) = CS;
}

int32_t sx1262_hw_byte(uint8_t out, uint8_t *in) {
    if (!wait_mask(&SPI(0x0cu), 2u, 2u)) return SX1262_SPI_ERROR;
    SPI(0x08u) = out;
    if (!wait_mask(&SPI(0x0cu), 4u, 4u)) return SX1262_SPI_ERROR;
    *in = (uint8_t)SPI(0x08u);
    if (!wait_mask(&SPI(0x0cu), 16u, 0u)) return SX1262_SPI_ERROR;
    return 0;
}

void sx1262_hw_release(void) {
    if (!pins_ready) return;
    sx1262_hw_select(0u);
    sx1262_hw_reset(0u);
    SPI(0x04u) = 0u;
    pins_ready = 0u;
}

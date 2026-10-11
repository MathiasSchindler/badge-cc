#include <stdint.h>

#ifdef RED_SQUARE_SHARED_DISPLAY
#include "display.h"

void bare_main(void) {
    badger_display_init();
    badger_display_begin();
    for (uint32_t count = 76800u; count != 0u; --count)
        badger_display_write(0u);
    badger_display_end_update();
    badger_display_begin_region(128u, 88u, 64u, 64u);
    for (uint32_t count = 4096u; count != 0u; --count)
        badger_display_write(0xf800u);
    badger_display_end();
    while (1) __asm__ volatile ("wfi");
}
#else

#ifdef RED_SQUARE_TEST
extern void red_square_register(uint32_t address, uint32_t value);
extern void red_square_delay(uint32_t cycles);
#define STORE(address, value) red_square_register(address, value)
#define delay_cycles red_square_delay
#else
#define STORE(address, value) (*(volatile uint32_t *)(address) = (value))
static void delay_cycles(uint32_t cycles) {
    /* At most 150 MHz: even the shorter four-million-iteration wait supplies
       24 million explicit cycles (160 ms), independent of loop optimization. */
    while (cycles-- != 0u) __asm__ volatile ("nop\nnop\nnop\nnop\nnop\nnop");
}
#endif

#define SIO 0xd0000000u
#define CS (1u << 27)
#define DC (1u << 28)
#define WR (1u << 30)

/* LLVM otherwise duplicates the MMIO sequence; keep its measured shared helper. */
#ifdef __clang__
static void write_byte(uint32_t value) __attribute__((noinline));
#endif
static void write_byte(uint32_t value) {
    STORE(SIO + 0x24u, 255u);
    STORE(SIO + 0x1cu, value);
    STORE(SIO + 0x20u, WR);
#ifndef RED_SQUARE_TEST
    __asm__ volatile ("nop\nnop\nnop");
#endif
    STORE(SIO + 0x18u, WR);
#ifndef RED_SQUARE_TEST
    __asm__ volatile ("nop\nnop");
#endif
}

static void command(uint32_t value) {
    STORE(SIO + 0x20u, DC);
    write_byte(value);
    STORE(SIO + 0x18u, DC);
}

/* Same analog, gamma, RGB565 and orientation settings as the shared driver.
   Records are command, payload length, then payload bytes. */
static const uint8_t setup[] = {
    0x3a, 1, 0x05,
    0xb2, 5, 0x0c, 0x0c, 0x00, 0x33, 0x33,
    0xc0, 1, 0x2c,
    0xc2, 1, 0x01,
    0xc3, 1, 0x0f,
    0xc4, 1, 0x20,
    0xd0, 2, 0xa4, 0xa1,
    0xc6, 1, 0x0f,
    0xb0, 2, 0x00, 0xc0,
    0xb7, 1, 0x35,
    0xbb, 1, 0x1b,
    0xe0, 14, 0xf0, 0x00, 0x06, 0x04, 0x05, 0x05, 0x31,
              0x44, 0x48, 0x36, 0x12, 0x12, 0x2b, 0x34,
    0xe1, 14, 0xf0, 0x0b, 0x0f, 0x0f, 0x0d, 0x26, 0x31,
              0x43, 0x47, 0x38, 0x14, 0x14, 0x2c, 0x32,
    0x21, 0,
    0x11, 0,
};
static const uint8_t full_window[] = {
    0x2a, 4, 0x00, 0x00, 0x00, 0xef,
    0x2b, 4, 0x00, 0x00, 0x01, 0x3f,
    0x36, 1, 0x90,
};
static const uint8_t square_window[] = {
    0x2a, 4, 0x00, 0x58, 0x00, 0x97,
    0x2b, 4, 0x00, 0x80, 0x00, 0xbf,
};

static void commands(const uint8_t *data, uint32_t length) {
    const uint8_t *end = data + length;
    while (data < end) {
        command(*data++);
        uint32_t count = *data++;
        while (count-- != 0u) write_byte(*data++);
    }
}

static void fill(uint32_t pixel, uint32_t count) {
    command(0x2cu);
    while (count-- != 0u) {
        write_byte(pixel >> 8);
        write_byte(pixel & 255u);
    }
}

void bare_main(void) {
    /* Configure only LCD pins and board power, with the backlight still off. */
    STORE(SIO + 0x20u, (1u << 26) | CS);
    STORE(SIO + 0x18u, DC | WR | (1u << 31));
    STORE(SIO + 0x24u, 255u);
    STORE(SIO + 0x1cu, 1u << 9);
    for (uint32_t pin = 26u; pin <= 41u; ++pin) {
        if (pin == 29u || pin == 40u) continue;
        STORE(0x40038004u + pin * 4u, 0x21u);
        STORE(0x40028004u + pin * 8u, 5u);
    }
    STORE(SIO + 0x38u, (1u << 26) | CS | DC | WR | (1u << 31));
    STORE(SIO + 0x3cu, 255u | (1u << 9));
    command(0x01u);
    delay_cycles(6000000u);
    commands(setup, sizeof(setup));
    delay_cycles(4000000u);
    commands(full_window, sizeof(full_window));
    fill(0u, 76800u);
    commands(square_window, sizeof(square_window));
    fill(0xf800u, 4096u);
    command(0x29u);
    STORE(SIO + 0x18u, CS | (1u << 26));
#ifndef RED_SQUARE_TEST
    while (1) __asm__ volatile ("wfi");
#endif
}
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "types.h"

static struct {
    uint32_t address, value;
} registers[2048];
static uint32_t register_count;
static int fail_selected, fail_pll, fail_timer;

static volatile uint32_t *storage(uint32_t address) {
    for (uint32_t i = 0u; i < register_count; ++i)
        if (registers[i].address == address) return &registers[i].value;
    assert(register_count < 2048u);
    registers[register_count].address = address;
    return &registers[register_count++].value;
}

static volatile uint32_t *fake_register(uint32_t address) {
    volatile uint32_t *value = storage(address);
    if (address == 0x400b0028u && !fail_timer &&
        (*storage(0x40108018u) & 1u) && *storage(0x4010801cu) == 12u)
        *value += 1000u;
    if (address == 0x40108018u && (*value & 1u) && !fail_timer) *value |= 2u;
    if (address == 0x40010038u) *value = (*storage(0x40010030u) & 3u) == 2u ? 4u : 1u;
    if (address == 0x40048004u) *value = 0x80001000u;
    if (address == 0x40020008u) *value = 0xffffffffu;
    if (address == 0x40010044u)
        *value = fail_selected ? 0u : ((*storage(0x4001003cu) & 1u) ? 2u : 1u);
    if ((address == 0x40050000u || address == 0x40058000u) && !fail_pll)
        *value |= 0x80000000u;
    return value;
}

#define REG(address) (*fake_register(address))
#include "../src/usb/hardware.c"

int main(void) {
    /* TIMER0 and tick generator start stopped, as on power-on reset. */
    assert(timer_ticks() == 0u);
    *storage(0x4001003cu) = 0x41u;
    assert(usb_hw_init() == 0);
    assert((*storage(0x4001003cu) & 0xe1u) == 1u);
    assert(*storage(0x40010040u) == 0x10000u);
    assert(*storage(0x40050008u) == 120u);
    assert(*storage(0x4005000cu) == ((6u << 16u) | (2u << 12u)));
    assert(*storage(0x40050004u) == 4u);
    assert(*storage(0x40058008u) == 100u);
    assert(*storage(0x4005800cu) == ((5u << 16u) | (5u << 12u)));
    assert(*storage(0x40010064u) == 0x10000u);
    assert(*storage(0x5011004cu) == ((1u << 29u) | (1u << 16u)));
    assert(*storage(0x4010801cu) == 12u && *storage(0x40108018u) == 3u);
    assert(*storage(0x40010034u) == 0x10000u && (*storage(0x40010030u) & 3u) == 2u);
    assert(*storage(0x400b0030u) == 0u && *storage(0x400b0038u) == 0u);
    assert(*storage(0x40108024u) == 0u && *storage(0x40108028u) == 0u);
    const uint32_t sys_hz = 12000000u * 120u / 6u / 2u;
    const uint32_t usb_hz = 12000000u * 100u / 5u / 5u;
    assert(sys_hz * 10u >= usb_hz * 11u);
    usb_control_t control;
    const uint32_t chip[2] = {1u, 2u};
    usb_control_init(&control, 3u, chip);
    control.action = USB_ACTION_IN;
    control.length = 0u;
    control.in_pid = 0u;
    memset(control.data, 0x55, sizeof(control.data));
    apply_action(&control);
    assert(*storage(0x50100080u) == (FULL | AVAILABLE));
    control.action = USB_ACTION_IN;
    control.in_pid = 1u;
    apply_action(&control);
    assert(*storage(0x50100080u) == (FULL | AVAILABLE | DATA1));
    register_count = 0u;
    memset(registers, 0, sizeof(registers));
    fail_selected = 1;
    assert(usb_hw_init() == -2001);
    assert(*storage(0x5011004cu) == 0u);
    register_count = 0u;
    memset(registers, 0, sizeof(registers));
    fail_selected = 0;
    fail_pll = 1;
    assert(usb_hw_init() == -2001);
    assert(*storage(0x5011004cu) == 0u);
    register_count = 0u;
    memset(registers, 0, sizeof(registers));
    fail_pll = 0;
    fail_timer = 1;
    assert(usb_hw_init() == -2007);
    assert(*storage(0x5011004cu) == 0u);
    puts("PASS: USB cold-start TIMER0/tick init, E12 clock rates, bounded timer/PLL failures and control DATA PID");
    return 0;
}

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t addresses[32], values[32], count;
static uint32_t waits, events, barriers, enables;

static uint32_t *reg(uint32_t address) {
    for (uint32_t i = 0u; i < count; ++i)
        if (addresses[i] == address) return &values[i];
    assert(count < 32u);
    addresses[count] = address;
    return &values[count++];
}

#define BADGER_HOME_REG(address) (*reg(address))
#define BADGER_HOME_WAIT_EVENT() (++waits)
#define BADGER_HOME_SEND_EVENT() (++events)
#define BADGER_HOME_BARRIER() (++barriers)
#define BADGER_HOME_ENABLE_CPU_IRQS() (++enables)
#include "../../platform/badger/home.c"

int main(void) {
    badger_home_init();
    assert(*reg(0xd0000040u) == (1u << 22u));
    assert(*reg(0x4003805cu) == 0xcau);
    assert(*reg(0x400280b4u) == 5u);
    *reg(0x400280b0u) = 1u << 17u;
    assert(!badger_home_pressed());
    *reg(0x400280b0u) = 0u;
    assert(badger_home_pressed());
    *reg(0x40028250u) = 0x100u;
    *reg(0xe000ed10u) = 0x14u;
    badger_home_wake_enable();
    assert(*reg(0x40028238u) == 0x0c000000u);
    assert(*reg(0x40028250u) == 0x0c000100u);
    assert(*reg(0xe000e100u) == (1u << 21u));
    assert(*reg(0xe000e280u) == (1u << 21u));
    assert(*reg(0xe000ed10u) == 0x10u);
    assert(enables == 1u && barriers == 1u);
    *reg(0x40028280u) = 0x04000100u;
    bare_gpio_wake_irq();
    assert(*reg(0x40028238u) == 0x04000000u && events == 1u);
    *reg(0x40028280u) = 0x08000000u;
    bare_gpio_wake_irq();
    assert(*reg(0x40028238u) == 0x08000000u && events == 2u);
    badger_home_wait_event();
    assert(waits == 1u && barriers == 2u);
    badger_home_wake_disable();
    assert(*reg(0x40028250u) == 0x100u);
    assert(*reg(0xe000e180u) == (1u << 21u));
    assert(*reg(0xe000e280u) == (1u << 21u));
    assert(*reg(0x40028238u) == 0x0c000000u && barriers == 3u);
    puts("HOME: input-only GPIO22, active-low level, both-edge IRQ21, event wake and cleanup PASS");
}

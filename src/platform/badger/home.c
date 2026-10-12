#include "home.h"

#ifndef BADGER_HOME_REG
#define BADGER_HOME_REG(address) (*(volatile uint32_t *)(uintptr_t)(address))
#endif
#ifndef BADGER_HOME_WAIT_EVENT
#define BADGER_HOME_WAIT_EVENT() __asm__ volatile ("wfe" ::: "memory")
#endif
#ifndef BADGER_HOME_SEND_EVENT
#define BADGER_HOME_SEND_EVENT() __asm__ volatile ("sev" ::: "memory")
#endif
#ifndef BADGER_HOME_BARRIER
#define BADGER_HOME_BARRIER() __asm__ volatile ("dsb sy\nisb sy" ::: "memory")
#endif
#ifndef BADGER_HOME_ENABLE_CPU_IRQS
#define BADGER_HOME_ENABLE_CPU_IRQS() __asm__ volatile ("cpsie i" ::: "memory")
#endif

#define HOME_EDGES (0xcu << 24u)
#define HOME_IRQ (1u << 21u)

void badger_home_init(void) {
    BADGER_HOME_REG(0xd0000040u) = 1u << 22u;
    BADGER_HOME_REG(0x4003805cu) = 0xcau;
    BADGER_HOME_REG(0x400280b4u) = 5u;
}

int badger_home_pressed(void) {
    return (BADGER_HOME_REG(0x400280b0u) & (1u << 17u)) == 0u;
}

void badger_home_wake_enable(void) {
    BADGER_HOME_REG(0x40028238u) = HOME_EDGES;
    BADGER_HOME_REG(0xe000e280u) = HOME_IRQ;
    BADGER_HOME_REG(0xe000ed10u) &= ~4u;
    BADGER_HOME_REG(0x40028250u) |= HOME_EDGES;
    BADGER_HOME_REG(0xe000e100u) = HOME_IRQ;
    BADGER_HOME_BARRIER();
    BADGER_HOME_ENABLE_CPU_IRQS();
}

void badger_home_wake_disable(void) {
    BADGER_HOME_REG(0x40028250u) &= ~HOME_EDGES;
    BADGER_HOME_REG(0xe000e180u) = HOME_IRQ;
    BADGER_HOME_REG(0x40028238u) = HOME_EDGES;
    BADGER_HOME_REG(0xe000e280u) = HOME_IRQ;
    BADGER_HOME_BARRIER();
}

void badger_home_wait_event(void) {
    BADGER_HOME_BARRIER();
    BADGER_HOME_WAIT_EVENT();
}

void bare_gpio_wake_irq(void) {
    uint32_t edges = BADGER_HOME_REG(0x40028280u) & HOME_EDGES;
    BADGER_HOME_REG(0x40028238u) = edges;
    /* Preserve an event if the edge arrives between the level check and WFE. */
    BADGER_HOME_SEND_EVENT();
}

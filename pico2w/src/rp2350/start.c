#include "types.h"

extern uint32_t __data_source;
extern uint32_t __data_start;
extern uint32_t __data_end;
extern uint32_t __bss_start;
extern uint32_t __bss_end;
extern uint32_t __StackTop;

void bare_main(void);
void bare_reset(void);

static void bare_halt(void) __attribute__((noreturn));
static void bare_halt(void) {
    while (1) __asm__ volatile ("wfi");
}

__attribute__((section(".vectors"), used))
void (* const bare_vectors[])(void) = {
    (void (*)(void))&__StackTop,
    bare_reset,
    bare_halt,
    bare_halt,
    bare_halt,
    bare_halt,
    bare_halt,
    bare_halt,
    0,
    0,
    0,
    bare_halt,
    bare_halt,
    0,
    bare_halt,
    bare_halt
};

void bare_reset(void) {
    uint32_t *source = &__data_source;
    uint32_t *destination = &__data_start;
    *(volatile uint32_t *)0xe000ed08u = (uint32_t)(uintptr_t)bare_vectors;
    __asm__ volatile ("dsb sy\nisb sy" ::: "memory");

    while (destination < &__data_end) *destination++ = *source++;
    destination = &__bss_start;
    while (destination < &__bss_end) *destination++ = 0u;

    bare_main();
    bare_halt();
}

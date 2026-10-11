#include <stdint.h>

__attribute__((section(".noinit")))
volatile uint32_t universe26_boot_marker;

void bare_main(void) {
    universe26_boot_marker = 0x23502601u;

    while (1) {
        __asm__ volatile ("wfi");
    }
}

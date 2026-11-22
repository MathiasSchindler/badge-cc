#include "application.h"
#include "display.h"

static uint32_t expected, writes, digest;
static int failed;

void badger_display_begin_region(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (expected || !width || !height || x + width > 320u || y + height > 240u) failed = 1;
    expected = width * height;
}
void badger_display_write(uint16_t pixel) {
    if (!expected) failed = 1;
    else expected--;
    writes++;
    digest = (digest ^ pixel) * 16777619u;
}
void badger_display_end_update(void) {
    if (expected) failed = 1;
}
static void print(uint32_t value) {
    const char hex[] = "0123456789abcdef";
    char output[10];
    for (unsigned i = 0; i < 8; i++) output[i] = hex[(value >> (28u - i * 4u)) & 15u];
    output[8] = '\n';
    output[9] = 0;
    register unsigned op __asm__("r0") = 4;
    register const char *arg __asm__("r1") = output;
    __asm__ volatile("bkpt #0xab" : "+r"(op) : "r"(arg) : "memory");
}
void bare_main(void) {
    for (uint32_t module = 0u; module < 6u; module++) {
        compiler_result_t result;
        writes = 0;
        digest = 2166136261u;
        compiler_application_prepare(module, 123456789u);
        compiler_application_run(module);
        if (!compiler_application_result(module, &result)) failed = 1;
        else {
            if (module >= 4u && (writes != result.cases || digest != result.values[0])) failed = 1;
            if (module < 4u && writes != 0u) failed = 1;
            print(result.values[0]);
        }
    }
    const uint32_t result[2] = {0x20026, (uint32_t)failed};
    register uint32_t op __asm__("r0") = 32;
    register const uint32_t *arg __asm__("r1") = result;
    __asm__ volatile("bkpt #0xab" : "+r"(op) : "r"(arg) : "memory");
    for (;;) {}
}

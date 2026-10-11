#include "../../../src/arch/rp2350/bootrom_reboot.h"

extern uint32_t __StackTop;
extern uint32_t __data_source, __data_start, __data_end;
extern uint32_t ba03_probe(void);
extern void ba03_svc(void);
extern int32_t ba03_flash_commit(const uintptr_t functions[5], const uint32_t page[64]);
volatile uint32_t ba03_exception;
extern volatile uint32_t ba03_progress;
void ba03_reset(void);
static void ba03_fault(void);

#define RESPONSE ((volatile uint32_t *)0x2007f100u)
#define REQUEST ((const volatile uint32_t *)0x2007f000u)

static uintptr_t flash_functions[5];

#ifdef BA05_SYNCHRONIZATION
extern void ba05_stop_core1(void);
extern void ba05_watchdog_wait(void) __attribute__((noreturn));
#define REPORT_MAGIC 0x35304142u
#define REPORT_VERSION 6u
#define REPORT_WORDS 64u
static uint32_t saved_actlr;
static uint32_t actlr_saved;

static uint32_t restore_actlr(void) {
    if (!actlr_saved) return 0;
    *(volatile uint32_t *)0xe000e008u = saved_actlr;
    __asm__ volatile ("dsb sy\nisb sy" ::: "memory");
    uint32_t current = *(const volatile uint32_t *)0xe000e008u;
#if defined(BA05_STAGE) && BA05_STAGE != 0
    RESPONSE[25] = current;
#endif
    return current != saved_actlr;
}
#else
#define REPORT_MAGIC 0x33304142u
#define REPORT_VERSION 3u
#define REPORT_WORDS 21u
#endif

static int32_t prepare_flash(void) {
    typedef uintptr_t (*lookup_t)(uint32_t, uint32_t);
    uintptr_t address = *(const volatile uint16_t *)0x16u;
    if (!(address & 1u) || address < 0x20u || address >= 0x10000u) return -1001;
    lookup_t lookup = (lookup_t)address;
    const uint32_t codes[5] = {0x4649u, 0x5845u, 0x4f46u, 0x4346u, 0x5843u};
    for (uint32_t i = 0; i < 5; ++i) {
        address = lookup(codes[i], 4u);
        if (!(address & 1u) || address < 0x20u || address >= 0x10000u) return -1002;
        flash_functions[i] = address;
    }
    return 0;
}

static void publish_flash(void) {
    uint32_t page[64];
    for (uint32_t i = 0; i < 64; ++i) page[i] = i < REPORT_WORDS ? RESPONSE[i] : 0;
    int32_t error = ba03_flash_commit(flash_functions, page);
    if (error != 0) {
        RESPONSE[6] = 0xfffd0000u | ((uint32_t)error & 0xffffu);
        (void)badger_reboot_to_bootsel();
        while (1) __asm__ volatile ("wfi");
    }
}

#ifdef BA05_SYNCHRONIZATION
void ba05_checkpoint(uint32_t phase) {
    ba03_progress = 0x200u | phase;
    publish_flash();
}
#endif

__attribute__((section(".vectors"), aligned(256), used))
void (*const ba03_vectors[])(void) = {
    (void (*)(void))&__StackTop, ba03_reset, ba03_fault, ba03_fault,
    ba03_fault, ba03_fault, ba03_fault, ba03_fault,
    0, 0, 0, ba03_svc, ba03_fault, 0, ba03_fault, ba03_fault
};

static void state(uint32_t values[4]) {
    __asm__ volatile (
        "mrs %0, primask\nmrs %1, basepri\nmrs %2, faultmask\nmrs %3, control"
        : "=r"(values[0]), "=r"(values[1]), "=r"(values[2]), "=r"(values[3]));
}

static void ba03_fault(void) {
#ifdef BA05_SYNCHRONIZATION
    ba05_stop_core1();
    uint32_t restore_error = restore_actlr();
#endif
    uint32_t exception;
    __asm__ volatile ("mrs %0, ipsr" : "=r"(exception));
    RESPONSE[6] = 0xfffe0000u | exception;
#ifdef BA05_SYNCHRONIZATION
    if (restore_error) RESPONSE[6] = 0xffff0503u;
#endif
    RESPONSE[7] = exception;
    RESPONSE[17] = *(const volatile uint32_t *)0xe000ed28u;
    RESPONSE[18] = *(const volatile uint32_t *)0xe000ed2cu;
    RESPONSE[19] = *(const volatile uint32_t *)0xe000ede4u;
    __asm__ volatile ("dsb sy" ::: "memory");
    RESPONSE[0] = REPORT_MAGIC;
    __asm__ volatile ("cpsie if\ndsb sy" ::: "memory");
    publish_flash();
    (void)badger_reboot_to_bootsel();
    while (1) __asm__ volatile ("wfi");
}

void ba03_reset(void) {
    volatile uint32_t *response = RESPONSE;
    const volatile uint32_t *request = REQUEST;
    uint32_t before[4], after[4];
    response[0] = 0;
    response[1] = REPORT_VERSION;
    response[6] = 0xffff0002u;
    response[7] = 0;
    ba03_progress = 0x100u;
    for (uint32_t i = 0; i < 4; ++i) response[2 + i] = request[i];
    *(volatile uint32_t *)0xe000ed08u = (uint32_t)(uintptr_t)ba03_vectors;
    __asm__ volatile ("dsb sy\nisb sy" ::: "memory");
    uint32_t *source = &__data_source;
    for (uint32_t *destination = &__data_start; destination < &__data_end; ++destination) {
        *destination = *source++;
    }
    ba03_exception = 0;
#ifdef BA05_SYNCHRONIZATION
    actlr_saved = 0;
#endif
    for (uint32_t i = 0; i < 4; ++i) {
        response[8 + i] = 0;
        response[12 + i] = 0;
        response[17 + i] = 0;
    }
    for (uint32_t i = 21; i < REPORT_WORDS; ++i) response[i] = 0;
    __asm__ volatile ("dsb sy" ::: "memory");
    response[0] = REPORT_MAGIC;
    state(before);
    for (uint32_t i = 0; i < 4; ++i) response[8 + i] = before[i];
    ba03_progress = 0x101u;
    int32_t recovery = badger_schedule_bootsel(8000u);
    response[20] = (uint32_t)recovery;
    ba03_progress = 0x102u;
    int32_t flash = prepare_flash();
    if (flash != 0) {
        response[6] = 0xfffc0000u | ((uint32_t)flash & 0xffffu);
        (void)badger_reboot_to_bootsel();
        while (1) __asm__ volatile ("wfi");
    }
    publish_flash();
#ifdef BA05_SYNCHRONIZATION
    saved_actlr = *(const volatile uint32_t *)0xe000e008u;
    actlr_saved = 1;
#endif
#ifdef BA03_BOOT_SMOKE
    uint32_t result = recovery == 0 ? 0u : 0xffff0001u;
#else
    uint32_t result = recovery == 0 ? ba03_probe() : 0xffff0001u;
#endif
#ifdef BA05_SYNCHRONIZATION
    if (restore_actlr()) result = 0xffff0503u;
#endif
    ba03_progress = 0x103u;
    state(after);
    for (uint32_t i = 0; i < 4; ++i) {
        response[12 + i] = after[i];
        if (before[i] != after[i]) result |= 0x100u << i;
    }
    response[6] = result;
    response[7] = ba03_exception;
#if defined(BA05_STAGE) && (BA05_STAGE == 6 || BA05_STAGE == 7)
    response[32] = 250;
    response[31] = (uint32_t)badger_schedule_bootsel(response[32]);
    if (response[31] != 0) response[6] = 0xffff0502u;
#endif
    __asm__ volatile ("dsb sy" ::: "memory");
    ba03_progress = 0x104u;
    __asm__ volatile ("dsb sy" ::: "memory");
    publish_flash();
#if defined(BA05_STAGE) && (BA05_STAGE == 6 || BA05_STAGE == 7)
    ba05_watchdog_wait();
#endif
    /* BOOTSEL clears SRAM; only the temporary flash page survives recovery. */
    (void)badger_reboot_to_bootsel();
    while (1) __asm__ volatile ("wfi");
}

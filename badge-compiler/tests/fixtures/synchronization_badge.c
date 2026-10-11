#include "../../../src/arch/rp2350/bootrom_reboot.h"

extern uint32_t ba05_lock(volatile uint32_t *, uint32_t);
extern void ba05_unlock(volatile uint32_t *);
extern uint32_t ba05_acquire(volatile uint32_t *);
extern void ba05_release(volatile uint32_t *, uint32_t);
extern uint32_t ba05_reserve(volatile uint32_t *);
extern uint32_t ba05_commit(volatile uint32_t *, uint32_t);
extern uint32_t ba05_basic_probe(volatile uint32_t *);
extern void ba05_checkpoint(uint32_t);
extern void ba05_park(volatile uint32_t *, uint32_t) __attribute__((noreturn));

#ifndef BA05_STAGE
#define BA05_STAGE 0
#endif
#define REG(address) (*(volatile uint32_t *)(address))
#define ITERATIONS 1024u
#define POLL_BUDGET 2000000u
#define LOCK_BUDGET 65536u
#define REPORT ((volatile uint32_t *)0x2007f100u)

static volatile uint32_t lock[4] __attribute__((aligned(16)));
static volatile uint32_t forced[4] __attribute__((aligned(16)));
static volatile uint32_t stage[4] __attribute__((aligned(16)));
static volatile uint32_t ready[4] __attribute__((aligned(16)));
static volatile uint32_t start[4] __attribute__((aligned(16)));
static volatile uint32_t done[4] __attribute__((aligned(16)));
static volatile uint32_t counter, checksum, completed[2], errors[2], core_state[2][12];

static uint32_t wait_value(volatile uint32_t *word, uint32_t expected) {
    for (uint32_t i = 0; i < POLL_BUDGET; ++i) {
        if (ba05_acquire(word) == expected) return 0;
        __asm__ volatile ("yield");
    }
    return 1;
}

static void snapshot(uint32_t core) {
    static const uintptr_t addresses[9] = {
        0x4001003cu, 0x40010040u, 0x40010044u,
        0x40010030u, 0x40010034u, 0x40010038u,
        0xe000ed00u, 0xe000ed94u, 0xe000e008u
    };
    for (uint32_t i = 0; i < 9; ++i) core_state[core][i] = REG(addresses[i]);
    uint32_t control, primask;
    __asm__ volatile ("mrs %0,control\nmrs %1,primask" : "=r"(control), "=r"(primask));
    core_state[core][9] = control;
    core_state[core][10] = primask;
    core_state[core][11] = REG(0xe000ed08u);
}

static uint32_t external_exclusives(void) {
    uint32_t control;
    __asm__ volatile ("mrs %0,control" : "=r"(control));
    if ((control & 1u) || REG(0xe000ed94u) != 0) return 2;
    __asm__ volatile ("dsb sy" ::: "memory");
    REG(0xe000e008u) |= 1u << 29;
    __asm__ volatile ("dsb sy\nisb sy" ::: "memory");
    return (REG(0xe000e008u) & (1u << 29)) ? 0u : 1u;
}

static uint32_t enable_xosc(void) {
    if (!(REG(0x40048004u) & (1u << 12))) {
        REG(0x4004800cu) = 196;
        REG(0x40048000u) = 0x00fabaa0u;
    }
    for (uint32_t i = 0; i < POLL_BUDGET; ++i)
        if (REG(0x40048004u) & 0x80000000u) break;
    REPORT[63] = REG(0x40048004u);
    if (!(REPORT[63] & 0x80000000u)) return 1;
    return 0;
}

static uint32_t configure_clock(void) {
    uint32_t error = enable_xosc();
    if (error) return error;
    /* Keep the live AUXSRC unchanged until the glitchless switch completes. */
    REG(0x4001003cu) &= ~1u;
    if (wait_value((volatile uint32_t *)0x40010044u, 1) != 0) return 2;
    REG(0x40010034u) = 1u << 16;
    REG(0x40010030u) = (REG(0x40010030u) & ~3u) | 2u;
    if (wait_value((volatile uint32_t *)0x40010038u, 4) != 0) return 3;
    REG(0x40010040u) = 1u << 16;
    __asm__ volatile ("dsb sy\nisb sy" ::: "memory");
    return 0;
}

static void updates(uint32_t core) {
    for (uint32_t i = 0; i < ITERATIONS; ++i) {
        if (ba05_lock(lock, LOCK_BUDGET) != 0) {
            errors[core] = 10;
            return;
        }
        if (checksum != counter * (counter + 1u) / 2u) {
            errors[core] = 11;
            ba05_unlock(lock);
            return;
        }
        uint32_t next = counter + 1u;
        counter = next;
        checksum += next;
        ++completed[core];
        ba05_unlock(lock);
    }
}

static void core1_fault(void) {
    uint32_t exception;
    __asm__ volatile ("mrs %0,ipsr" : "=r"(exception));
    errors[1] = 0xfffe0000u | exception;
    ba05_park(done, 1);
}

static void core1_entry(void) {
    REPORT[26] = REG(0xe000e008u);
    if (external_exclusives() != 0) {
        errors[1] = 98;
        ba05_park(done, 1);
    }
    snapshot(1);
    REPORT[30] = REG(0xd0000000u);
#if BA05_STAGE == 1
    ba05_release(ready, 1);
    ba05_park(done, 1);
#elif BA05_STAGE == 3
    ba05_release(ready, 1);
    if (wait_value(stage, 1) != 0) {
        errors[1] = 2;
        ba05_park(done, 1);
    }
    forced[0] = 0x12345678u;
    ba05_release(stage, 2);
    ba05_park(done, 1);
#elif BA05_STAGE == 0
    REPORT[32] = ba05_lock(lock, 32);
    if (REPORT[32] != 1) {
        errors[1] = 1;
        ba05_park(done, 1);
    }
    ba05_release(ready, 1);
    if (wait_value(stage, 1) != 0) {
        errors[1] = 2;
        ba05_park(done, 1);
    }
    forced[0] = 0x12345678u;
    ba05_release(stage, 2);
    if (wait_value(start, 1) != 0) {
        errors[1] = 3;
        ba05_park(done, 1);
    }
    updates(1);
    ba05_park(done, 1);
#else
    errors[1] = 99;
    ba05_park(done, 1);
#endif
}

__attribute__((aligned(256), used))
static void (*const core1_vectors[])(void) = {
    (void (*)(void))0x20081000u, core1_entry, core1_fault, core1_fault,
    core1_fault, core1_fault, core1_fault, core1_fault,
    0, 0, 0, core1_fault, core1_fault, 0, core1_fault, core1_fault
};

void ba05_stop_core1(void) {
    REG(0x4001a004u) = 1u << 24;
    __asm__ volatile ("dsb sy" ::: "memory");
    for (uint32_t i = 0; i < POLL_BUDGET; ++i)
        if (!(REG(0x4001800cu) & (1u << 24))) return;
    REPORT[6] = 0xffff0501u;
    (void)badger_reboot_to_bootsel();
    while (1) __asm__ volatile ("wfi");
}

static uint32_t launch(void) {
    const uint32_t commands[6] = {0, 0, 1, (uint32_t)(uintptr_t)core1_vectors,
        0x20081000u, (uint32_t)(uintptr_t)core1_entry};
    ba05_stop_core1();
    if (wait_value((volatile uint32_t *)0x40018004u, 1u << 24) != 0) return 1;
    REG(0x4001b004u) = 1u << 24;
    REG(0xd0000050u) = 12;
    uint32_t seq = 0;
    for (uint32_t budget = 32; budget != 0 && seq < 6; --budget) {
        uint32_t command = commands[seq];
        if (command == 0) {
            for (uint32_t i = 0; i < 4 && (REG(0xd0000050u) & 1u); ++i)
                (void)REG(0xd0000058u);
            __asm__ volatile ("sev");
        }
        uint32_t available = 0;
        for (uint32_t i = 0; i < POLL_BUDGET; ++i) {
            if (REG(0xd0000050u) & 2u) { available = 1; break; }
        }
        if (!available) return 2;
        REG(0xd0000054u) = command;
        __asm__ volatile ("sev");
        available = 0;
        for (uint32_t i = 0; i < POLL_BUDGET; ++i) {
            if (REG(0xd0000050u) & 1u) { available = 1; break; }
        }
        if (!available) return 3;
        seq = REG(0xd0000058u) == command ? seq + 1u : 0u;
    }
    if (seq != 6 || (REG(0xd0000050u) & 12u)) return 4;
    return 0;
}

void ba03_svc(void) {}

static void report_state(void) {
    const uint32_t fields[12] = {34, 36, 38, 40, 42, 44, 46, 54, 56, 48, 50, 52};
    for (uint32_t i = 0; i < 12; ++i)
        for (uint32_t core = 0; core < 2; ++core) REPORT[fields[i] + core] = core_state[core][i];
    REPORT[58] = done[0];
    REPORT[59] = REG(0x40018004u);
    REPORT[60] = (uint32_t)(uintptr_t)ba05_park;
    REPORT[61] = (uint32_t)(uintptr_t)core1_vectors;
}

uint32_t ba03_probe(void) {
    for (uint32_t i = 21; i < 64; ++i) REPORT[i] = 0;
    lock[0] = 1; forced[0] = 0; stage[0] = 0; ready[0] = 0; start[0] = 0; done[0] = 0;
    counter = 0; checksum = 0;
    for (uint32_t i = 0; i < 2; ++i) {
        completed[i] = 0; errors[i] = 0;
        for (uint32_t j = 0; j < 12; ++j) core_state[i][j] = 0;
    }
    REPORT[24] = REG(0xe000e008u);
    if (external_exclusives() != 0) return 9;
#if BA05_STAGE != 0
    REPORT[21] = 0x4c354142u;
    REPORT[22] = BA05_STAGE;
    ba05_stop_core1();
    snapshot(0);
    report_state();
    ba05_checkpoint(BA05_STAGE);
    uint32_t result = 0;
#if BA05_STAGE == 1 || BA05_STAGE == 3
    REPORT[31] = launch();
    if (REPORT[31] != 0 || wait_value(ready, 1) != 0) result = 1;
#if BA05_STAGE == 3
    if (!result) {
        (void)ba05_reserve(forced);
        ba05_release(stage, 1);
        if (wait_value(stage, 2) != 0) result = 2;
        else {
            REPORT[29] = ba05_commit(forced, 0xdeadbeefu);
            REPORT[33] = forced[0];
            if (REPORT[29] == 0 || REPORT[33] != 0x12345678u) result = 3;
        }
    }
#endif
    if (!result && wait_value(done, 1) != 0) result = 4;
    ba05_stop_core1();
    if (!result && errors[1]) result = 5;
#else
#if BA05_STAGE == 2
    REPORT[23] = ba05_basic_probe(forced);
    if (REPORT[23] != 0) result = 6;
#elif BA05_STAGE == 4
    result = enable_xosc();
#elif BA05_STAGE == 5 || BA05_STAGE == 7
    result = configure_clock();
#elif BA05_STAGE != 6
#error Unsupported BA05 startup stage
#endif
    snapshot(1);
#endif
    REPORT[27] = errors[0]; REPORT[28] = errors[1];
    report_state();
    REPORT[62] = result;
    return result;
#else
    REPORT[21] = 0x5352414du;
    REPORT[22] = ITERATIONS;
    uint32_t clock_error = configure_clock();
    if (clock_error != 0) { REPORT[62] = clock_error; return 8; }
    snapshot(0);
    REPORT[31] = launch();
    uint32_t result = REPORT[31] == 0 ? 0 : 1;
    if (!result && wait_value(ready, 1) != 0) result = 2;
    if (!result && ba05_lock(lock, 32) != 1) result = 3;
    if (!result) {
        ba05_unlock(lock);
        (void)ba05_reserve(forced);
        ba05_release(stage, 1);
        if (wait_value(stage, 2) != 0) result = 4;
        else {
            REPORT[29] = ba05_commit(forced, 0xdeadbeefu);
            REPORT[33] = forced[0];
            if (REPORT[29] == 0 || REPORT[33] != 0x12345678u) result = 5;
        }
    }
    if (!result) {
        ba05_release(start, 1);
        updates(0);
        if (wait_value(done, 1) != 0) result = 6;
    }
    ba05_stop_core1();
    REPORT[23] = counter; REPORT[24] = checksum;
    REPORT[25] = completed[0]; REPORT[26] = completed[1];
    REPORT[27] = errors[0]; REPORT[28] = errors[1];
    report_state();
    if (!result && (counter != 2u * ITERATIONS ||
        checksum != counter * (counter + 1u) / 2u ||
        completed[0] != ITERATIONS || completed[1] != ITERATIONS || errors[0] || errors[1]))
        result = 7;
    REPORT[62] = result;
    return result;
#endif
}

#include "bond.h"
#include "../src/shared/bytes.h"

typedef uintptr_t (*lookup_t)(uint32_t, uint32_t);
typedef int32_t (*flash_op_t)(uint32_t, uintptr_t, uint32_t, const uint8_t *);
typedef void (*flush_t)(void);
static int32_t last_error;
int32_t clicker_flash_error(void) {return last_error;}

__attribute__((section(".ramfunc"), noinline))
static int32_t operate(flash_op_t function, flush_t flush, uint32_t flags, uintptr_t address,
                         uint32_t length, const uint8_t *data) {
    volatile uint32_t *lock = (volatile uint32_t *)0x400e0810u;
    uint32_t primask;
    __asm__ volatile ("mrs %0, primask\ncpsid i" : "=r"(primask) :: "memory");
    if (*lock == 0u) {
        __asm__ volatile ("msr primask, %0" :: "r"(primask) : "memory");
        return -2001;
    }
    __asm__ volatile ("dmb" ::: "memory");
    int32_t result = function(flags, address, length, data);
    /* Checked flash operations do not invalidate cached XIP readback data. */
    flush();
    __asm__ volatile ("dsb\nisb" ::: "memory");
    *lock = 0u;
    __asm__ volatile ("dmb\nmsr primask, %0" :: "r"(primask) : "memory");
    return result;
}

static int execute(uint32_t offset, const uint8_t *data, uint32_t length, uint32_t op) {
    if (offset < CLICKER_BOND_OFFSET || length > 8192u ||
        offset - CLICKER_BOND_OFFSET > 8192u - length) return 0;
    if (op==1u && ((uintptr_t)data<0x20000000u ||
                   (uintptr_t)data>0x20082000u-length)) return 0;
    uintptr_t address = *(const volatile uint16_t *)0x16u;
    if ((address & 1u) == 0u || address<0x20u || address >= 0x10000u) return 0;
    lookup_t lookup = (lookup_t)address;
    address = lookup(0x4f46u, 4u);
    if ((address & 1u) == 0u || address<0x20u || address >= 0x10000u) return 0;
    uintptr_t flush=lookup(0x4346u,4u);
    if ((flush&1u)==0u || flush<0x20u || flush>=0x10000u) {last_error=-2002;return 0;}
    last_error=operate((flash_op_t)address,(flush_t)flush,0x100u | (op << 16u),
                       0x10000000u + offset, length, data);
    return last_error==0;
}
int clicker_flash_erase(void *context, uint32_t offset) {
    (void)context;
    if ((offset & 4095u) != 0u) return 0;
    return execute(offset, 0, 4096u, 0u);
}
int clicker_flash_program(void *context, uint32_t offset, const uint8_t *data, uint32_t length) {
    (void)context;
    if ((offset & 255u) != 0u || (length & 255u) != 0u || !length || data == 0) return 0;
    return execute(offset, data, length, 1u);
}
int clicker_flash_read(void *context, uint32_t offset, uint8_t *data, uint32_t length) {
    (void)context;
    if (offset < CLICKER_BOND_OFFSET || length > 8192u ||
        offset - CLICKER_BOND_OFFSET > 8192u - length || data == 0) return 0;
    badger_bytes_copy(data, (const uint8_t *)(uintptr_t)(0x10000000u + offset), length);
    return 1;
}

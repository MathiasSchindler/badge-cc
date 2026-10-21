#include "bootrom.h"

typedef uintptr_t (*rom_lookup_t)(uint32_t code, uint32_t flags);

static int rom_thumb_pointer(uintptr_t value) {
    return (value & 1u) != 0u && value >= 0x20u && value < 0x10000u;
}

static uintptr_t lookup_function(uint32_t code) {
    uintptr_t address = *(const volatile uint16_t *)0x16u;
    if (!rom_thumb_pointer(address)) return 0u;
    rom_lookup_t lookup = (rom_lookup_t)address;
    address = lookup(code, 0x0004u);
    return rom_thumb_pointer(address) ? address : 0u;
}

int32_t rp2350_schedule_bootsel(uint32_t delay_ms) {
    if (delay_ms == 0u || delay_ms > 8000u) return -1004;
    uintptr_t address = lookup_function(0x4252u);
    if (address == 0u) return -1002;
    typedef int32_t (*reboot_fn_t)(uint32_t, uint32_t, uint32_t, uint32_t);
    reboot_fn_t reboot = (reboot_fn_t)address;
    __asm__ volatile ("dsb sy" ::: "memory");
    return reboot(0x2u, delay_ms, 0u, 0u);
}

int32_t rp2350_chip_id(uint32_t id[2]) {
    if (id == 0) return -1001;
    uintptr_t address = lookup_function(0x5347u);
    if (address == 0u) return -1002;
    typedef int32_t (*info_fn_t)(uint32_t *, uint32_t, uint32_t);
    info_fn_t info = (info_fn_t)address;
    uint32_t data[4] = {0};
    int32_t result = info(data, 4u, 1u);
    if (result != 4 || (data[0] & 1u) == 0u) return -1005;
    id[0] = data[2];
    id[1] = data[3];
    return 0;
}

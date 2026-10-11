#include "bootrom_reboot.h"

typedef uintptr_t (*rom_lookup_t)(uint32_t code, uint32_t flags);
typedef int32_t (*rom_reboot_t)(uint32_t flags, uint32_t delay_ms,
                                uint32_t p0, uint32_t p1);

static int rom_thumb_pointer(uintptr_t value) {
    return (value & 1u) != 0u && value >= 0x20u && value < 0x10000u;
}

static int32_t reboot_to_bootsel(uint32_t flags, uint32_t delay_ms) {
    uintptr_t lookup_address = *(const volatile uint16_t *)0x16u;
    if (!rom_thumb_pointer(lookup_address)) return -1001;
    rom_lookup_t lookup = (rom_lookup_t)lookup_address;
    uintptr_t reboot_address = lookup(0x4252u, 0x0004u);
    if (!rom_thumb_pointer(reboot_address)) return -1002;
    rom_reboot_t reboot = (rom_reboot_t)reboot_address;
    __asm__ volatile ("dsb" ::: "memory");
    return reboot(flags, delay_ms, 0u, 0u);
}

int32_t badger_reboot_to_bootsel(void) {
    int32_t result = reboot_to_bootsel(0x102u, 10u);
    return result == 0 ? -1003 : result;
}

int32_t badger_schedule_bootsel(uint32_t delay_ms) {
    if (delay_ms == 0u || delay_ms > 8000u) return -1004;
    return reboot_to_bootsel(0x2u, delay_ms);
}

int32_t badger_rom_chip_id(uint32_t id[2]) {
    uintptr_t lookup_address = *(const volatile uint16_t *)0x16u;
    if (id == 0 || !rom_thumb_pointer(lookup_address)) return -1001;
    rom_lookup_t lookup = (rom_lookup_t)lookup_address;
    uintptr_t info_address = lookup(0x5347u, 0x0004u);
    if (!rom_thumb_pointer(info_address)) return -1002;
    typedef int32_t (*info_fn_t)(uint32_t *, uint32_t, uint32_t);
    info_fn_t info = (info_fn_t)info_address;
    uint32_t data[4] = {0};
    int32_t result = info(data, 4u, 1u);
    if (result != 4 || (data[0] & 1u) == 0u) return -1005;
    id[0] = data[2];
    id[1] = data[3];
    return 0;
}

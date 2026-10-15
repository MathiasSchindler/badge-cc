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

static int32_t rom_info(uint32_t *data, uint32_t words, uint32_t flag) {
    uintptr_t lookup_address = *(const volatile uint16_t *)0x16u;
    if (!rom_thumb_pointer(lookup_address)) return -1001;
    rom_lookup_t lookup = (rom_lookup_t)lookup_address;
    uintptr_t info_address = lookup(0x5347u, 0x0004u);
    if (!rom_thumb_pointer(info_address)) return -1002;
    typedef int32_t (*info_fn_t)(uint32_t *, uint32_t, uint32_t);
    info_fn_t info = (info_fn_t)info_address;
    int32_t result = info(data, words, flag);
    return result == (int32_t)words && (data[0] & flag) != 0u ? 0 : -1005;
}

int32_t badger_rom_chip_id(uint32_t id[2]) {
    if (id == 0) return -1001;
    uint32_t data[4] = {0};
    int32_t result = rom_info(data, 4u, 1u);
    if (result != 0) return result;
    id[0] = data[2];
    id[1] = data[3];
    return 0;
}

int32_t badger_rom_boot_random(uint32_t random[4]) {
    if (random == 0) return -1001;
    uint32_t data[5];
    int32_t result = rom_info(data, 5u, 0x10u);
    if (result != 0) return result;
    for (uint32_t i = 0u; i < 4u; ++i) random[i] = data[i + 1u];
    return 0;
}

#ifndef UNIVERSE26_BOOTROM_REBOOT_H
#define UNIVERSE26_BOOTROM_REBOOT_H

#include <stdint.h>

/* Returns an error only; successful BOOTSEL entry does not return. */
int32_t badger_reboot_to_bootsel(void);
int32_t badger_schedule_bootsel(uint32_t delay_ms);
int32_t badger_rom_chip_id(uint32_t id[2]);
int32_t badger_rom_boot_random(uint32_t random[4]);

#endif

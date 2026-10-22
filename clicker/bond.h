#ifndef CLICKER_BOND_H
#define CLICKER_BOND_H
#include <stdint.h>
#define CLICKER_BONDS 4u
#define CLICKER_BOND_OFFSET 0x003fe000u
typedef struct {
    uint8_t valid, type, address[6], ltk[16], random[8];
    uint16_t ediv;
    uint8_t has_irk, irk[16];
    uint16_t report_ccc, boot_ccc, service_ccc;
} clicker_bond_t;
typedef struct {
    clicker_bond_t peers[CLICKER_BONDS];
    uint32_t generation;
    uint8_t sector, initialized;
} clicker_bonds_t;
typedef struct {
    void *context;
    int (*erase)(void *context, uint32_t offset);
    int (*program)(void *context, uint32_t offset, const uint8_t *data, uint32_t length);
    int (*read)(void *context, uint32_t offset, uint8_t *data, uint32_t length);
} clicker_bond_io_t;
/* 1 valid/erased store, 0 corruption/I/O failure. Interrupted uncommitted
   sectors are ignored only when an independently committed sector is valid. */
int clicker_bonds_load(clicker_bonds_t *bonds, const clicker_bond_io_t *io);
int clicker_bonds_save(clicker_bonds_t *bonds, const clicker_bond_io_t *io);
int clicker_flash_erase(void *context, uint32_t offset);
int clicker_flash_program(void *context, uint32_t offset, const uint8_t *data, uint32_t length);
int clicker_flash_read(void *context, uint32_t offset, uint8_t *data, uint32_t length);
int32_t clicker_flash_error(void);
#endif

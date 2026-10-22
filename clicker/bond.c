#include "bond.h"
#include "crypto.h"
#include "../src/shared/bytes.h"
#include "../src/tools/diagnostics_logic.h"

static uint16_t u16(const uint8_t *p) { return (uint16_t)((uint32_t)p[0] | (uint32_t)p[1] << 8u); }
static void put16(uint8_t *p, uint16_t value) { p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8u); }
static void encode(uint8_t *p, const clicker_bond_t *bond) {
    badger_bytes_zero(p, 64u);
    p[0] = bond->valid; p[1] = bond->type;
    badger_bytes_copy(p + 2u, bond->address, 6u);
    badger_bytes_copy(p + 8u, bond->ltk, 16u);
    badger_bytes_copy(p + 24u, bond->random, 8u);
    put16(p + 32u, bond->ediv); p[34] = bond->has_irk;
    badger_bytes_copy(p + 36u, bond->irk, 16u);
    put16(p + 52u, bond->report_ccc); put16(p + 54u, bond->boot_ccc); put16(p + 56u, bond->service_ccc);
}
static int decode(clicker_bond_t *bond, const uint8_t *p) {
    if (p[0] > 1u || p[1] > 1u || p[34] > 1u || u16(p + 52u) > 1u ||
        u16(p + 54u) > 1u || (u16(p + 56u) != 0u && u16(p + 56u) != 2u)) return 0;
    bond->valid = p[0]; bond->type = p[1]; bond->has_irk = p[34];
    badger_bytes_copy(bond->address, p + 2u, 6u);
    badger_bytes_copy(bond->ltk, p + 8u, 16u);
    badger_bytes_copy(bond->random, p + 24u, 8u);
    badger_bytes_copy(bond->irk, p + 36u, 16u);
    bond->ediv = u16(p + 32u);
    bond->report_ccc = u16(p + 52u); bond->boot_ccc = u16(p + 54u); bond->service_ccc = u16(p + 56u);
    return 1;
}

static int valid(const uint8_t data[768]) {
    return badger_bytes_load_u32(data) == 0x43424e44u && badger_bytes_load_u32(data + 4u) == 1u &&
        badger_bytes_load_u32(data + 508u) == diag_crc32(data, 508u) &&
        badger_bytes_load_u32(data + 512u) == 0x434d4954u &&
        badger_bytes_load_u32(data + 516u) == badger_bytes_load_u32(data + 8u) &&
        badger_bytes_load_u32(data + 520u) == badger_bytes_load_u32(data + 508u) &&
        badger_bytes_load_u32(data + 524u) == diag_crc32(data + 512u, 12u);
}

int clicker_bonds_load(clicker_bonds_t *bonds, const clicker_bond_io_t *io) {
    uint8_t data[768], selected[512];
    uint8_t found = 0u, erased = 1u;
    badger_bytes_zero((uint8_t *)bonds, sizeof(*bonds));
    for (uint32_t sector = 0u; sector < 2u; ++sector) {
        if (!io->read(io->context, CLICKER_BOND_OFFSET + sector * 4096u, data, sizeof(data))) return 0;
        for (uint32_t i = 0u; i < sizeof(data); ++i) if (data[i] != 255u) erased = 0u;
        if (!valid(data)) continue;
        uint32_t generation = badger_bytes_load_u32(data + 8u);
        if (!found || (int32_t)(generation - bonds->generation) > 0) {
            badger_bytes_copy(selected, data, sizeof(selected));
            bonds->generation = generation;
            bonds->sector = (uint8_t)sector;
            found = 1u;
        }
    }
    if (found) {
        for (uint32_t i = 0u; i < CLICKER_BONDS; ++i) {
            if (!decode(&bonds->peers[i], selected + 16u + i * 64u)) {
                clicker_secret_clear(selected, sizeof(selected));
                clicker_secret_clear(data, sizeof(data));
                return 0;
            }
        }
    }
    bonds->initialized = found;
    clicker_secret_clear(selected, sizeof(selected));
    clicker_secret_clear(data, sizeof(data));
    return found || erased;
}

int clicker_bonds_save(clicker_bonds_t *bonds, const clicker_bond_io_t *io) {
    uint8_t data[768], readback[768];
    for (uint32_t i = 0u; i < sizeof(data); ++i) data[i] = 255u;
    badger_bytes_store_u32(data, 0x43424e44u);
    badger_bytes_store_u32(data + 4u, 1u);
    uint32_t generation = bonds->generation + 1u;
    badger_bytes_store_u32(data + 8u, generation);
    for (uint32_t i = 0u; i < CLICKER_BONDS; ++i) encode(data + 16u + i * 64u, &bonds->peers[i]);
    uint32_t crc = diag_crc32(data, 508u);
    badger_bytes_store_u32(data + 508u, crc);
    badger_bytes_store_u32(data + 512u, 0x434d4954u);
    badger_bytes_store_u32(data + 516u, generation);
    badger_bytes_store_u32(data + 520u, crc);
    badger_bytes_store_u32(data + 524u, diag_crc32(data + 512u, 12u));
    uint8_t sector = bonds->initialized ? (uint8_t)(bonds->sector ^ 1u) : 0u;
    uint32_t offset = CLICKER_BOND_OFFSET + sector * 4096u;
    int ok = io->erase(io->context, offset) &&
        io->program(io->context, offset, data, 512u) &&
        io->read(io->context, offset, readback, 512u) &&
        clicker_secret_equal(data, readback, 512u) &&
        io->program(io->context, offset + 512u, data + 512u, 256u) &&
        io->read(io->context, offset, readback, sizeof(readback)) &&
        clicker_secret_equal(data, readback, sizeof(data)) && valid(readback);
    if (ok) { bonds->initialized = 1u; bonds->sector = sector; bonds->generation = generation; }
    clicker_secret_clear(data, sizeof(data));
    clicker_secret_clear(readback, sizeof(readback));
    return ok;
}

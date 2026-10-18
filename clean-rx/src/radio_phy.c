#include "cyw43_hw.h"

#ifndef CLEAN_RX_TX_BBMULT
#define CLEAN_RX_TX_BBMULT 64u
#endif
_Static_assert(CLEAN_RX_TX_BBMULT == 16u || CLEAN_RX_TX_BBMULT == 64u,
               "TX amplitude control supports only the bounded 16/64 profiles");

enum {
    OP_PHY_MOD = 0u,
    OP_PHY_AND = 1u,
    OP_PHY_OR  = 2u,
    OP_PHY_WR  = 3u,
    OP_RAD_MOD = 4u,
    OP_RAD_AND = 5u,
    OP_RAD_OR  = 6u,
    OP_RAD_WR  = 7u,
    OP_DELAY   = 8u
};

typedef struct {
    uint8_t  kind;
    uint16_t index;
    uint16_t mask;
    uint16_t value;
} reg_op_t;

/* Complete 14-channel 2.4 GHz Radio 20691 synthesizer table (37.4 MHz crystal, 31 registers per channel) */
static const uint16_t radio20691_2g_channels[14][31] = {
    /* Ch  1 (2412 MHz) */ {0x4b,0x176,0x13,0x3032,0x16,0x3,0x7e41,0x600,0xa00,0x3d9,0x27,0x25,0,0x991,0,0x96c,0,0,0x305,0xe75b,8,0x583f,0x1212,0x142f,0x1f1f,0x1f1f,0x5120,0x6127,0,0x6009,0x703f},
    /* Ch  2 (2417 MHz) */ {0x4b,0x176,0x13,0x3032,0x16,0x3,0x7e41,0x600,0xa00,0x3d9,0x27,0x25,0,0x991,0,0x971,0,0,0x307,0x820d,8,0x583f,0x1212,0x142f,0x1f1f,0x1f1f,0x5120,0x6127,0,0x6009,0x703f},
    /* Ch  3 (2422 MHz) */ {0x4b,0x176,0x13,0x3032,0x16,0x3,0x7e41,0x600,0xa00,0x3d9,0x27,0x25,0,0x991,0,0x976,0,0,0x309,0x1cbf,8,0x583f,0x1212,0x142f,0x1f1f,0x1f1f,0x5120,0x6127,0,0x6009,0x703f},
    /* Ch  4 (2427 MHz) */ {0x4b,0x176,0x13,0x3032,0x16,0x3,0x7e41,0x600,0xa00,0x3d9,0x27,0x25,0,0x991,0,0x97b,0,0,0x30a,0xb771,8,0x583f,0x1212,0x142f,0x1f1f,0x1f1f,0x5120,0x6127,0,0x6009,0x703f},
    /* Ch  5 (2432 MHz) */ {0x4b,0x176,0x13,0x3032,0x16,0x3,0x7e41,0x600,0xa00,0x3d9,0x27,0x25,0,0x991,0,0x980,0,0,0x30c,0x5223,8,0x583f,0x1212,0x142f,0x1f1f,0x1f1f,0x5121,0x6127,0,0x6009,0x703f},
    /* Ch  6 (2437 MHz) */ {0x4b,0x176,0x13,0x3032,0x16,0x3,0x7e41,0x600,0xa00,0x3d9,0x27,0x25,0,0x991,0,0x985,0,0,0x30d,0xecd5,8,0x583f,0x1212,0x142f,0x1f1f,0x1f1f,0x5121,0x6127,0,0x6009,0x703f},
    /* Ch  7 (2442 MHz) */ {0x4b,0x176,0x13,0x3032,0x16,0x3,0x7e41,0x600,0xa00,0x3d9,0x27,0x25,0,0x991,0,0x98a,0,0,0x30f,0x8787,8,0x583f,0x1212,0x142f,0x1f1f,0x1f1f,0x5121,0x6127,0,0x6008,0x703f},
    /* Ch  8 (2447 MHz) */ {0x4b,0x176,0x13,0x3032,0x16,0x3,0x7e41,0x600,0xa00,0x3d9,0x27,0x25,0,0x991,0,0x98f,0,0,0x311,0x2239,8,0x583f,0x1212,0x142f,0x1f1f,0x1f1f,0x5121,0x6127,0,0x6008,0x703f},
    /* Ch  9 (2452 MHz) */ {0x4b,0x176,0x13,0x3032,0x16,0x3,0x7e41,0x600,0xa00,0x3d9,0x27,0x25,0,0x991,0,0x994,0,0,0x312,0xbceb,8,0x583f,0x1212,0x142f,0x1f1f,0x1f1f,0x5121,0x6127,0,0x6008,0x603f},
    /* Ch 10 (2457 MHz) */ {0x4b,0x176,0x13,0x3032,0x16,0x3,0x7e41,0x600,0xa00,0x3d9,0x27,0x25,0,0x991,0,0x999,0,0,0x314,0x579d,8,0x583f,0x1212,0x142f,0x1f1f,0x1f1f,0x5121,0x6127,0,0x6008,0x603f},
    /* Ch 11 (2462 MHz) */ {0x4b,0x176,0x13,0x3032,0x16,0x3,0x7e41,0x600,0xa00,0x3d9,0x27,0x25,0,0x991,0,0x99e,0,0,0x315,0xf24f,8,0x583f,0x1212,0x142f,0x1f1f,0x1f1f,0x5121,0x6127,0,0x6007,0x603f},
    /* Ch 12 (2467 MHz) */ {0x4b,0x176,0x13,0x3032,0x16,0x3,0x7e41,0x600,0xa00,0x3d9,0x27,0x25,0,0x991,0,0x9a3,0,0,0x317,0x8d01,8,0x583f,0x1212,0x142f,0x1f1f,0x1f1f,0x5121,0x6127,0,0x6007,0x603f},
    /* Ch 13 (2472 MHz) */ {0x4b,0x176,0x13,0x3032,0x16,0x3,0x7e41,0x600,0xa00,0x3d9,0x27,0x25,0,0x991,0,0x9a8,0,0,0x319,0x27b3,8,0x583f,0x1212,0x142f,0x1f1f,0x1f1f,0x5121,0x6128,0,0x6007,0x603f},
    /* Ch 14 (2484 MHz) */ {0x4b,0x176,0x13,0x3032,0x16,0x3,0x7e41,0x600,0xa00,0x3d9,0x27,0x25,0,0x991,0,0x9b4,0,0,0x31d,0x015e,8,0x583f,0x1212,0x142f,0x1f1f,0x1f1f,0x5121,0x6128,0,0x6007,0x603f}
};

static const uint16_t channel_reg_indices[31] = {
    0x62u,0x53u,0x54u,0x57u,0x58u,0x64u,0x51u,0x5cu,0x5du,0x5au,0x5bu,0x63u,0x56u,0x59u,0x61u,0x55u,
    0x5fu,0x52u,0x4eu,0x4fu,0x4du,0x4cu,0x4au,0x4bu,0x48u,0x49u,0x45u,0x46u,0x18u,0x3cu,0x86u
};

static uint8_t rom_board_packed[ROM_BOARD_DATA_BYTES];

static uint32_t get_le(const uint8_t *p, uint32_t bytes) {
    uint32_t v = 0u;
    for (uint32_t i = 0u; i < bytes; ++i)
        v |= (uint32_t)p[i] << (8u * i);
    return v;
}

static void run_ops(const reg_op_t *ops, uint32_t count) {
    for (uint32_t i = 0u; i < count; ++i) {
        const reg_op_t *op = &ops[i];
        switch (op->kind) {
        case OP_PHY_MOD: phy_reg_mod(op->index, op->mask, op->value); break;
        case OP_PHY_AND: phy_reg_mod(op->index, (uint16_t)~op->mask, 0u); break;
        case OP_PHY_OR:  phy_reg_mod(op->index, op->value, op->value); break;
        case OP_PHY_WR:  phy_reg_write(op->index, op->value); break;
        case OP_RAD_MOD: radio_reg_mod(op->index, op->mask, op->value); break;
        case OP_RAD_AND: radio_reg_mod(op->index, (uint16_t)~op->mask, 0u); break;
        case OP_RAD_OR:  radio_reg_mod(op->index, op->value, op->value); break;
        case OP_RAD_WR:  radio_reg_write(op->index, op->value); break;
        case OP_DELAY:   (void)delay_ticks((uint32_t)op->index * 180u); break;
        default: break;
        }
    }
}

static void phy_table_write(uint16_t id, uint16_t offset, uint16_t width, const uint32_t *data, uint32_t count) {
    uint16_t saved_9a5 = phy_reg_read(0x9a5u);
    phy_reg_mod(0x9a5u, 2u, 2u);
    phy_reg_write(0x00du, id);
    phy_reg_write(0x00eu, offset);
    for (uint32_t i = 0u; i < count; ++i) {
        if (width == 32u)
            phy_reg_write(0x457u, (uint16_t)(data[i] >> 16u));
        phy_reg_write(0x456u, (uint16_t)(data[i] & 0xffffu));
    }
    phy_reg_write(0x9a5u, saved_9a5);
}

static void enforce_tx_inhibit_guards(void) {
    phy_reg_mod(0x44du, 0x4000u, 0x0000u);
    phy_reg_mod(0x44cu, 0x4000u, 0x4000u);
    phy_reg_mod(0x939u, 0x0800u, 0x0000u);
    phy_reg_mod(0x938u, 0x0800u, 0x0800u);
    radio_reg_mod(0x79u, 0x0001u, 0x0000u);
    radio_reg_mod(0xa4u, 0x0004u, 0x0004u);
}

static int d11_release_core_and_phy(void) {
    /* 1. Assert D11 reset and clock IOCTRL */
    mmio_write32(D11_WRAP_BASE + 0x800u, 1u);
    (void)delay_ticks(10u * 180u);
    mmio_write32(D11_WRAP_BASE + 0x408u, 0x0fu);
    (void)mmio_read32(D11_WRAP_BASE + 0x408u);

    /* 2. Release D11 core reset while holding PHY reset */
    mmio_write32(D11_WRAP_BASE + 0x800u, 0u);
    (void)mmio_read32(D11_WRAP_BASE + 0x800u);
    mmio_write32(D11_WRAP_BASE + 0x408u, 0x0du);
    (void)mmio_read32(D11_WRAP_BASE + 0x408u);
    (void)delay_ticks(180u);

    /* 3. Set initial stopped MACCONTROL with IHR_EN and WAKE */
    mmio_write32(D11_MACCONTROL, 0x04000004u);
    mmio_write32(D11_MACCONTROL, 0x04000404u);

    /* 4. Step D11 wrapper IOCTRL through PHY clock/reset release (0x0d -> 0x45) */
    static const uint32_t masks[3]  = {0xccu, 0x0eu, 0x06u};
    static const uint32_t values[3] = {0x4cu, 0x02u, 0x04u};
    static const uint32_t delays[3] = {2u, 1u, 1u};
    for (uint32_t i = 0u; i < 3u; ++i) {
        uint32_t cur = mmio_read32(D11_WRAP_BASE + 0x408u);
        uint32_t next = (cur & ~masks[i]) | values[i];
        mmio_write32(D11_WRAP_BASE + 0x408u, next);
        if (mmio_read32(D11_WRAP_BASE + 0x408u) != next) return 0;
        (void)delay_ticks(delays[i] * 180u);
    }

    enforce_tx_inhibit_guards();
    return 1;
}

static void gci_radio_notify(void) {
    static const uint32_t addrs[4]  = {GCI_CONTROL_REG1, GCI_OUTPUT_REG1, GCI_OUTPUT_REG1, GCI_OUTPUT_REG1};
    static const uint32_t masks[4]  = {8u, 8u, 0xf000u, 0x0e00u};
    static const uint32_t values[4] = {8u, 8u, 0x1000u, 0x0200u};
    for (uint32_t i = 0u; i < 4u; ++i) {
        uint32_t cur = mmio_read32(addrs[i]);
        mmio_write32(addrs[i], (cur & ~masks[i]) | values[i]);
        (void)mmio_read32(addrs[i]);
    }
}

static void load_rom_board_tables(void) {
    for (uint32_t at = 0u; at < ROM_BOARD_DATA_BYTES; at += 4u) {
        uint32_t w = mmio_read32(ROM_BOARD_DATA_BASE + at);
        rom_board_packed[at + 0u] = (uint8_t)w;
        rom_board_packed[at + 1u] = (uint8_t)(w >> 8u);
        rom_board_packed[at + 2u] = (uint8_t)(w >> 16u);
        rom_board_packed[at + 3u] = (uint8_t)(w >> 24u);
    }
}

static void program_phy_common_and_gain_tables(void) {
    static const uint16_t common_ids[5]     = {2u, 1u, 20u, 23u, 0u};
    static const uint16_t common_widths[5]  = {16u, 16u, 8u, 16u, 16u};
    static const uint16_t common_counts[5]  = {128u, 64u, 256u, 154u, 108u};
    static const uint16_t common_offsets[5] = {0x446u, 0x546u, 0x5c6u, 0x6c6u, 0x36eu};
    uint32_t buf[256];

    for (uint32_t t = 0u; t < 5u; ++t) {
        uint32_t bytes = (common_widths[t] == 8u) ? 1u : 2u;
        for (uint32_t j = 0u; j < common_counts[t]; ++j)
            buf[j] = get_le(rom_board_packed + common_offsets[t] + j * bytes, bytes);
        phy_table_write(common_ids[t], 0u, common_widths[t], buf, common_counts[t]);
    }

    /* Gain tables: ID 17 (26 x 8-bit), ID 18 (96 x 32-bit), ID 13 (76 x 32-bit) */
    for (uint32_t j = 0u; j < 26u; ++j)
        buf[j] = rom_board_packed[852u + j];
    phy_table_write(17u, 0u, 8u, buf, 26u);

    for (uint32_t j = 0u; j < 96u; ++j)
        buf[j] = get_le(rom_board_packed + 468u + j * 4u, 4u);
    phy_table_write(18u, 0u, 32u, buf, 96u);

    for (uint32_t j = 0u; j < 76u; ++j)
        buf[j] = get_le(rom_board_packed + 164u + j * 4u, 4u);
    phy_table_write(13u, 0u, 32u, buf, 76u);
}

static void program_phy_defaults(void) {
    static const reg_op_t defaults[] = {
        {OP_PHY_MOD, 0x440u, 0x0001u, 0x0000u}, {OP_PHY_WR,  0x44au, 0x0000u, 0x004fu},
        {OP_PHY_WR,  0x43au, 0x0000u, 0x0000u}, {OP_PHY_WR,  0x575u, 0x0000u, 0x0000u},
        {OP_PHY_WR,  0x44cu, 0x0000u, 0x4000u}, {OP_PHY_WR,  0x4b0u, 0x0000u, 0x0000u},
        {OP_PHY_WR,  0x4f9u, 0x0000u, 0x0000u}, {OP_PHY_WR,  0x938u, 0x0000u, 0x0800u},
        {OP_PHY_WR,  0x53du, 0x0000u, 0x0000u}, {OP_PHY_WR,  0x571u, 0x0000u, 0x0000u},
        {OP_PHY_WR,  0x573u, 0x0000u, 0x0000u}, {OP_PHY_WR,  0x44eu, 0x0000u, 0x0000u},
        {OP_PHY_MOD, 0x534u, 0x00ffu, 0x0012u}, {OP_PHY_MOD, 0x4e4u, 0xff00u, 0x0600u},
        {OP_PHY_MOD, 0x44du, 0x0200u, 0x0000u}, {OP_PHY_MOD, 0x44cu, 0x0200u, 0x0200u},
        {OP_PHY_MOD, 0x536u, 0x0010u, 0x0010u}, {OP_PHY_MOD, 0x440u, 0x0001u, 0x0001u},
        {OP_PHY_WR,  0x44au, 0x0000u, 0x0000u}, {OP_PHY_MOD, 0x655u, 0xf000u, 0x8000u},
        {OP_PHY_MOD, 0xadau, 0x4000u, 0x4000u}
    };
    run_ops(defaults, sizeof(defaults) / sizeof(defaults[0]));
}

static int32_t positive(int32_t v) { return v < 1 ? 1 : v; }
static uint16_t ceiling(int32_t v, uint32_t max_v) {
    return (uint16_t)(v > (int32_t)max_v ? (int32_t)max_v : v);
}
static int32_t asr12(uint32_t v) {
    return (v & 0x80000000u) ? -(int32_t)((~v) >> 12u) - 1 : (int32_t)(v >> 12u);
}

static int radio_init_and_rc_adc_tia_cal(uint32_t *rc_lpf_out, uint32_t *rc_adc_out, uint32_t *rc_dacbuf_out) {
    /* 1. Switch D11 IOCTRL to 0x2055 (MACPHY clock enable + GMODE) */
    mmio_write32(D11_WRAP_BASE + 0x408u, 0x2055u);
    if (mmio_read32(D11_WRAP_BASE + 0x408u) != 0x2055u) return 0;

    static const reg_op_t power_ops[] = {
        {OP_RAD_MOD, 0x8cu, 0x0001u, 0x0001u},
        {OP_RAD_MOD, 0x9bu, 0x0040u, 0x0040u}, {OP_RAD_MOD, 0x19u, 0x0080u, 0x0080u},
        {OP_RAD_MOD, 0x9bu, 0x0010u, 0x0010u}, {OP_RAD_MOD, 0x19u, 0x0008u, 0x0008u},
        {OP_DELAY,   100u,  0u,      0u},
        {OP_RAD_MOD, 0xa7u, 0x0001u, 0x0001u}, {OP_RAD_MOD, 0x89u, 0x0001u, 0x0001u},
        {OP_RAD_MOD, 0x97u, 0x0001u, 0x0001u}, {OP_RAD_MOD, 0x04u, 0x0001u, 0x0001u},
        {OP_DELAY,   20u,   0u,      0u},
        {OP_RAD_MOD, 0x9bu, 0x0020u, 0x0020u}, {OP_RAD_MOD, 0x9bu, 0x0008u, 0x0008u},
        {OP_RAD_MOD, 0x9bu, 0x0004u, 0x0004u}, {OP_RAD_MOD, 0x9bu, 0x0002u, 0x0002u},
        {OP_RAD_MOD, 0x19u, 0x0010u, 0x0010u}, {OP_RAD_MOD, 0x19u, 0x0004u, 0x0004u},
        {OP_RAD_MOD, 0x19u, 0x0002u, 0x0002u}, {OP_RAD_MOD, 0x19u, 0x0001u, 0x0001u},
        {OP_DELAY,   200u,  0u,      0u},
        {OP_RAD_MOD, 0x43u, 0x0001u, 0x0001u}, {OP_RAD_MOD, 0x51u, 0x0001u, 0x0000u},
        {OP_RAD_MOD, 0xa0u, 0x0020u, 0x0020u}, {OP_RAD_MOD, 0x42u, 0x0002u, 0x0002u},
        {OP_RAD_MOD, 0xa0u, 0x0040u, 0x0040u}, {OP_RAD_MOD, 0x42u, 0x0004u, 0x0004u},
        {OP_RAD_MOD, 0xa0u, 0x0010u, 0x0010u}, {OP_RAD_MOD, 0x42u, 0x0001u, 0x0001u},
        {OP_RAD_MOD, 0xa0u, 0x0080u, 0x0080u}, {OP_RAD_MOD, 0x42u, 0x0008u, 0x0008u},
        {OP_RAD_MOD, 0x97u, 0x0004u, 0x0004u}, {OP_RAD_MOD, 0x04u, 0x00f0u, 0x0070u},
        {OP_RAD_MOD, 0x97u, 0x0002u, 0x0000u}, {OP_RAD_MOD, 0x8au, 0x0200u, 0x0000u}
    };
    run_ops(power_ops, sizeof(power_ops) / sizeof(power_ops[0]));

    /* 2. Request PHYPLL availability on D11 clock control (0x180011e0 = 0x240) */
    mmio_write32(D11_CLK_CTL_ST, 0x240u);
    uint32_t pll_ready = 0u;
    for (uint32_t i = 0u; i < 10000u; ++i) {
        if ((mmio_read32(PMU_RES_STATE) & 0x3eca3df7u) == 0x3eca3df7u &&
            (mmio_read32(D11_CLK_CTL_ST) & 0x02000000u) != 0u) {
            pll_ready = 1u;
            break;
        }
        (void)delay_ticks(1800u);
    }
    if (!pll_ready) return 0;

    /* 3. Two-pass RC calibration (Pass 0 = LPF, Pass 1 = ADC) */
    uint16_t old_8a = radio_reg_read(0x8au);
    radio_reg_write(0x8au, (uint16_t)((old_8a & ~0x07f0u) | ((old_8a & 0x07f0u) | 0x4400u)));
    uint32_t rc_vals[2] = {0u, 0u};

    for (uint32_t cycle = 0u; cycle < 2u; ++cycle) {
        radio_reg_mod(0x1eu, 0x0040u, 0x0040u);
        radio_reg_mod(0x1eu, 0x0030u, cycle ? 0x0010u : 0x0000u);
        radio_reg_mod(0x8fu, 0xff00u, cycle ? 0x7000u : 0x1c00u);
        radio_reg_mod(0x90u, 0x1fffu, cycle ? 0x00f0u : 0x022du);
        radio_reg_mod(0x1eu, 0x0001u, 0x0000u);
        (void)delay_ticks(180u);
        radio_reg_mod(0x1eu, 0x0001u, 0x0001u);
        (void)delay_ticks(35u * 180u);
        radio_reg_mod(0x8fu, 0x0001u, 0x0001u);

        uint32_t done = 0u;
        for (uint32_t s = 0u; s < 100u; ++s) {
            (void)delay_ticks(100u * 180u);
            uint16_t st = radio_reg_read(0x91u);
            if (st != 0xffffu && (st & 1u) != 0u) {
                done = 1u;
                break;
            }
        }
        if (!done) return 0;

        radio_reg_mod(0x8fu, 0x0001u, 0x0000u);
        uint32_t n0 = radio_reg_read(0x92u) & 0x1fffu;
        uint32_t n1 = radio_reg_read(0x93u) & 0x1fffu;
        if (n1 <= n0) return 0;
        rc_vals[cycle] = (cycle ? 102051u : 101541u) * (n1 - n0) / (37400000u >> 12u);
        radio_reg_mod(0x1eu, 0x0001u, 0x0000u);
    }

    uint16_t raw_94 = radio_reg_read(0x94u);
    radio_reg_mod(0xa3u, 0x0100u, 0x0000u);
    radio_reg_write(0x8au, (uint16_t)(radio_reg_read(0x8au) & 0xfbffu));

    *rc_lpf_out    = rc_vals[0];
    *rc_adc_out    = rc_vals[1];
    *rc_dacbuf_out = (raw_94 & 0x03e0u) >> 5u;

    /* 4. Compute and apply 11 ADC coefficients from RC ADC calibration result */
    uint32_t recip = (16777216u / rc_vals[1]) & 0xfffffu;
    int32_t g = (int32_t)(recip ? recip : 1u);
    int32_t sq = positive(asr12((uint32_t)g * (uint32_t)g));
    uint16_t adc[11];
    adc[0]  = 16u;
    adc[1]  = ceiling(511999 / (68182015 / sq + 1) + 1, 127u);
    adc[2]  = ceiling(255999 / (52346879 / g + 1) + 1, 127u);
    adc[3]  = ceiling(255999 / (55926783 / g + 1) + 1, 127u);
    adc[4]  = 42u;
    adc[5]  = ceiling(positive((1994997759 / g - 128000) / 4000 + 1), 127u);
    adc[6]  = ceiling(255999 / positive((int32_t)((uint32_t)(161939455 / g) & 0xfffffu) - 7999) + 1, 127u);
    adc[7]  = ceiling(255999 / positive((int32_t)((uint32_t)(138575871 / g) & 0xfffffu) - 3999) + 1, 127u);
    adc[8]  = ceiling(255999 / positive((int32_t)((uint32_t)(238493695 / g) & 0xfffffu) - 31999) + 1, 127u);
    adc[9]  = 10u;
    adc[10] = ceiling(127999 / positive(32767999 / g - 1999) + 1, 127u);

    const reg_op_t adc_ops[] = {
        {OP_RAD_MOD, 0x9cu, 0x0008u, 0x0000u}, {OP_RAD_MOD, 0x9cu, 0x0004u, 0x0000u},
        {OP_RAD_MOD, 0x1fu, 0x00e0u, 0x0080u}, {OP_RAD_MOD, 0x26u, 0x0800u, 0x0000u},
        {OP_RAD_MOD, 0x24u, 0x00ffu, 0x0010u}, {OP_RAD_MOD, 0x24u, 0xff00u, 0x0800u},
        {OP_RAD_MOD, 0x25u, 0x00ffu, 0x0008u}, {OP_RAD_MOD, 0x25u, 0xff00u, 0x0800u},
        {OP_RAD_MOD, 0x26u, 0x0300u, 0x0100u}, {OP_RAD_MOD, 0x26u, 0x00ffu, 0x0040u},
        {OP_RAD_MOD, 0x20u, 0x007fu, adc[0]},  {OP_RAD_MOD, 0x21u, 0x007fu, adc[1]},
        {OP_RAD_MOD, 0x21u, 0x7f00u, (uint16_t)(adc[2] << 8u)}, {OP_RAD_MOD, 0x22u, 0x007fu, adc[3]},
        {OP_RAD_MOD, 0x28u, 0x007fu, adc[6]},  {OP_RAD_MOD, 0x28u, 0x7f00u, (uint16_t)(adc[7] << 8u)},
        {OP_RAD_MOD, 0x29u, 0x007fu, adc[8]},  {OP_RAD_MOD, 0x29u, 0xff00u, (uint16_t)(adc[9] << 8u)},
        {OP_RAD_MOD, 0x23u, 0x7f00u, (uint16_t)(adc[4] << 8u)}, {OP_RAD_MOD, 0x23u, 0x007fu, adc[5]},
        {OP_RAD_MOD, 0x22u, 0x7f00u, (uint16_t)(adc[10] << 8u)},
        {OP_RAD_MOD, 0x2au, 0x1f00u, 0x0200u}, {OP_RAD_MOD, 0x2au, 0x6000u, 0x2000u},
        {OP_RAD_MOD, 0x9cu, 0x0010u, 0x0010u}, {OP_RAD_MOD, 0x1fu, 0x0004u, 0x0004u},
        {OP_RAD_MOD, 0x1fu, 0x0004u, 0x0000u}, {OP_RAD_MOD, 0x9cu, 0x0010u, 0x0000u}
    };
    run_ops(adc_ops, sizeof(adc_ops) / sizeof(adc_ops[0]));

    /* 5. DAC, TIA (108 registers 0xa9..0x114), and preferred Radio 20691 settings */
    static const reg_op_t dac_ops[] = {
        {OP_RAD_MOD, 0x08u, 0x00f0u, 0x0040u}, {OP_RAD_MOD, 0x08u, 0x000eu, 0x0008u},
        {OP_RAD_MOD, 0x6fu, 0x0200u, 0x0200u}, {OP_RAD_MOD, 0x6fu, 0x0c00u, 0x0000u},
        {OP_RAD_MOD, 0x04u, 0x0002u, 0x0002u}, {OP_RAD_MOD, 0x6cu, 0x0030u, 0x0010u}
    };
    run_ops(dac_ops, sizeof(dac_ops) / sizeof(dac_ops[0]));

    for (uint32_t i = 0u; i < 108u; ++i) {
        uint16_t val = (i < 52u) ? (uint16_t)rom_board_packed[112u + i]
                                 : (uint16_t)get_le(rom_board_packed + 2u * (i - 52u), 2u);
        radio_reg_write((uint16_t)(0xa9u + i), val);
    }

    static const reg_op_t pref_ops[] = {
        {OP_RAD_MOD, 0x89u, 0x0300u, 0x0300u},
        {OP_RAD_MOD, 0x18u, 0x0007u, 0x0000u}, {OP_RAD_MOD, 0x18u, 0x0070u, 0x0000u},
        {OP_RAD_MOD, 0x17u, 0x00e0u, 0x0040u}, {OP_RAD_MOD, 0x17u, 0x0700u, 0x0700u},
        {OP_RAD_MOD, 0x8du, 0x00f0u, 0x0010u},
        {OP_RAD_MOD, 0x79u, 0x0020u, 0x0000u}, {OP_RAD_MOD, 0x79u, 0x0010u, 0x0000u},
        {OP_RAD_MOD, 0x79u, 0x0008u, 0x0000u},
        {OP_RAD_MOD, 0x7au, 0xff00u, 0x3000u}, {OP_RAD_MOD, 0x7au, 0x00f0u, 0x00f0u},
        {OP_RAD_MOD, 0x7au, 0x000fu, 0x0000u},
        {OP_RAD_MOD, 0x7cu, 0x003fu, 0x0019u}, {OP_RAD_MOD, 0x7cu, 0x3f00u, 0x1900u},
        {OP_RAD_MOD, 0x7du, 0x0780u, 0x0000u}, {OP_RAD_MOD, 0x7du, 0x003fu, 0x0021u},
        {OP_RAD_MOD, 0x83u, 0x003eu, 0x002au},
        {OP_RAD_MOD, 0x87u, 0xf000u, 0x0000u}, {OP_RAD_MOD, 0x87u, 0x0fc0u, 0x0a00u},
        {OP_RAD_MOD, 0x87u, 0x003fu, 0x0016u},
        {OP_RAD_MOD, 0x7bu, 0x0700u, 0x0400u}, {OP_RAD_MOD, 0x7bu, 0x0070u, 0x0040u},
        {OP_RAD_MOD, 0x7bu, 0x0003u, 0x0001u}, {OP_RAD_MOD, 0x1eu, 0x0080u, 0x0000u}
    };
    run_ops(pref_ops, sizeof(pref_ops) / sizeof(pref_ops[0]));
    return 1;
}

static int radio_program_channel_and_vco(uint32_t channel, uint32_t *vco_status_out) {
    if (channel < 1u || channel > 14u) return 0;
    const uint16_t *words = radio20691_2g_channels[channel - 1u];
    for (uint32_t i = 0u; i < 31u; ++i)
        radio_reg_write(channel_reg_indices[i], words[i]);

    radio_reg_mod(0x3du, 0x1000u, 0x0000u);
    radio_reg_mod(0x71u, 0x1000u, 0x0000u);
    radio_reg_mod(0x72u, 0x2000u, 0x0000u);

    static const reg_op_t vco_trigger[] = {
        {OP_RAD_OR,  0x8au, 0x0000u, 0x0100u},
        {OP_RAD_MOD, 0x1bu, 0x0700u, 0x0000u},
        {OP_RAD_MOD, 0x51u, 0x0001u, 0x0000u},
        {OP_DELAY,   1u,    0u,      0u},
        {OP_RAD_MOD, 0x51u, 0x0001u, 0x0001u},
        {OP_RAD_MOD, 0xa0u, 0x0100u, 0x0100u},
        {OP_RAD_MOD, 0x42u, 0x0010u, 0x0000u},
        {OP_RAD_MOD, 0xa0u, 0x0200u, 0x0200u},
        {OP_RAD_MOD, 0x42u, 0x0020u, 0x0000u},
        {OP_DELAY,   1u,    0u,      0u},
        {OP_RAD_MOD, 0x42u, 0x0010u, 0x0010u},
        {OP_RAD_MOD, 0x42u, 0x0020u, 0x0020u},
        {OP_DELAY,   1u,    0u,      0u},
        {OP_RAD_MOD, 0x42u, 0x0080u, 0x0080u},
        {OP_RAD_MOD, 0x42u, 0x0040u, 0x0040u},
        {OP_DELAY,   1u,    0u,      0u},
        {OP_RAD_MOD, 0x42u, 0x0080u, 0x0000u},
        {OP_RAD_MOD, 0x42u, 0x0040u, 0x0000u},
        {OP_DELAY,   50u,   0u,      0u}
    };
    run_ops(vco_trigger, sizeof(vco_trigger) / sizeof(vco_trigger[0]));

    uint32_t locked = 0u;
    for (uint32_t i = 0u; i < 100u; ++i) {
        (void)delay_ticks(10u * 180u);
        uint16_t st = radio_reg_read(0x65u);
        if (st != 0xffffu && (st & 1u) != 0u) {
            if (vco_status_out) *vco_status_out = st;
            locked = 1u;
            break;
        }
    }
    if (!locked) return 0;
    (void)delay_ticks(100u * 180u);
    radio_reg_write(0x8au, (uint16_t)(radio_reg_read(0x8au) & 0xfeffu));
    return 1;
}

static uint32_t gcd_u32(uint32_t a, uint32_t b) {
    while (b != 0u) {
        uint32_t next = a % b;
        a = b;
        b = next;
    }
    return a;
}

static void phy_apply_channel_timing(uint32_t channel) {
    uint32_t freq = (channel == 14u) ? 2484u : (2407u + 5u * channel);
    uint32_t mu = (freq * 65536u + 2u) / 5u;
    uint32_t period = 1280u / gcd_u32(1280u, freq);
    phy_reg_mod(0x9adu, 0xffffu, (uint16_t)(mu & 0xffffu));
    phy_reg_mod(0x9aeu, 0x03ffu, (uint16_t)(mu >> 16u));
    phy_reg_mod(0x9aeu, 0x4000u, 0x0000u);
    phy_reg_mod(0x9b1u, 0x1fffu, (uint16_t)period);
    phy_reg_mod(0x9acu, 0x0400u, 0x0400u);
    phy_reg_mod(0x657u, 0x0fffu, (uint16_t)(((freq * 4u / 5u) + 1u) / 2u));
    phy_reg_mod(0x658u, 0x0fffu, (uint16_t)(((5242880u / freq) + 1u) / 2u));
}

static void phy_configure_rx_path(uint32_t channel) {
    static const uint32_t rfseq_init[19] = {10u,5u,8u,1600u,8u,5u,8u,8u,119u,8u,138u,8u,1u,5u,1u,10u,1u,5u,1600u};
    static const uint32_t rfseq_turn[3]  = {60u,1u,8u};
    static const uint32_t zero12[12]     = {0u};
    static const uint32_t gmap[8]        = {0x28u,0x128u,0x138u,0x148u,0x158u,0x155u,0x150u,0x350u};

    phy_apply_channel_timing(channel);

    phy_reg_write(0x9cfu, 0x0000u);
    phy_table_write(8u, 3u, 16u, rfseq_init, 19u);
    phy_table_write(8u, 29u, 16u, rfseq_turn, 3u);

    static const reg_op_t power_and_dcoe[] = {
        {OP_PHY_MOD, 0x53bu, 0x0100u, 0x0100u}, {OP_PHY_MOD, 0x53bu, 0x0200u, 0x0200u},
        {OP_PHY_MOD, 0x53bu, 0x0400u, 0x0400u}, {OP_PHY_MOD, 0x4b1u, 0x0004u, 0x0004u},
        {OP_RAD_MOD, 0x19u,  0x0001u, 0x0001u}, {OP_RAD_MOD, 0x9bu,  0x0002u, 0x0002u},
        {OP_RAD_MOD, 0x1cu,  0x0001u, 0x0001u}, {OP_RAD_MOD, 0x17u,  0x4000u, 0x4000u},
        {OP_RAD_MOD, 0x9au,  0x0001u, 0x0001u}, {OP_RAD_MOD, 0x3du,  0x4000u, 0x4000u},
        {OP_RAD_MOD, 0x9fu,  0x0020u, 0x0020u}, {OP_RAD_MOD, 0x3eu,  0x0010u, 0x0000u},
        {OP_RAD_MOD, 0x9fu,  0x0002u, 0x0002u}, {OP_RAD_MOD, 0x3eu,  0x0020u, 0x0020u},
        {OP_RAD_MOD, 0x9fu,  0x0004u, 0x0004u}, {OP_RAD_MOD, 0x26u,  0x1000u, 0x1000u},
        {OP_RAD_MOD, 0x9cu,  0x0004u, 0x0004u}, {OP_PHY_MOD, 0x4b1u, 0x0004u, 0x0004u},
        {OP_PHY_MOD, 0x9a5u, 0x0040u, 0x0040u}, {OP_PHY_MOD, 0x463u, 0xff00u, 0x7200u},
        {OP_PHY_MOD, 0x976u, 0x0fffu, 0x0020u}, {OP_PHY_MOD, 0x966u, 0xfff7u, 0x8985u},
        {OP_PHY_MOD, 0x967u, 0x00ffu, 0x0019u}
    };
    run_ops(power_and_dcoe, sizeof(power_and_dcoe) / sizeof(power_and_dcoe[0]));
    phy_table_write(34u, 0u, 32u, zero12, 12u);

    static const reg_op_t idacc[] = {
        {OP_PHY_MOD, 0x96eu, 0x0fffu, 0x0000u}, {OP_PHY_MOD, 0x965u, 0x0004u, 0x0004u},
        {OP_PHY_MOD, 0x968u, 0xfff7u, 0x5d86u}, {OP_PHY_MOD, 0x967u, 0xff00u, 0x2800u},
        {OP_PHY_MOD, 0x969u, 0x00ffu, 0x0001u}
    };
    run_ops(idacc, sizeof(idacc) / sizeof(idacc[0]));
    phy_table_write(36u, 0u, 16u, gmap, 8u);
    phy_table_write(35u, 0u, 32u, zero12, 8u);

    static const reg_op_t dc_and_reset_tail[] = {
        {OP_PHY_MOD, 0x971u, 0x00ffu, 0x0001u}, {OP_PHY_MOD, 0x965u, 0x0008u, 0x0008u},
        {OP_PHY_MOD, 0x4b1u, 0x0001u, 0x0000u}, {OP_PHY_MOD, 0x4b0u, 0x0007u, 0x0000u},
        {OP_RAD_MOD, 0x32u,  0x007eu, 0x0004u}, {OP_PHY_MOD, 0x96bu, 0xff00u, 0x2800u},
        {OP_PHY_MOD, 0x96du, 0x003fu, 0x0018u},
        {OP_PHY_WR,  0xa00u, 0x0000u, 0x0000u}, {OP_PHY_MOD, 0x440u, 0x0001u, 0x0000u},
        {OP_PHY_OR,  0x44au, 0x0000u, 0x0044u}, {OP_PHY_WR,  0x44au, 0x0000u, 0x0080u},
        {OP_PHY_WR,  0xa00u, 0x0000u, 0x00ffu}, {OP_PHY_MOD, 0x440u, 0x0001u, 0x0001u},
        {OP_RAD_MOD, 0x30u,  0x003fu, 0x0008u}, {OP_RAD_MOD, 0x31u,  0x007eu, 0x0010u},
        {OP_RAD_MOD, 0x39u,  0x000fu, 0x0000u}
    };
    run_ops(dc_and_reset_tail, sizeof(dc_and_reset_tail) / sizeof(dc_and_reset_tail[0]));
}

static int phy_run_dccal(uint32_t *dc_idac_out, uint32_t *dc_dcoe_out) {
    phy_reg_mod(0x965u, 0x0004u, 0x0004u);
    phy_reg_mod(0x965u, 0x0008u, 0x0008u);
    phy_reg_mod(0x975u, 0x004fu, 0x0009u);
    phy_reg_mod(0x969u, 0x3800u, 0x0000u);
    phy_reg_mod(0x440u, 0x0004u, 0x0004u);

    uint32_t ok = 0u;
    for (uint32_t p = 0u; p < 10u; ++p) {
        phy_reg_mod(0x44au, 0x0001u, 0x0001u);
        phy_reg_mod(0x44au, 0x0001u, 0x0000u);
        (void)delay_ticks(32u * 180u);
        uint16_t idac = phy_reg_read(0x978u);
        if ((idac & 0x00ffu) == 0x00ffu) {
            uint16_t dcoe = phy_reg_read(0x96fu);
            if ((dcoe & 0x0fffu) == 0x0fffu) {
                if (dc_idac_out) *dc_idac_out = idac;
                if (dc_dcoe_out) *dc_dcoe_out = dcoe;
                ok = 1u;
                break;
            }
        }
    }
    phy_reg_mod(0x440u, 0x0004u, 0x0000u);
    phy_reg_mod(0x975u, 0x004fu, 0x000fu);
    return (int)ok;
}

static void phy_configure_agc_and_finalize(void) {
    static const reg_op_t agc_ops[] = {
        {OP_RAD_MOD,0x36u,0x7000u,0u},{OP_RAD_MOD,0x36u,0x700u,0x400u},{OP_RAD_MOD,0x36u,0x70u,0x40u},
        {OP_RAD_MOD,0x36u,7u,0u},{OP_RAD_MOD,0x35u,0x7000u,0u},{OP_RAD_MOD,0x35u,0x700u,0x700u},
        {OP_RAD_MOD,0x35u,0x8000u,0u},{OP_RAD_MOD,0x35u,0x7eu,0x20u},{OP_RAD_MOD,0x3au,0x1eu,0x10u},
        {OP_RAD_MOD,0x3au,1u,0u},{OP_PHY_MOD,0x50au,1u,0u},{OP_PHY_MOD,0x471u,0xfu,4u},
        {OP_PHY_MOD,0x471u,0xf0u,0u},{OP_PHY_MOD,0x41fu,0x7fu,0x41u},{OP_PHY_MOD,0x420u,0xffu,0xcu},
        {OP_PHY_MOD,0x5e0u,0xffu,0u},{OP_PHY_MOD,0x5e0u,0xff00u,0x3000u},{OP_PHY_MOD,0x5e1u,0xffu,0x27u},
        {OP_PHY_MOD,0x5e1u,0xff00u,0x2700u},{OP_PHY_MOD,0x5e2u,0xffu,0x27u},{OP_PHY_MOD,0x5e2u,0xff00u,0x1e00u},
        {OP_PHY_MOD,0x5e3u,0xffu,0x1eu},{OP_PHY_MOD,0x5e3u,0xff00u,0x1e00u},{OP_PHY_MOD,0x5e4u,0xffu,0x1eu},
        {OP_PHY_MOD,0xa96u,0xffu,0x15u},{OP_PHY_MOD,0xa96u,0xff00u,0xc00u},{OP_PHY_MOD,0xa97u,0xffu,0u},
        {OP_PHY_MOD,0x5e5u,0x7fu,0u},{OP_PHY_MOD,0x5e5u,0x3f80u,0x780u},{OP_PHY_MOD,0x5e6u,0x7fu,0x15u},
        {OP_PHY_MOD,0x5e6u,0x3f80u,0u},{OP_PHY_MOD,0x5e7u,0x7fu,0u},{OP_PHY_MOD,0x5e7u,0x3f80u,0x980u},
        {OP_PHY_MOD,0x5e8u,0x7fu,0u},{OP_PHY_MOD,0x5e8u,0x3f80u,0u},{OP_PHY_MOD,0x5e9u,0x7fu,0u},
        {OP_PHY_MOD,0x5e9u,0x3f80u,0x900u},{OP_PHY_MOD,0xa91u,0x7fu,0x16u},{OP_PHY_MOD,0xa91u,0x3f80u,0xc80u},
        {OP_PHY_MOD,0x5fau,0xffu,0x36u},{OP_PHY_MOD,0x5f5u,0xffu,0u},{OP_PHY_MOD,0x5f5u,0xff00u,0x2d00u},
        {OP_PHY_MOD,0x5f6u,0xffu,0x24u},{OP_PHY_MOD,0x5f6u,0xff00u,0x2400u},{OP_PHY_MOD,0x5f7u,0xffu,0x24u},
        {OP_PHY_MOD,0x5f7u,0xff00u,0x2400u},{OP_PHY_MOD,0x5f8u,0xffu,0x24u},{OP_PHY_MOD,0x5f8u,0xff00u,0x2400u},
        {OP_PHY_MOD,0x5f9u,0xffu,0x24u},{OP_PHY_MOD,0xa8fu,0xffu,0x1bu},{OP_PHY_MOD,0xa8fu,0xff00u,0x1200u},
        {OP_PHY_MOD,0xa90u,0xffu,6u},{OP_PHY_MOD,0x5ffu,0x3f80u,0u},{OP_PHY_MOD,0x5d1u,0x7fu,0x28u},
        {OP_PHY_MOD,0x5d1u,0x3f80u,0x1100u},{OP_PHY_MOD,0x5d2u,0x7fu,0u},{OP_PHY_MOD,0x5d2u,0x3f80u,0u},
        {OP_PHY_MOD,0x5d3u,0x7fu,0u},{OP_PHY_MOD,0x5d3u,0x3f80u,0u},{OP_PHY_MOD,0x5d4u,0x7fu,0u},
        {OP_PHY_MOD,0x5d4u,0x3f80u,0u},{OP_PHY_MOD,0xa94u,0x7fu,5u},{OP_PHY_MOD,0xa94u,0x3f80u,0x500u},
        {OP_PHY_MOD,0xa95u,0x7fu,0x12u},{OP_PHY_MOD,0x5f9u,0xff00u,0x3000u},{OP_PHY_MOD,0x5edu,0xff00u,0xfa00u},
        {OP_PHY_MOD,0x5e4u,0xf00u,0x100u},{OP_PHY_MOD,0x565u,0x18u,0x10u},{OP_PHY_MOD,0x030u,7u,7u},
        {OP_PHY_MOD,0x471u,0xff00u,0x4b00u},{OP_PHY_MOD,0x5eau,0x3fu,0xfffau},{OP_PHY_MOD,0x5eau,0xff00u,0xf700u},
        {OP_PHY_MOD,0x437u,0xffu,0u},{OP_PHY_MOD,0x429u,0x7c00u,0x4800u},{OP_PHY_MOD,0x472u,0xffu,0x51u},
        {OP_PHY_MOD,0x542u,0xff00u,0x5400u},{OP_PHY_MOD,0x534u,0xffu,0x17u},{OP_PHY_MOD,0x5d5u,0xffu,0u},
        {OP_PHY_MOD,0x550u,0x3ffu,0x169u},{OP_PHY_MOD,0xa52u,0x1fe0u,0x40u},{OP_PHY_MOD,0xa53u,0xffu,2u},
        {OP_PHY_MOD,0xa58u,0xfu,1u},{OP_PHY_MOD,0x542u,0xffu,0x2eu},{OP_PHY_MOD,0x441u,0xff00u,0u},
        {OP_PHY_MOD,0x442u,0xffu,0x62u},{OP_PHY_MOD,0x5d5u,0xff00u,0x5f00u},{OP_PHY_MOD,0xa28u,0xff00u,0x5f00u},
        {OP_PHY_MOD,0xa28u,0xffu,0x5fu},{OP_PHY_MOD,0xa29u,0xff00u,0x5f00u},{OP_PHY_MOD,0xa29u,0xffu,0x5fu},
        {OP_PHY_MOD,0xa55u,0xffu,0x5eu},
        {OP_PHY_MOD,0xa4fu,0xffu,91u},
        {OP_PHY_MOD,0xa57u,0xff00u,0x6100u},{OP_PHY_MOD,0xa57u,0xffu,97u},
        {OP_PHY_MOD,0x788u,0x80u,0x80u},{OP_PHY_MOD,0x788u,2u,0u},
        {OP_PHY_MOD,0x787u,0xff00u,187u<<8u},{OP_PHY_MOD,0x432u,0xff00u,181u<<8u},
        {OP_PHY_MOD,0xa57u,0xffu,97u},{OP_PHY_MOD,0xa57u,0xff00u,0x6100u},
        {OP_PHY_MOD,0xb00u,1u,0u},{OP_PHY_MOD,0xb00u,2u,0u},{OP_PHY_MOD,0xb00u,4u,0u},
        {OP_PHY_MOD,0xb00u,8u,0u},{OP_PHY_MOD,0xb00u,0xf0u,0xf0u},{OP_PHY_MOD,0xb00u,0xf00u,0xf00u},
        {OP_PHY_MOD,0xb00u,0x1000u,0u},{OP_PHY_MOD,0xb07u,1u,0u},{OP_PHY_MOD,0xb07u,6u,4u},
        {OP_PHY_MOD,0xb07u,0x38u,8u},{OP_PHY_MOD,0xb07u,0x1c0u,0u},{OP_PHY_MOD,0xb0bu,0x30u,0x10u},
        {OP_PHY_MOD,0xb0bu,0x40u,0x40u},{OP_PHY_MOD,0xb0bu,0x780u,0x400u},{OP_PHY_MOD,0xb0bu,0x7800u,0x2000u},
        {OP_PHY_MOD,0xb0cu,0xffffu,0x1000u},{OP_PHY_MOD,0xb0du,0xffffu,0xd000u},{OP_PHY_MOD,0xb0eu,0xffffu,0xf800u},
        {OP_PHY_MOD,0xb0fu,0xffffu,0x10u},{OP_PHY_MOD,0xb10u,0xffffu,0xf800u},{OP_PHY_MOD,0xb11u,0xffffu,0xfd00u},
        {OP_PHY_MOD,0xb12u,0xfu,0u},{OP_PHY_MOD,0xb12u,0xf0u,0u},{OP_PHY_MOD,0xb12u,0xf00u,0u},
        {OP_PHY_MOD,0xb13u,0xfu,0u},{OP_PHY_MOD,0xb13u,0xf0u,0u},{OP_PHY_MOD,0xb13u,0xf00u,0u},
        {OP_PHY_MOD,0xb14u,0xfu,0u},{OP_PHY_MOD,0xb14u,0xf0u,0u},{OP_PHY_MOD,0xb14u,0xf00u,0u},
        {OP_PHY_MOD,0xb15u,0xfu,0u},{OP_PHY_MOD,0xb15u,0xf0u,0u},{OP_PHY_MOD,0xb15u,0xf00u,0u},
        {OP_PHY_MOD,0xb16u,3u,1u},{OP_PHY_MOD,0xb16u,0xcu,0u},{OP_PHY_MOD,0xb16u,0x30u,0x10u},
        {OP_PHY_MOD,0xb16u,0xc0u,0x40u},{OP_PHY_MOD,0xb16u,0x300u,0u},{OP_PHY_MOD,0xb16u,0xc00u,0x400u},
        {OP_PHY_MOD,0xb17u,3u,0u},{OP_PHY_MOD,0xb17u,0xcu,0xfffcu},{OP_PHY_MOD,0xb17u,0x30u,0xfff0u},
        {OP_PHY_MOD,0xb17u,0xc0u,0u},{OP_PHY_MOD,0xb17u,0x300u,0xff00u},{OP_PHY_MOD,0xb17u,0xc00u,0xfc00u},
        {OP_PHY_MOD,0xb06u,1u,0u},{OP_PHY_MOD,0xb05u,0x1fu,0x1fu},{OP_PHY_MOD,0xb04u,0xffffu,0xffffu},
        {OP_PHY_MOD,0xb08u,0xffffu,0xffffu},{OP_PHY_MOD,0xb09u,0xffffu,0x7fffu},{OP_PHY_MOD,0xb0au,0xffffu,8u},
        {OP_PHY_MOD,0xb02u,0xffffu,0x3fu},{OP_PHY_MOD,0xb03u,0xffffu,0xffu},{OP_PHY_MOD,0xb01u,0xfu,0xfu},
        {OP_PHY_MOD,0xb01u,0xf0u,0xf0u},{OP_PHY_MOD,0xb00u,1u,1u},
        {OP_PHY_WR, 0xa00u,0u,0u},{OP_PHY_MOD,0x440u,1u,0u},
        {OP_PHY_OR, 0x44au,0u,0x44u},{OP_PHY_WR, 0x44au,0u,0x80u},
        {OP_PHY_WR, 0xa00u,0u,0xffu},{OP_PHY_MOD,0x440u,1u,1u},
        /* Undeaf + Finalize */
        {OP_PHY_MOD,0x440u,1u,1u},
        {OP_PHY_MOD,0x440u,0x200u,0x200u},{OP_PHY_MOD,0x805u,0x10u,0x10u},
        {OP_PHY_MOD,0x304u,1u,1u},{OP_PHY_MOD,0x315u,0xfu,0xau},
        {OP_PHY_MOD,0x327u,0xfffu,0xfffu}
    };
    run_ops(agc_ops, sizeof(agc_ops) / sizeof(agc_ops[0]));
}

int phy_tune_channel_2g(uint32_t channel, uint32_t *vco_status_out, uint32_t *dc_idac_out, uint32_t *dc_dcoe_out) {
    if (channel < 1u || channel > 14u) return 0;
    phy_reg_mod(0x440u, 0x0001u, 0x0000u); /* Set PHY deaf */
    if (!radio_program_channel_and_vco(channel, vco_status_out)) return 0;
    phy_configure_rx_path(channel);
    if (!phy_run_dccal(dc_idac_out, dc_dcoe_out)) return 0;
    phy_configure_agc_and_finalize();
    enforce_tx_inhibit_guards();
    return 1;
}

int phy_fast_switch_channel_2g(uint32_t channel, uint32_t *vco_status_out) {
    if (channel < 1u || channel > 14u) return 0;
    phy_reg_mod(0x440u, 0x0001u, 0x0000u); /* Set PHY deaf */
    if (!radio_program_channel_and_vco(channel, vco_status_out)) return 0;
    phy_apply_channel_timing(channel);
    (void)phy_run_dccal(0, 0);
    phy_reg_mod(0x440u, 0x0001u, 0x0001u); /* Undeaf PHY */
    enforce_tx_inhibit_guards();
    return 1;
}

static int tx_hw_configured;

static void phy_tx_farrow_init(uint32_t channel) {
    if (channel < 1u || channel > 14u) channel = 1u;
    static const uint32_t mu_delta_lut[14] = {
        6677499u, 6663685u, 6649929u, 6636229u, 6622585u, 6608998u, 6595466u,
        6581989u, 6568567u, 6555200u, 6541888u, 6528629u, 6515424u, 6483948u
    };
    uint32_t freq = (channel == 14u) ? 2484u : (2407u + 5u * channel);
    uint32_t mu_delta = mu_delta_lut[channel - 1u];
    uint16_t samp_sync = (uint16_t)(freq / gcd_u32(freq, 1920u));
    phy_reg_mod(0x987u, 0x0002u, 0x0002u);
    phy_reg_write(0x986u, samp_sync);
    phy_reg_mod(0x980u, 0x00ffu, (uint16_t)((mu_delta >> 16u) & 0xffu));
    phy_reg_write(0x981u, (uint16_t)(mu_delta & 0xffffu));
    phy_reg_mod(0x984u, 0x00ffu, (uint16_t)((mu_delta >> 16u) & 0xffu));
    phy_reg_write(0x985u, (uint16_t)(mu_delta & 0xffffu));
}

static void phy_tx_iir_filter_init(void) {
    static const uint16_t cck_iir[][2] = {
        {0x910u, 0u}, {0x91eu, 20u}, {0x91fu, (uint16_t)-92}, {0x924u, 301u},
        {0x925u, 301u}, {0x926u, 0u}, {0x920u, 378u}, {0x921u, (uint16_t)-184},
        {0x927u, 64u}, {0x928u, 128u}, {0x929u, 64u}, {0x922u, 504u},
        {0x923u, (uint16_t)-128}, {0x930u, 64u}, {0x931u, 128u}, {0x932u, 64u},
        {0x912u, 8u}
    };
    static const uint16_t ofdm_iir[][2] = {
        {0x90fu, 0u}, {0x900u, 0u}, {0x901u, 0u}, {0x906u, 256u},
        {0x907u, 256u}, {0x908u, 0u}, {0x902u, 0u}, {0x903u, 0u},
        {0x909u, 511u}, {0x90au, 0u}, {0x90bu, 0u}, {0x904u, 0u},
        {0x905u, 0u}, {0x90cu, 292u}, {0x90du, 292u}, {0x90eu, 0u},
        {0x911u, 9u}
    };
    for (uint32_t i = 0u; i < sizeof(cck_iir) / sizeof(cck_iir[0]); ++i)
        phy_reg_write(cck_iir[i][0], cck_iir[i][1]);
    for (uint32_t i = 0u; i < sizeof(ofdm_iir) / sizeof(ofdm_iir[0]); ++i)
        phy_reg_write(ofdm_iir[i][0], ofdm_iir[i][1]);

    phy_reg_mod(0x6dau, 0x0080u, 0x0080u);
    phy_reg_mod(0x6d3u, 0x0080u, 0x0080u);
    phy_reg_mod(0x6d3u, 0x0080u, 0x0000u);
    phy_reg_mod(0x6dau, 0x0080u, 0x0000u);
}

void phy_tx_set_inhibit(int inhibit) {
    if (inhibit) {
        enforce_tx_inhibit_guards();
    } else {
        phy_reg_mod(0x44du, 0x4000u, 0x4000u);
        phy_reg_mod(0x44cu, 0x4000u, 0x4000u);
        phy_reg_mod(0x939u, 0x0800u, 0x0800u);
        phy_reg_mod(0x938u, 0x0800u, 0x0800u);
        radio_reg_mod(0x79u, 0x7f87u, 0x7f87u);
        radio_reg_mod(0xa4u, 0x005fu, 0x0004u);
    }
}

void phy_tx_set_automatic(void) {
    /* Release only the TX overrides owned by phy_tx_set_inhibit. D11/PHY
     * sequencing, rather than a forced-on transmitter, owns RX/TX turnaround. */
    radio_reg_mod(0x79u, 0x0007u, 0u);
    radio_reg_mod(0xa4u, 0x0004u, 0u);
    phy_reg_mod(0x44cu, 0x4000u, 0u);
    phy_reg_mod(0x938u, 0x0800u, 0u);
}

void phy_tx_hw_configure(uint32_t channel) {
    mmio_write32(GCI_CONTROL_REG1, 0x00f0ff08u);
    mmio_write32(GCI_OUTPUT_REG1, (mmio_read32(GCI_OUTPUT_REG1) & ~0x00f00000u) | 0x00100000u);
    (void)mmio_read32(GCI_OUTPUT_REG1);

    if (!tx_hw_configured) {
        static const uint16_t tx_rad_pairs[][2] = {
            {0x03u, 0x0092u}, {0x0cu, 0x0001u}, {0x34u, 0x271cu}, {0x38u, 0x00a2u},
            {0x66u, 0x0a68u}, {0x7eu, 0x610bu}, {0x81u, 0x006eu},
            {0x0au, 0x0000u}, {0x0bu, 0x0000u}, {0xa3u, 0x201fu},
            {0x72u, 0x0963u}, {0x73u, 0x292cu}, {0x74u, 0x05afu},
            {0x75u, 0x0073u}, {0x76u, 0x0738u}, {0x77u, 0x0060u}
        };
        for (uint32_t i = 0u; i < sizeof(tx_rad_pairs) / sizeof(tx_rad_pairs[0]); ++i)
            radio_reg_write(tx_rad_pairs[i][0], tx_rad_pairs[i][1]);

        static const reg_op_t tx_ops[] = {
            {OP_RAD_MOD, 0x1du, 0x0020u, 0x0000u}, {OP_RAD_MOD, 0x68u, 0x00f4u, 0x0000u},
            {OP_RAD_MOD, 0x79u, 0x0020u, 0x0000u}, {OP_RAD_MOD, 0x6fu, 0x3000u, 0x1000u},
            {OP_RAD_MOD, 0x99u, 0x0001u, 0x0000u}, {OP_RAD_MOD, 0x96u, 0x0002u, 0x0000u},
            {OP_RAD_MOD, 0xa2u, 0x0001u, 0x0000u}, {OP_RAD_MOD, 0xa5u, 0x0040u, 0x0000u},
            {OP_RAD_MOD, 0xa6u, 0x0001u, 0x0000u}, {OP_RAD_MOD, 0xa1u, 0x0001u, 0x0000u},
            {OP_RAD_MOD, 0x02u, 0x0030u, 0x0000u},
            {OP_PHY_MOD, 0x631u, 0x0700u, 0x0100u}, {OP_PHY_MOD, 0x62au, 0x00ffu, 0x005au},
            {OP_PHY_WR,  0x848u, 0x0000u, 0x0000u}, {OP_PHY_MOD, 0x448u, 0x0300u, 0x0200u},
            {OP_PHY_MOD, 0x4d9u, 0x000eu, 0x0006u}, {OP_PHY_WR,  0x4d7u, 0x0000u, 0x210du},
            {OP_PHY_MOD, 0x6dau, 0x0040u, 0x0000u}, {OP_PHY_MOD, 0x6a3u, 0x0010u, 0x0010u},
            {OP_PHY_MOD, 0x637u, 0xf000u, 0x0000u}, {OP_PHY_MOD, 0x4d0u, 0x7fe4u, 0x0000u},
            {OP_PHY_MOD, 0x478u, 0x0003u, 0x0000u}, {OP_PHY_WR,  0x4a5u, 0x0000u, 0xa12cu},
            {OP_PHY_WR,  0x40du, 0x0000u, 0x4040u}, {OP_PHY_WR,  0x4a2u, 0x0000u, 0x4040u},
            {OP_PHY_WR,  0x4a6u, 0x0000u, 0x80ffu}, {OP_PHY_WR,  0x49au, 0x0000u, 0x00ffu},
            {OP_PHY_WR,  0x9f4u, 0x0000u, 0xa037u}, {OP_PHY_MOD, 0x499u, 0x03ffu, 0x0078u},
            {OP_PHY_WR,  0x49fu, 0x0000u, 0x4190u}, {OP_PHY_MOD, 0x9acu, 0x003cu, 0x0014u},
            {OP_PHY_MOD, 0x9f8u, 0x0004u, 0x0004u}, {OP_PHY_WR,  0x4dbu, 0x0000u, 0x07c8u},
            {OP_PHY_WR,  0x4dcu, 0x0000u, 0x07c8u}, {OP_PHY_WR,  0x40au, 0x0000u, 0x07c8u},
            {OP_PHY_WR,  0x40bu, 0x0000u, 0x30a4u}, {OP_PHY_WR,  0x40cu, 0x0000u, 0x30a4u},
            {OP_PHY_WR,  0x4b5u, 0x0000u, 0x0f06u}, {OP_PHY_WR,  0x4fbu, 0x0000u, 0xff22u},
            {OP_PHY_MOD, 0x439u, 0x0780u, 0x0380u}, {OP_PHY_MOD, 0x4b0u, 0x0080u, 0x0080u},
            {OP_PHY_MOD, 0x43au, 0x0040u, 0x0040u}, {OP_PHY_WR,  0x46eu, 0x0000u, 0x0000u},
            {OP_PHY_MOD, 0x4a4u, 0xe000u, 0x0000u}
        };
        run_ops(tx_ops, sizeof(tx_ops) / sizeof(tx_ops[0]));
        mmio_write16(0x180013e6u, 0x003du);
        mmio_write16(0x1800151eu, 0x3020u);
        mmio_write16(0x18001520u, 0x7f7bu);
        mmio_write16(0x18001526u, 0x1ec0u);
        mmio_write16(0x18001528u, 0x1ec0u);
        mmio_write16(0x1800152au, 0x1ec0u);
        mmio_write16(0x18001540u, 0x0500u);
        mmio_write16(0x18001554u, 0xafffu);
        mmio_write16(0x1800155eu, 0x1000u);

        static const uint32_t iq_ofdm[4] = {0x0400u, 0x0000u, 0x0000u, CLEAN_RX_TX_BBMULT};
        static const uint32_t iq_bphy[4] = {0x4000u, 0x0000u, 0x0000u, CLEAN_RX_TX_BBMULT};
        phy_table_write(0u, 96u,  16u, iq_ofdm, 4u);
        phy_table_write(0u, 112u, 16u, iq_bphy, 4u);
        phy_reg_mod(0x631u, 0x0014u, 0x0014u);
        phy_tx_iir_filter_init();
        tx_hw_configured = 1;
    }
    phy_tx_farrow_init(channel);
}

int d11_and_phy_cold_init(uint32_t *rc_lpf_out, uint32_t *rc_adc_out, uint32_t *rc_dacbuf_out,
                          uint32_t *vco_status_out, uint32_t *dc_idac_out, uint32_t *dc_dcoe_out) {
    if (!d11_release_core_and_phy()) return 0;

    load_rom_board_tables();

    /* Pulse radioReset (PHY 0x44a bit 8) and arm TX-inhibit guards */
    phy_reg_mod(0x44au, 0x0100u, 0x0100u);
    phy_reg_mod(0x44au, 0x0100u, 0x0000u);
    enforce_tx_inhibit_guards();

    /* Notify GCI and apply 2.4 GHz band PHY configuration */
    gci_radio_notify();
    phy_reg_mod(0x448u, 0x8000u, 0x8000u);
    phy_reg_mod(0x408u, 0x0100u, 0x0000u);

    /* Program PHY common tables (2, 1, 20, 23, 0), gain tables (17, 18, 13), and register defaults */
    program_phy_common_and_gain_tables();
    program_phy_defaults();

    /* Run Radio 20691 power-up, RC calibration, ADC tuning, DAC, and TIA programming */
    if (!radio_init_and_rc_adc_tia_cal(rc_lpf_out, rc_adc_out, rc_dacbuf_out)) return 0;

    /* Tune to initial Channel 1 (2412 MHz) and run VCO, DC, and AGC calibration */
    return phy_tune_channel_2g(1u, vco_status_out, dc_idac_out, dc_dcoe_out);
}

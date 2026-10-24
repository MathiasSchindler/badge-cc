static uint32_t tx_status(void) {
    return ((uint32_t)mmio_read16(0x1800150eu) << 16u) | mmio_read16(0x18001690u);
}

static int fail(uint32_t err, uint32_t addr, uint32_t val) {
    MBOX->fw_error = err;
    MBOX->fw_error_addr = addr;
    MBOX->fw_error_val = val;
    MBOX->fw_state = FW_STATE_ERROR;
    __asm__ volatile("dsb" ::: "memory");
    return 0;
}

/* SHM 16-bit word access via D11 object selector (0x18001160) and data port (0x18001164/0x18001166) */
static uint16_t shm_read16(uint32_t word_index) {
    mmio_write32(D11_OBJ_ADDR, 0x00010000u | (word_index >> 1u));
    (void)mmio_read32(D11_OBJ_ADDR);
    return mmio_read16(D11_OBJ_DATA + 2u * (word_index & 1u));
}

static void shm_write16(uint32_t word_index, uint16_t val) {
    mmio_write32(D11_OBJ_ADDR, 0x00010000u | (word_index >> 1u));
    (void)mmio_read32(D11_OBJ_ADDR);
    mmio_write16(D11_OBJ_DATA + 2u * (word_index & 1u), val);
}

static void sample_rxe8(volatile uint16_t out[8]) {
    static const uint32_t addrs[8] = {
        0x18001408u, 0x180014f0u, 0x18001412u, 0x18001418u,
        0x180014a0u, 0x180014b0u, 0x18001450u, 0x18001452u
    };
    for (uint32_t i = 0u; i < 8u; ++i)
        out[i] = mmio_read16(addrs[i]);
}

/* Configure D11 RXE, PSM external condition branch masks, RF switch / BTCX grant, IFS/TSF/NAV, and Radio LPF */
static void d11_configure_mac_and_rxe(uint32_t flags, uint16_t rc_lpf) {
    if (flags & CFG_RADIO_LPF) {
        radio_reg_mod(0xa3u, 0x0010u, 0x0010u);
        radio_reg_mod(0xa3u, 0x0008u, 0x0008u);
        radio_reg_mod(0xa3u, 0x0004u, 0x0004u);
        radio_reg_mod(0xa3u, 0x0001u, 0x0001u);
        radio_reg_mod(0xa3u, 0x0002u, 0x0002u);
        radio_reg_mod(0xa3u, 0x2000u, 0x2000u);
        uint16_t gmult = rc_lpf ? rc_lpf : 4880u;
        radio_reg_mod(0x72u, 0x1fffu, (uint16_t)(((uint32_t)2396u * gmult) >> 15u));
        radio_reg_mod(0x76u, 0x1fffu, (uint16_t)(((uint32_t)3058u * gmult) >> 15u));
        radio_reg_mod(0x73u, 0x1fffu, (uint16_t)(((uint32_t)3594u * gmult) >> 15u));
        radio_reg_mod(0x74u, 0x1fffu, (uint16_t)(((uint32_t)1797u * gmult) >> 15u));
        radio_reg_mod(0x75u, 0x1fffu, (uint16_t)(((uint32_t)2909u * gmult) >> 15u));
        radio_reg_mod(0x77u, 0x00ffu, 0x00aau);
        radio_reg_mod(0x71u, 0x0008u, 0x0000u);
        radio_reg_mod(0x72u, 0x2000u, 0x0000u);
    }
    if (flags & CFG_D11_RXE) {
        mmio_write16(0x18001406u, 0x0001u);
        mmio_write16(0x18001406u, 0x0000u);
        mmio_write16(0x1800140cu, 0x0018u);
        mmio_write16(0x18001406u, 0x0000u);
        mmio_write16(0x18001448u, 0x0c02u);
        mmio_write16(0x18001402u, CLEAN_SHM_FRAME);
        mmio_write16(0x18001450u, 0x4e01u);
        mmio_write16(0x18001452u, 0x015bu);
        mmio_write16(0x180014e4u, 0x0090u);
        mmio_write16(0x18001404u, CLEAN_RAW_BYTES);
        mmio_write16(0x180014feu, 0x0000u);
    }
    if (flags & CFG_D11_BRANCH_MASKS) {
        mmio_write16(0x18001490u, 0x0000u);
        mmio_write16(0x180014a0u, 0xf3f1u);
        mmio_write16(0x180014b0u, 0xfdefu);
        mmio_write16(0x180014a8u, 0xffffu);
        mmio_write16(0x180014a8u, 0x0000u);
        mmio_write16(0x180014a2u, 0x0001u);
        mmio_write16(0x180014b2u, 0xfffbu);
        mmio_write16(0x180014aau, 0xffffu);
        mmio_write16(0x180014aau, 0x0000u);
        mmio_write16(0x180014a4u, 0x1acfu);
        mmio_write16(0x180014acu, 0x0000u);
        mmio_write16(0x180014bcu, 0x0000u);
        mmio_write16(0x180014a6u, 0x02d7u);
        mmio_write16(0x180014b6u, 0xfdffu);
        mmio_write16(0x180014aeu, 0xffffu);
    }
    if (flags & CFG_D11_BTCX_RFSW) {
        mmio_write16(0x1800149eu, (uint16_t)(mmio_read16(0x1800149eu) | 0x00ffu));
        mmio_write16(0x1800149cu, (uint16_t)((mmio_read16(0x1800149cu) & ~0x00ffu) | 0x0004u));
        mmio_write16(0x180016b4u, (uint16_t)(mmio_read16(0x180016b4u) | 0x0003u));
        mmio_write16(0x180016b8u, (uint16_t)(mmio_read16(0x180016b8u) | 0x00c0u));
        mmio_write16(0x180016bau, 0x0010u);
        mmio_write16(0x180016bcu, 0x0064u);
        mmio_write16(0x180016f0u, 0x00c2u);
        mmio_write16(0x180016f2u, (uint16_t)(mmio_read16(0x180016f2u) | 0x00c0u));
    }
    if (flags & CFG_D11_IFS_TSF_CLK) {
        mmio_write32(D11_INT_CONTROL, 0x01000000u);
        mmio_write32(D11_MACCOMMAND, 0x00000004u);
        mmio_write32(D11_MACINTSTATUS, 0x00000000u);
        mmio_write32(D11_MACINTMASK, 0x00000000u);
        mmio_write16(D11_TSF_CFP_MAXDUR32, 0x01ddu);
        mmio_write16(D11_TSF_CFP_MAXDUR32 + 2u, 0x01ddu);
        mmio_write32(D11_CLK_CTL_ST, mmio_read32(D11_CLK_CTL_ST) | 0x00000002u);
        mmio_write16(0x180014d8u, (uint16_t)(mmio_read16(0x180014d8u) & ~0x0019u));
        mmio_write16(0x18001600u, 0x8004u);
        mmio_write16(0x18001610u, 0x010au);
        mmio_write16(0x18001612u, 0x0002u);
        mmio_write16(0x1800162eu, 0xcccdu);
        mmio_write16(0x18001630u, 0x000cu);
        mmio_write16(0x18001646u, 0xc000u);
        mmio_write16(0x1800164au, 0x0060u);
        mmio_write16(0x1800164eu, 0x0005u);
        mmio_write16(0x18001652u, 0x0060u);
        mmio_write16(0x18001656u, 0x0005u);
        mmio_write16(0x18001686u, 0x09d0u);
        mmio_write16(0x18001680u, 0x3e3eu);
        mmio_write16(0x18001682u, 0x023eu);
        mmio_write16(0x18001684u, 0x0212u);
        mmio_write16(0x18001696u, 0x0008u);
        mmio_write16(0x1800169au, 0x00e4u);
        for (uint32_t k = 0u; k < 4u; ++k) {
            mmio_write16(0x18001688u, (uint16_t)(k << 12u));
            mmio_write16(0x1800169cu, 0x0002u);
        }
        mmio_write16(0x18001688u, 0x0052u);
        mmio_write16(0x18001696u, 0x0206u);
        mmio_write16(0x1800169au, 0x0207u);
        mmio_write16(0x1800169cu, 0x0317u);
        mmio_write16(0x1800169eu, 0x0000u);
        mmio_write16(0x180016a8u, 0x0c3cu);
        mmio_write16(0x18001700u, 0x003cu);
        mmio_write16(0x180017c0u, 0x0083u);
        mmio_write16(0x180017d0u, 0x0003u);
    }
}


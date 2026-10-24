#if CLEAN_RX_HT_REARM_RESET
static uint16_t observer_tx_inhibited(void) {
    return (uint16_t)(
        (!(phy_reg_read(0x44du)&0x4000u) ? 1u : 0u) |
        ((phy_reg_read(0x44cu)&0x4000u) ? 2u : 0u) |
        (!(phy_reg_read(0x939u)&0x800u) ? 4u : 0u) |
        ((phy_reg_read(0x938u)&0x800u) ? 8u : 0u));
}

static void observer_reset_service(void) {
    if (shm_read16(CLEAN_OBSERVER_RESET_SHM) != 1u) return;
    uint32_t control = mmio_read32(D11_MACCONTROL);
    mmio_write32(D11_MACCONTROL,clean_mac_pause_psm(control));
    (void)mmio_read32(D11_MACCONTROL);
    if (shm_read16(CLEAN_OBSERVER_RESET_SHM) == 1u) {
        uint32_t start = mac_now();
        uint16_t count = shm_read16(CLEAN_OBSERVER_RESET_SHM+1u);
        uint16_t guard = observer_tx_inhibited();
        shm_write16(CLEAN_OBSERVER_RESET_SHM+2u,(uint16_t)control);
        shm_write16(CLEAN_OBSERVER_RESET_SHM+3u,(uint16_t)(control>>16u));
        shm_write16(CLEAN_OBSERVER_RESET_SHM+4u,phy_reg_read(0x440u));
        shm_write16(CLEAN_OBSERVER_RESET_SHM+5u,phy_reg_read(0x44au));
        shm_write16(CLEAN_OBSERVER_RESET_SHM+6u,phy_reg_read(0xa00u));
        shm_write16(CLEAN_OBSERVER_RESET_SHM+10u,guard);
        shm_write16(CLEAN_OBSERVER_RESET_SHM+14u,mmio_read16(D11_BASE+0x408u));
        int allowed = !count && guard == 15u &&
            !(MBOX->config_flags&CFG_TX_ENABLE) &&
            shm_read16(CLEAN_OBSERVER_REARM_SHM+16u) == 2u;
        if (allowed) {
#if CLEAN_RX_HT_REARM_PARK
            uint32_t parked = mac_now();
            uint32_t polls = 0u;
            while (mac_now()-parked < 16u && polls < 65535u) ++polls;
            if (mac_now()-parked < 16u) allowed = 0;
#else
            shm_write16(CLEAN_OBSERVER_RESET_SHM+1u,1u);
#define OBSERVER_RESET_WRITE(index,mask,value) \
            if ((mask) == 0xffffu) phy_reg_write(index,value); \
            else phy_reg_mod(index,mask,value);
            CLEAN_OBSERVER_RX_RESET_OPS(OBSERVER_RESET_WRITE)
#undef OBSERVER_RESET_WRITE
#endif
        }
        uint16_t phy440 = phy_reg_read(0x440u), phy44a = phy_reg_read(0x44au);
        uint16_t phya00 = phy_reg_read(0xa00u);
        shm_write16(CLEAN_OBSERVER_RESET_SHM+7u,phy440);
        shm_write16(CLEAN_OBSERVER_RESET_SHM+8u,phy44a);
        shm_write16(CLEAN_OBSERVER_RESET_SHM+9u,phya00);
        uint16_t after = observer_tx_inhibited();
        shm_write16(CLEAN_OBSERVER_RESET_SHM+11u,after);
        uint32_t elapsed = mac_now()-start;
        shm_write16(CLEAN_OBSERVER_RESET_SHM+12u,(uint16_t)elapsed);
        shm_write16(CLEAN_OBSERVER_RESET_SHM+13u,(uint16_t)(elapsed>>16u));
        shm_write16(CLEAN_OBSERVER_RESET_SHM+15u,mmio_read16(D11_BASE+0x408u));
        shm_write16(CLEAN_OBSERVER_RESET_SHM,(uint16_t)(
            allowed && after == 15u &&
#if CLEAN_RX_HT_REARM_PARK
            phy440 == shm_read16(CLEAN_OBSERVER_RESET_SHM+4u) &&
            phy44a == shm_read16(CLEAN_OBSERVER_RESET_SHM+5u) &&
            phya00 == shm_read16(CLEAN_OBSERVER_RESET_SHM+6u)
#else
            (phy440&1u) && phy44a == 0x80u && phya00 == 0xffu
#endif
            ? 2u : 3u));
    }
    mmio_write32(D11_MACCONTROL,control);
    (void)mmio_read32(D11_MACCONTROL);
}
#endif

#if CLEAN_RX_HT_PHY_SNAPSHOT
static void snapshot_observer_phy(uint32_t phase) {
    static const uint16_t indices[CLEAN_OBSERVER_PHY_REGS] = CLEAN_OBSERVER_PHY_INDICES;
    volatile uint32_t *header = (volatile uint32_t *)CLEAN_OBSERVER_PHY_ADDR;
    volatile uint16_t *values = (volatile uint16_t *)(CLEAN_OBSERVER_PHY_ADDR+8u);
    if (!phase) header[0] = header[1] = 0u;
    for (uint32_t i = 0u; i < CLEAN_OBSERVER_PHY_REGS; ++i)
        values[phase*CLEAN_OBSERVER_PHY_REGS+i] = phy_reg_read(indices[i]);
    header[1] |= 1u<<phase;
    if (phase) header[0] = CLEAN_OBSERVER_PHY_MAGIC;
}
#endif

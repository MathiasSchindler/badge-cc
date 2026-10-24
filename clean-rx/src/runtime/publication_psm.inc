static void snapshot_psm(uint32_t started, uint32_t polls) {
#if CLEAN_RX_HT_WINDOW
    volatile uint32_t *commit = (volatile uint32_t *)CLEAN_OBSERVER_PHY_ADDR;
    volatile uint16_t *live = (volatile uint16_t *)(CLEAN_OBSERVER_PHY_ADDR+4u);
    ++*commit;
    __asm__ volatile("dsb" ::: "memory");
#endif
    snapshot_rx_trace();
    for (uint32_t i = 0u; i < 16u; ++i) {
        MBOX->psm_mailbox[i] = shm_read16(0x600u + i);
        MBOX->psm_diag[i] = shm_read16(0x7a0u + i);
        if (i < CLEAN_RX_FAULT_WORDS) MBOX->rx_fifo_fault[i] = shm_read16(0x620u + i);
    }
    sample_rxe8(MBOX->rxe_pre);
    MBOX->rx_polls = polls;
    MBOX->rx_elapsed_ticks = ARM_CYCLE_COUNTER - started;
    MBOX->maccontrol_active = mmio_read32(D11_MACCONTROL);
#if CLEAN_RX_HT_WINDOW
    for (uint32_t i = 0u; i < CLEAN_HT_EARLY_WORDS; ++i)
        live[i] = shm_read16(CLEAN_HT_EARLY_SHM+i);
    live[10] = shm_read16(CLEAN_OBSERVER_SHM+1u);
    live[11] = shm_read16(CLEAN_OBSERVER_SHM+2u);
    live[12] = shm_read16(CLEAN_OBSERVER_EARLY_SHM);
    live[13] = STATION(observer_restart_attempts);
#if CLEAN_RX_HT_REARM_WAIT
    volatile uint16_t *rearm = (volatile uint16_t *)CLEAN_OBSERVER_REARM_ADDR;
    for (uint32_t i = 0u; i < CLEAN_OBSERVER_REARM_WORDS; ++i)
        rearm[i] = shm_read16(CLEAN_OBSERVER_REARM_SHM+i);
    if (rearm[0] != shm_read16(CLEAN_OBSERVER_REARM_SHM)) rearm[0] |= 1u;
#if CLEAN_RX_HT_REARM_RECOVER
    if (rearm[16] != shm_read16(CLEAN_OBSERVER_REARM_SHM+16u)) rearm[16] |= 1u;
#endif
#if CLEAN_RX_HT_REARM_RESET
    if (rearm[32] != shm_read16(CLEAN_OBSERVER_RESET_SHM)) rearm[32] = 0xffffu;
#endif
#endif
    __asm__ volatile("dsb" ::: "memory");
    ++*commit;
#endif
}


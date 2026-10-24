static void snapshot_rx_trace(void) {
    volatile uint16_t *trace = (volatile uint16_t *)(uintptr_t)CLEAN_RX_TRACE_ADDR;
    for (uint32_t i = 0u; i < CLEAN_RX_TRACE_WORDS; ++i)
        trace[i] = shm_read16(CLEAN_RX_TRACE_SHM + i);
    volatile uint32_t *tail = (volatile uint32_t *)(uintptr_t)CLEAN_TX_TAIL_ADDR;
    tail[4] = phy_reg_read(0x465u);
    tail[5] = phy_reg_read(0x42cu);
}

static void snapshot_ack_rx(void) {
    volatile clean_ack_rx_t *rx = (volatile clean_ack_rx_t *)CLEAN_ACK_RX_ADDR;
    volatile uint16_t *words = (volatile uint16_t *)(CLEAN_ACK_RX_ADDR + 4u);
    rx->magic = CLEAN_ACK_RX_MAGIC;
    for (uint32_t i = 0u; i < CLEAN_ACK_RX_WORDS; ++i)
        words[i] = shm_read16(CLEAN_ACK_RX_SHM + i);
}

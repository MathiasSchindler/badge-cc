
#if CLEAN_RX_HT_WINDOW
    ++*(volatile uint32_t *)CLEAN_OBSERVER_PHY_ADDR;
    __asm__ volatile("dsb" ::: "memory");
#endif
    MBOX->rx_elapsed_ticks = ARM_CYCLE_COUNTER - LOOP(rx_start);
    MBOX->rx_polls = LOOP(polls);

#if CLEAN_RX_AMPDU_DMA
    ampdu_dma_snapshot(mmio_read32(D11_RX_DMA_STATUS0));
#endif
    mmio_write32(D11_MACCONTROL, 0x04020400u);
    (void)mmio_read32(D11_MACCONTROL);
#if CLEAN_RX_HT_PHY_SNAPSHOT
    snapshot_observer_phy(1u);
#endif
    phy_tx_set_inhibit(1);
    MULTI->dma_status0 = mmio_read32(D11_RX_DMA_STATUS0);
    MULTI->dma_status1 = mmio_read32(D11_RX_DMA_STATUS1);
    (void)delay_ticks(20000u);
    rx_dma_stop();
#if CLEAN_RX_HT_COPY_DMA_PROBE
    volatile uint32_t *dma_trace = (volatile uint32_t *)CLEAN_OBSERVER_DMA_ADDR;
    uint32_t stopped_status = mmio_read32(D11_RX_DMA_STATUS0);
    uint32_t stopped_control = mmio_read32(D11_RX_DMA_CTL);
    dma_trace[30] = stopped_status;
    dma_trace[31] = stopped_control;
    if ((stopped_status>>28u) || stopped_control) {
        fail(0x8du,D11_RX_DMA_STATUS0,stopped_status);
    } else {
        __asm__ volatile("dsb" ::: "memory");
        uint32_t stopped_status1 = mmio_read32(D11_RX_DMA_STATUS1);
        for (uint32_t i = 0u; i < 32u; ++i)
            observer_dma_sample((const volatile uint8_t *)(FW_D11_DMA_BUF_ADDR+
                i*FW_D11_DMA_BUF_BYTES),i,stopped_status,stopped_status1,0x80000000u);
    }
#endif

    if (STATION(station_hw)) snapshot_dcf_trace();
    if (STATION(station_mac)) {
        snapshot_ack_test();
        snapshot_ack_rx();
    }
#if CLEAN_RX_AMPDU_PROBE
    volatile clean_ampdu_probe_trace_t *aggregate =
        (volatile clean_ampdu_probe_trace_t *)CLEAN_AMPDU_PROBE_ADDR;
    volatile uint16_t *aggregate_words = (volatile uint16_t *)(CLEAN_AMPDU_PROBE_ADDR + 4u);
    for (uint32_t i = 0u; i < CLEAN_AMPDU_PROBE_WORDS; ++i)
        aggregate_words[i] = shm_read16(CLEAN_AMPDU_PROBE_SHM + i);
    aggregate->magic = CLEAN_AMPDU_PROBE_MAGIC;
#endif
#if CLEAN_RX_AMPDU_INTERFACE
    volatile uint16_t *interface_words = (volatile uint16_t *)(CLEAN_AMPDU_IF_ADDR + 4u);
    for (uint32_t i = 0u; i < CLEAN_AMPDU_IF_WORDS; ++i)
        interface_words[i] = shm_read16(CLEAN_AMPDU_IF_SHM + i);
    ((volatile clean_ampdu_interface_t *)CLEAN_AMPDU_IF_ADDR)->magic = CLEAN_AMPDU_IF_MAGIC;
#endif
    if (STATION(station_mac)) {
        volatile clean_auth_failure_t *failure =
            (volatile clean_auth_failure_t *)CLEAN_AUTH_FAILURE_ADDR;
        volatile uint16_t *words = (volatile uint16_t *)(CLEAN_AUTH_FAILURE_ADDR + 4u);
        for (uint32_t i = 0u; i < CLEAN_AUTH_FAILURE_WORDS; ++i)
            words[i] = shm_read16(CLEAN_AUTH_FAILURE_SHM + i);
        failure->magic = CLEAN_AUTH_FAILURE_MAGIC;
#if CLEAN_RX_REJECT_PROBE
        volatile uint16_t *probe = (volatile uint16_t *)CLEAN_RX_REJECT_ADDR;
        for (uint32_t i = 0u; i < sizeof(clean_rx_reject_probe_t) / 2u; ++i)
            probe[i] = shm_read16(CLEAN_RX_REJECT_SHM + i);
#endif
    }
    volatile uint16_t *early = (volatile uint16_t *)&HT_DIAG->early;
    for (uint32_t i = 0u; i < CLEAN_HT_EARLY_WORDS; ++i)
        early[i] = shm_read16(CLEAN_HT_EARLY_SHM + i);
    if (CLEAN_RX_HT_RESPONSE) {
        volatile clean_ht_failure_t *failure = (volatile clean_ht_failure_t *)CLEAN_HT_FAILURE_ADDR;
        volatile uint16_t *words = (volatile uint16_t *)(CLEAN_HT_FAILURE_ADDR + 4u);
        for (uint32_t i = 0u; i < CLEAN_HT_FAILURE_WORDS; ++i)
            words[i] = shm_read16(CLEAN_HT_FAILURE_SHM + i);
        failure->magic = CLEAN_HT_FAILURE_MAGIC;
    }
#if CLEAN_RX_HT_REJECT_TRACE
    volatile uint16_t *copy_words = (volatile uint16_t *)(CLEAN_HT_REJECT_COPY_ADDR + 4u);
    for (uint32_t i = 0u; i < CLEAN_HT_REJECT_COPY_WORDS; ++i)
        copy_words[i] = shm_read16(CLEAN_HT_REJECT_COPY_SHM + i);
    ((volatile clean_ht_reject_copy_t *)CLEAN_HT_REJECT_COPY_ADDR)->magic = CLEAN_HT_REJECT_COPY_MAGIC;
#endif
#if CLEAN_RX_HT_CANDIDATE_TRACE
    volatile uint16_t *candidate_words = (volatile uint16_t *)(CLEAN_HT_CANDIDATE_ADDR + 4u);
    for (uint32_t i = 0u; i < CLEAN_HT_CANDIDATE_WORDS; ++i)
        candidate_words[i] = shm_read16(CLEAN_HT_CANDIDATE_SHM + i);
    ((volatile clean_ht_candidate_trace_t *)CLEAN_HT_CANDIDATE_ADDR)->magic = CLEAN_HT_CANDIDATE_MAGIC;
#endif
    sample_rxe8(MBOX->rxe_post);
    for (uint32_t i = 0u; i < 16u; ++i)
        MBOX->psm_mailbox[i] = shm_read16(0x600u + i);
    for (uint32_t i = 0u; i < 16u; ++i) {
        MBOX->psm_diag[i] = shm_read16(0x7a0u + i);
        if (i < CLEAN_RX_FAULT_WORDS) MBOX->rx_fifo_fault[i] = shm_read16(0x620u + i);
    }
    if (MULTI->captured_frames > 0u) {
        for (uint32_t i = 0u; i < 96u; ++i)
            MBOX->rx_snapshot[i] = MULTI->frames[0].raw[i];
        MBOX->psm_mailbox[1] = PSM_STATE_CAPTURED;
        MBOX->psm_mailbox[2] = 0u;
    }
    if (PUBLICATION(tx_diag).magic == CLEAN_TX_DIAG_MAGIC) {
        publish_tx_diag();
    } else
        for (uint32_t i = 0u; i < 90u; ++i) {
            uint16_t w = shm_read16(CLEAN_SHM_FRAME + i);
            MBOX->rx_input[2u * i] = (uint8_t)w;
            MBOX->rx_input[2u * i + 1u] = (uint8_t)(w >> 8u);
        }
    MULTI->discard_bad_fcs  = MBOX->psm_diag[8];
    MULTI->discard_oversize = MBOX->psm_diag[9];
    MULTI->discard_rx_event = MBOX->psm_diag[10];
    MULTI->discard_plcp     = MBOX->psm_diag[11];

    MBOX->fw_state = MBOX->fw_error ? FW_STATE_ERROR : FW_STATE_RX_DONE;
    __asm__ volatile("dsb" ::: "memory");

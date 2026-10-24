    ((volatile clean_edca_status_t *)CLEAN_EDCA_STATUS_ADDR)->magic = 0u;
    volatile uint32_t *mac_words = (volatile uint32_t *)CLEAN_MAC_RADIO_ADDR;
    for (uint32_t i = 0u; i < sizeof(clean_mac_radio_status_t) / 4u; ++i) mac_words[i] = 0u;
    volatile uint32_t *trace_words = (volatile uint32_t *)CLEAN_MAC_TRACE_ADDR;
    for (uint32_t i = 0u; i < sizeof(clean_mac_trace_status_t) / 4u; ++i) trace_words[i] = 0u;
    DCF_TRACE->magic = 0u;
    if (MBOX->fw_substage != 0u) {
        MBOX->fw_error = 0x99u;
        MBOX->fw_state = FW_STATE_ERROR;
        __asm__ volatile("dsb" ::: "memory");
        return;
    }
    MBOX->magic = FW_MAGIC;
    MBOX->fw_state = FW_STATE_PMU_PLL;
    static const uint32_t layout[CLEAN_RX_LAYOUT_WORDS] = CLEAN_RX_LAYOUT_VALUES;
    volatile uint32_t *published_layout = (volatile uint32_t *)CLEAN_RX_LAYOUT_ADDR;
    for (uint32_t i = 0u; i < CLEAN_RX_LAYOUT_WORDS; ++i) published_layout[i] = layout[i];
    uint32_t LOOP(flags) = MBOX->config_flags;
    uint32_t generation = MBOX->generation ? MBOX->generation : 0x13572468u;

    /* 1. Initialize PMU PLL and PMU Resources */
    int pmu_rc = pmu_init_pll_and_resources();
    if (pmu_rc != 1) {
        fail(0x11u, PMU_RES_PENDING, (uint32_t)(-pmu_rc));
        return;
    }

    /* 2. Wait for host to request HT clock on Function 1 (0x1000e = 0x10) */
    MBOX->fw_state = FW_STATE_WAIT_HT;
    __asm__ volatile("dsb" ::: "memory");
    uint32_t ht_ok = 0u;
    for (uint32_t i = 0u; i < 2000000u; ++i) {
        if (MBOX->host_cmd >= 1u && (mmio_read32(CC_CLK_CTL_ST) & 0x30000u) == 0x30000u) {
            ht_ok = 1u;
            break;
        }
    }
    if (!ht_ok) {
        fail(0x30u, CC_CLK_CTL_ST, mmio_read32(CC_CLK_CTL_ST));
        return;
    }

#ifdef CLEAN_SDIO_SELF_TEST
#include "diagnostic/sdio_run.inc"
#endif

    /* 3. Release D11 & PHY from reset, load ROM board tables, and run Radio 20691 + LCN20 PHY calibration */
    MBOX->fw_state = FW_STATE_COLD_PHY;
    uint32_t rc_lpf = 0u, rc_adc = 0u, rc_dacbuf = 0u, LOOP(vco_st) = 0u, dc_idac = 0u, dc_dcoe = 0u;
    if (!d11_and_phy_cold_init(&rc_lpf, &rc_adc, &rc_dacbuf, &LOOP(vco_st), &dc_idac, &dc_dcoe)) {
        fail(0x51u, D11_PHY_REG_ADDR, 0u);
        return;
    }
    MBOX->rc_lpf = rc_lpf;
    MBOX->rc_adc = rc_adc;
    MBOX->rc_dacbuf = rc_dacbuf;
    MBOX->vco_status = LOOP(vco_st);
    MBOX->dc_idac = dc_idac;
    MBOX->dc_dcoe = dc_dcoe;

    /* 4. Tune to requested initial 2.4 GHz channel (if != 1) */
    uint32_t LOOP(channel) = CFG_CHANNEL_GET(LOOP(flags));
    if (LOOP(channel) != 1u) {
        if (!phy_tune_channel_2g(LOOP(channel), &LOOP(vco_st), &dc_idac, &dc_dcoe)) {
            fail(0x61u, LOOP(channel), 0u);
            return;
        }
        MBOX->vco_status = LOOP(vco_st);
        MBOX->dc_idac = dc_idac;
        MBOX->dc_dcoe = dc_dcoe;
    }

    /* 5. Configure D11 RXE, PSM Branch Masks, BTCX / RF Switch, IFS/TSF/NAV, and Radio LPF */
    MBOX->fw_state = FW_STATE_EXTRA_INIT;
    d11_configure_mac_and_rxe(LOOP(flags), (uint16_t)rc_lpf);

    /* 6. Load and start D11 PSM microcode to PSM_STATE_READY */
    MBOX->fw_state = FW_STATE_PSM_LOAD;
    if (!load_and_start_psm(generation, LOOP(channel)))
        return;

    sample_rxe8(MBOX->rxe_pre);

    /* 7. Announce PSM_READY and wait for host_cmd >= 2 */
    MBOX->fw_state = FW_STATE_PSM_READY;
    __asm__ volatile("dsb" ::: "memory");
    while (MBOX->host_cmd < 2u) {
        __asm__ volatile("nop");
    }

    /* 8. Pre-configure TX chain, set station MAC for Auto-ACK, initialize RX/SDIO DMA, and enable MAC + PSM */
    phy_tx_hw_configure(LOOP(channel));
    phy_tx_set_inhibit(1);
    if (LOOP(flags) & CFG_STA_ROLE) {
        shm_write16(0x612u, 0x0802u);
        shm_write16(0x613u, 0x57dau);
        shm_write16(0x614u, 0xeaf5u);
    } else {
        shm_write16(0x612u, 0x9b02u);
        shm_write16(0x613u, 0x88bfu);
        shm_write16(0x614u, 0x7dddu);
    }
    rx_dma_init();
    if (!sdio_f2_init()) return;

    uint32_t raw_target = MULTI->target_frames;
    uint32_t LOOP(scanner_mode) = (raw_target == 0xffffffffu);
    uint32_t LOOP(target_frames) = raw_target ? raw_target : 1u;
    if (!LOOP(scanner_mode) && LOOP(target_frames) > FW_MAX_FRAMES) LOOP(target_frames) = FW_MAX_FRAMES;
    uint8_t LOOP(capture_target_reached) = 0u;
    MULTI->magic = FW_MULTI_MAGIC;
    MULTI->target_frames = LOOP(target_frames);
    MULTI->captured_frames = 0u;

    uint32_t LOOP(mac_en) = 0xc0020403u;
    if (LOOP(flags) & CFG_PROMISCUOUS)
        LOOP(mac_en) |= 0x01d00000u;
#if CLEAN_RX_HT_PHY_SNAPSHOT
    snapshot_observer_phy(0u);
#endif
    mmio_write32(D11_MACCONTROL, LOOP(mac_en));
    MBOX->maccontrol_active = mmio_read32(D11_MACCONTROL);
    MBOX->fw_state = FW_STATE_RX_ACTIVE;

    uint32_t LOOP(rx_start) = ARM_CYCLE_COUNTER;
    uint32_t LOOP(polls) = 0u;
#if CLEAN_RX_HT_OBSERVER_FIFO
    uint32_t LOOP(observer_stop_started) = 0u, LOOP(observer_stop_polls) = 0u;
#endif

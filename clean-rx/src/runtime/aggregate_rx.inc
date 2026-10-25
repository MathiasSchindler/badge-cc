static clean_ampdu_probe_trace_t AGGREGATE(ampdu_shm_snapshot);
static uint16_t AGGREGATE(ampdu_shm_outcomes)[3];
static uint8_t AGGREGATE(ampdu_shm_copied);
static uint8_t AGGREGATE(ampdu_shm_next);
#if CLEAN_RX_AMPDU_REPEAT
static uint16_t AGGREGATE(ampdu_shm_lease);
#endif

static int ampdu_shm_receive(uint32_t mac_en, uint32_t rx_start) {
    const uint32_t status = CLEAN_AMPDU_IF_SHM + CLEAN_AMPDU_SHM_STATUS_WORD;
    if (!AGGREGATE(ampdu_shm_copied)) {
        if (shm_read16(0x601u) != CLEAN_AMPDU_SHM_HELD_STATE) return 1;
        mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(mac_en));
        uint32_t control = mmio_read32(D11_MACCONTROL);
        if ((control & 3u) != (clean_mac_pause_psm(mac_en) & 3u))
            return fail(0x8du, D11_MACCONTROL, control);
        uint16_t state = shm_read16(0x601u);
        uint16_t count = shm_read16(CLEAN_AMPDU_PROBE_SHM);
        if (state != CLEAN_AMPDU_SHM_HELD_STATE || !count || count > 3u ||
            shm_read16(CLEAN_AMPDU_IF_SHM) != 3u ||
            shm_read16(CLEAN_AMPDU_IF_SHM + 1u) != count)
            return fail(0x8du, CLEAN_AMPDU_PROBE_SHM, count);
#if CLEAN_RX_AMPDU_REPEAT
        uint16_t lease = shm_read16(status + 2u);
        if (lease != (uint16_t)(AGGREGATE(ampdu_shm_lease) + 1u) ||
            shm_read16(status + 3u) != AGGREGATE(ampdu_shm_lease))
            return fail(0x8du, status + 2u, lease);
        AGGREGATE(ampdu_shm_lease) = lease;
#endif
        uint8_t *bytes = (uint8_t *)&AGGREGATE(ampdu_shm_snapshot) + 4u;
        for (uint32_t i = 0u; i < CLEAN_AMPDU_PROBE_WORDS; ++i) {
            uint16_t word = shm_read16(CLEAN_AMPDU_PROBE_SHM + i);
            bytes[2u * i] = (uint8_t)word;
            bytes[2u * i + 1u] = (uint8_t)(word >> 8u);
        }
        AGGREGATE(ampdu_shm_snapshot).magic = CLEAN_AMPDU_PROBE_MAGIC;
        for (uint32_t i = 0u; i < count; ++i)
            AGGREGATE(ampdu_shm_outcomes)[i] = shm_read16(CLEAN_AMPDU_IF_SHM + 11u + 8u * i);
        uint32_t published = (uint32_t)shm_read16(status) |
            ((uint32_t)shm_read16(status + 1u) << 16u);
        shm_write16(status + 4u, state);
        shm_write16(status + 5u, (uint16_t)(shm_read16(status + 5u) + 1u));
        uint32_t held = mmio_read32(D11_BASE + 0x180u) - published;
        shm_write16(status + 6u, (uint16_t)held);
        shm_write16(status + 7u, (uint16_t)(held >> 16u));
        AGGREGATE(ampdu_shm_copied) = 1u;
#if !CLEAN_RX_AMPDU_REPEAT
        shm_write16(0x601u, 0u);
#endif
        mmio_write32(D11_MACCONTROL, mac_en);
    }
    if (AGGREGATE(ampdu_shm_next) != AGGREGATE(ampdu_shm_snapshot).count) {
        uint32_t total = MULTI->captured_frames;
        int credit = clean_sdio_credit_available(total, TRANSPORT(rx_published_frames), FW_MAX_FRAMES);
        if (credit < 0) return fail(0x8cu, FW_MULTI_ADDR, total - TRANSPORT(rx_published_frames));
        uint16_t outcome = AGGREGATE(ampdu_shm_outcomes)[AGGREGATE(ampdu_shm_next)];
        if (outcome != 1u && outcome != 2u)
            return fail(0x8du, CLEAN_AMPDU_IF_SHM + 11u +
                        8u * AGGREGATE(ampdu_shm_next), outcome);
        if (!credit && outcome != 2u) return 1;
        volatile fw_frame_slot_t *slot = &MULTI->frames[total & (FW_MAX_FRAMES - 1u)];
        int decoded = clean_ht_ampdu_shm_decode(&AGGREGATE(ampdu_shm_snapshot),
            AGGREGATE(ampdu_shm_next), outcome, slot->raw);
        if (decoded < 0)
            return fail(decoded == CLEAN_AMPDU_DMA_FCS_ERROR ? 0x8eu : 0x8du,
                        CLEAN_AMPDU_PROBE_SHM, (uint32_t)AGGREGATE(ampdu_shm_next));
        ++AGGREGATE(ampdu_shm_next);
        if (decoded != CLEAN_AMPDU_DMA_BAD) {
            slot->frame_index = (uint16_t)(total + 1u);
            slot->rxe_len = 86u;
            slot->rxe_status1 = 0u;
            uint32_t saved_status = 264u + 2u * (AGGREGATE(ampdu_shm_next) - 1u);
            slot->rxe_status2 = (uint16_t)(AGGREGATE(ampdu_shm_snapshot).raw[saved_status] |
                ((uint16_t)AGGREGATE(ampdu_shm_snapshot).raw[saved_status + 1u] << 8u));
            slot->phy_status0 = AGGREGATE(ampdu_shm_snapshot).phy;
            slot->rx_tick_hi = (uint16_t)((ARM_CYCLE_COUNTER - rx_start) >> 16u);
            slot->dma_rx_len = slot->dma_completed = 0u;
            slot->f2_received = slot->f2_sdpcm_len = 0u;
            ampdu_shm_barrier();
            MULTI->captured_frames = total + 1u;
        }
    }
#if CLEAN_RX_AMPDU_REPEAT
    if (AGGREGATE(ampdu_shm_next) == AGGREGATE(ampdu_shm_snapshot).count) {
        mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(mac_en));
        uint32_t control = mmio_read32(D11_MACCONTROL);
        if ((control & 3u) != (clean_mac_pause_psm(mac_en) & 3u))
            return fail(0x8du, D11_MACCONTROL, control);
        if (shm_read16(0x601u) != CLEAN_AMPDU_SHM_HELD_STATE ||
            shm_read16(status + 2u) != AGGREGATE(ampdu_shm_lease) ||
            shm_read16(status + 3u) != (uint16_t)(AGGREGATE(ampdu_shm_lease) - 1u))
            return fail(0x8du, status + 2u, shm_read16(status + 2u));
        ampdu_shm_barrier();
        shm_write16(0x601u, 0u);
        AGGREGATE(ampdu_shm_copied) = 0u;
        AGGREGATE(ampdu_shm_next) = 0u;
        mmio_write32(D11_MACCONTROL, mac_en);
    }
#endif
    return 1;
}

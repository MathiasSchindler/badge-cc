#if CLEAN_RX_HT_EARLY_FIFO
static int observer_receive_early(uint32_t rx_start) {
    uint16_t owner = shm_read16(CLEAN_OBSERVER_EARLY_SHM);
    if (!owner) return 1;
    uint16_t header[CLEAN_OBSERVER_EARLY_HEADER];
    for (uint32_t i = 0u; i < CLEAN_OBSERVER_EARLY_HEADER; ++i)
        header[i] = shm_read16(CLEAN_OBSERVER_EARLY_SHM+i);
    if (owner != header[0] || owner != shm_read16(CLEAN_OBSERVER_EARLY_SHM))
        return 1;
    if (header[13] == owner) return 1;
    if (header[13]) return fail(0x8fu,CLEAN_OBSERVER_EARLY_SHM+13u,header[13]);
    int length = clean_observer_early_length(header);
    if (!length) return 1;
    if (length < 0 || shm_read16(CLEAN_OBSERVER_SHM) != 1u ||
        owner > shm_read16(CLEAN_OBSERVER_SHM+1u))
        return fail(0x8fu,CLEAN_OBSERVER_EARLY_SHM,header[2]);
    uint32_t total = MULTI->captured_frames;
    int credit = clean_sdio_credit_available(total,TRANSPORT(rx_published_frames),FW_MAX_FRAMES);
    if (credit < 0) return fail(0x8cu,FW_MULTI_ADDR,total-TRANSPORT(rx_published_frames));
    if (!credit) return 1;
    uint16_t prefix[CLEAN_OBSERVER_RECORD_WORDS];
    for (uint32_t i = 0u; i < CLEAN_OBSERVER_RECORD_WORDS; ++i)
        prefix[i] = shm_read16(CLEAN_OBSERVER_SHM+CLEAN_OBSERVER_HEADER_WORDS+
            (owner-1u)*CLEAN_OBSERVER_RECORD_WORDS+i);
    uint32_t index = total&(FW_MAX_FRAMES-1u);
    volatile fw_frame_slot_t *slot = &MULTI->frames[index];
    for (uint32_t i = 0u; i < (uint32_t)length; i += 2u) {
        uint16_t word = shm_read16(CLEAN_OBSERVER_EARLY_SHM+
            CLEAN_OBSERVER_EARLY_HEADER+i/2u);
        slot->raw[8u+i] = (uint8_t)word;
        if (i+1u < (uint32_t)length) slot->raw[9u+i] = (uint8_t)(word>>8u);
    }
    if (owner != shm_read16(CLEAN_OBSERVER_EARLY_SHM) ||
        header[1] != shm_read16(CLEAN_OBSERVER_EARLY_SHM+1u)) return 1;
    int decoded = clean_observer_early_decode(header,prefix,slot->raw);
    if (decoded != length)
        return fail(decoded == -2 ? 0x8eu : 0x8fu,CLEAN_OBSERVER_EARLY_SHM,header[2]);
    for (uint32_t i = 0u; i < 3u; ++i) {
        uint16_t ta = shm_read16(CLEAN_OBSERVER_SHM+3u+i);
        if (slot->raw[18u+2u*i] != (uint8_t)ta ||
            slot->raw[19u+2u*i] != (uint8_t)(ta>>8u))
            return fail(0x8fu,CLEAN_OBSERVER_EARLY_SHM,owner);
    }
    for (uint32_t i = 8u+(uint32_t)length; i < CLEAN_RAW_BYTES; ++i) slot->raw[i] = 0u;
    slot->frame_index = (uint16_t)(total+1u);
    slot->rxe_len = header[2];
    slot->rxe_status1 = header[3];
    slot->rxe_status2 = header[4];
    slot->phy_status0 = header[12];
    slot->rx_tick_hi = (uint16_t)((ARM_CYCLE_COUNTER-rx_start)>>16u);
    slot->dma_rx_len = slot->dma_completed = slot->f2_received = slot->f2_sdpcm_len = 0u;
    TRANSPORT(observer_capture_ids)[index] = owner|CLEAN_OBSERVER_EARLY_TAG;
    __asm__ volatile("dsb" ::: "memory");
    MULTI->captured_frames = total+1u;
    __asm__ volatile("dsb" ::: "memory");
    shm_write16(CLEAN_OBSERVER_EARLY_SHM+13u,owner);
    return 1;
}
#endif

static int station_publish_capture(uint32_t channel) {
    uint32_t total = MULTI->captured_frames;
    if (clean_sdio_credit_available(total, TRANSPORT(rx_published_frames), FW_MAX_FRAMES) < 0)
        return fail(0x8cu, FW_MULTI_ADDR, total - TRANSPORT(rx_published_frames));
    if (total == TRANSPORT(rx_published_frames)) return 1;
    volatile fw_frame_slot_t *slot = &MULTI->frames[TRANSPORT(rx_published_frames) & (FW_MAX_FRAMES - 1u)];
    int posted = sdio_f2_send_frame(slot, channel);
    if (MBOX->fw_state == FW_STATE_ERROR) return 0;
    if (!posted) return 1;
    if (!CLEAN_RX_HT_OBSERVER_FIFO && slot->rxe_len >= 34u) {
        const volatile uint8_t *m = slot->raw + 8u;
        uint8_t bound = 1u;
        for (uint32_t i = 0u; i < 6u; ++i)
            if (m[4u + i] != STATION(station_address)[i] || m[10u + i] != STATION(station_peer)[i])
                bound = 0u;
        if (bound) {
            ++MAC_TRACE->published[clean_mac_frame_kind(m[0])];
            if (!clean_mac_valid(m, slot->rxe_len - CLEAN_PLCP_BYTES))
                ++MAC_TRACE->invalid;
        }
    }
    ++TRANSPORT(rx_published_frames);
    return 1;
}

#if CLEAN_RX_AMPDU_DMA
static void ampdu_dma_snapshot(uint32_t status) {
    volatile clean_ampdu_dma_state_t *state =
        (volatile clean_ampdu_dma_state_t *)CLEAN_AMPDU_DMA_STATE_ADDR;
    if (state->magic == CLEAN_AMPDU_DMA_MAGIC && state->generation == MBOX->generation) return;
    state->magic = 0u;
    uint32_t consumer = TRANSPORT(dma_consumer);
    const volatile uint32_t *descriptor =
        (const volatile uint32_t *)(FW_D11_DMA_RING_ADDR + 16u * consumer);
    const volatile uint8_t *buf = (const volatile uint8_t *)
        (FW_D11_DMA_BUF_ADDR + consumer * FW_D11_DMA_BUF_BYTES);
    state->indices = consumer | (TRANSPORT(dma_producer) << 16u);
    state->status0 = status;
    state->status1 = mmio_read32(D11_RX_DMA_STATUS1);
    state->descriptor_flags = descriptor[0];
    state->descriptor_bytes = descriptor[1];
    state->descriptor_low = descriptor[2];
    state->descriptor_high = descriptor[3];
    state->ownership = shm_read16(0x601u) |
        ((uint32_t)shm_read16(CLEAN_AMPDU_DMA_PENDING) << 16u);
    state->progress = shm_read16(CLEAN_AMPDU_DMA_SUBMITTED) |
        ((uint32_t)mmio_read16(0x18001418u) << 16u);
    state->rxe_status = mmio_read16(0x1800141au) |
        ((uint32_t)mmio_read16(0x1800141cu) << 16u);
    state->copy_status = mmio_read16(0x18001412u) |
        ((uint32_t)mmio_read16(0x18001408u) << 16u);
    __asm__ volatile("dsb" ::: "memory");
    for (uint32_t i = 0u; i < sizeof(state->prefix); ++i) state->prefix[i] = buf[i];
    state->generation = MBOX->generation;
    __asm__ volatile("dsb" ::: "memory");
    state->magic = CLEAN_AMPDU_DMA_MAGIC;
}

static int ampdu_dma_fail(uint32_t error, uint32_t address, uint32_t value,
                          uint32_t status) {
    ampdu_dma_snapshot(status);
    return fail(error, address, value);
}

static int ampdu_dma_receive(uint32_t rx_start) {
    uint32_t status = mmio_read32(D11_RX_DMA_STATUS0);
    uint32_t current = ((status & 0x1fffu) -
        (FW_D11_DMA_RING_ADDR & 0x1fffu)) >> 4u;
    if (current >= 32u) return ampdu_dma_fail(0x8du, D11_RX_DMA_STATUS0, status, status);
    if (current == TRANSPORT(dma_consumer)) return 1;
    const volatile uint8_t *buf = (const volatile uint8_t *)(uintptr_t)
        (FW_D11_DMA_BUF_ADDR + TRANSPORT(dma_consumer) * FW_D11_DMA_BUF_BYTES);
    __asm__ volatile("dsb" ::: "memory");
    uint16_t outcome = (uint16_t)(buf[2] | ((uint16_t)buf[3] << 8u));
    if (!outcome) return 1;
    uint32_t total = MULTI->captured_frames;
    int credit = clean_sdio_credit_available(total, TRANSPORT(rx_published_frames), FW_MAX_FRAMES);
    if (credit < 0) return fail(0x8cu, FW_MULTI_ADDR, total - TRANSPORT(rx_published_frames));
    if (!credit) return 1;
    volatile fw_frame_slot_t *slot = &MULTI->frames[total & (FW_MAX_FRAMES - 1u)];
    int decoded = clean_ht_ampdu_dma_decode(buf, FW_D11_DMA_BUF_BYTES, slot->raw);
    if (decoded == CLEAN_AMPDU_DMA_BAD) {
        (void)rx_dma_poll_and_reclaim(0);
        return 1;
    }
    if (decoded != CLEAN_AMPDU_DMA_GOOD) {
        uint32_t error = decoded == CLEAN_AMPDU_DMA_FCS_ERROR ? 0x8eu :
            decoded == CLEAN_AMPDU_DMA_PREFIX_ERROR ? 0x8fu : 0x8du;
        return ampdu_dma_fail(error, FW_D11_DMA_BUF_ADDR +
                    TRANSPORT(dma_consumer) * FW_D11_DMA_BUF_BYTES + 38u,
                    (uint32_t)buf[0] | ((uint32_t)buf[1] << 8u), status);
    }
    slot->frame_index = (uint16_t)(total + 1u);
    slot->rxe_len = CLEAN_HT_AMPDU_PROBE_MPDU + CLEAN_PLCP_BYTES;
    slot->rxe_status1 = 0u;
    slot->rxe_status2 = 0u;
    slot->phy_status0 = (uint16_t)(buf[4] | ((uint16_t)buf[5] << 8u));
    slot->rx_tick_hi = (uint16_t)((ARM_CYCLE_COUNTER - rx_start) >> 16u);
    slot->f2_received = 0u;
    slot->f2_sdpcm_len = 0u;
    if (!rx_dma_poll_and_reclaim(slot))
        return ampdu_dma_fail(0x8du, D11_RX_DMA_STATUS0, status, status);
    __asm__ volatile("dsb" ::: "memory");
    MULTI->captured_frames = total + 1u;
    return 1;
}
#endif

#if CLEAN_RX_AMPDU_SHM
static void ampdu_shm_barrier(void) {
    __asm__ volatile("dsb" ::: "memory");
}
#include "aggregate_rx.inc"
#endif

/* Template preparation is deliberately outside the contention interval. The
 * PSM may answer RX while waiting; in that case its TXE setup takes precedence. */

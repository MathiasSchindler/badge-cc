#if CLEAN_RX_HT_COPY_DMA_PROBE
static void observer_dma_sample(const volatile uint8_t *buf, uint32_t index,
    uint32_t status0, uint32_t status1, uint32_t frozen) {
    uint16_t owner = shm_read16(CLEAN_OBSERVER_COPY_SHM);
    uint16_t epoch = shm_read16(CLEAN_OBSERVER_COPY_SHM+1u);
    if (!owner || owner != shm_read16(CLEAN_OBSERVER_EARLY_SHM) ||
        epoch != shm_read16(CLEAN_OBSERVER_EARLY_SHM+1u) ||
        shm_read16(CLEAN_OBSERVER_COPY_SHM+2u) != 3u) return;
    uint8_t nonce[14];
    for (uint32_t i = 0u; i < 14u; i += 2u) {
        uint16_t word = shm_read16(CLEAN_OBSERVER_EARLY_SHM+
            CLEAN_OBSERVER_EARLY_HEADER+13u+i/2u);
        nonce[i] = (uint8_t)word;
        nonce[i+1u] = (uint8_t)(word>>8u);
    }
    const uint32_t context[7] = {MULTI->dma_frames,index,TRANSPORT(dma_producer),status0,status1,
        (uint32_t)owner | ((uint32_t)epoch<<16u),shm_read16(0x601u) | frozen};
    clean_observer_dma_record((volatile uint32_t *)CLEAN_OBSERVER_DMA_ADDR,
        buf,nonce,context);
}
#endif

/* D11 RX DMA ring management (0x18001220..0x18001234) */
static void rx_dma_init(void) {
    mmio_write32(D11_RX_DMA_CTL, 0u);
    for (uint32_t i = 0u; i < 1000u; ++i) {
        if ((mmio_read32(D11_RX_DMA_STATUS0) >> 28u) == 0u) break;
    }
    volatile uint32_t *ring = (volatile uint32_t *)FW_D11_DMA_RING_ADDR;
    volatile uint32_t *bufs = (volatile uint32_t *)FW_D11_DMA_BUF_ADDR;
    for (uint32_t i = 0u; i < (32u * FW_D11_DMA_BUF_BYTES) / 4u; ++i)
        bufs[i] = 0u;

    for (uint32_t slot = 0u; slot < 32u; ++slot) {
        uint32_t buf_addr = FW_D11_DMA_BUF_ADDR + slot * FW_D11_DMA_BUF_BYTES;
        uint32_t ctrl1 = (slot == 31u) ? (1u << 28u) : 0u;
        uint32_t ctrl2 = FW_D11_DMA_BUF_BYTES & 0x7fffu;
        ring[4u * slot + 0u] = ctrl1;
        ring[4u * slot + 1u] = ctrl2;
        ring[4u * slot + 2u] = buf_addr;
        ring[4u * slot + 3u] = 0u;
    }
    __asm__ volatile("dsb" ::: "memory");

    TRANSPORT(dma_consumer) = 0u;
    TRANSPORT(dma_producer) = 31u;
#if CLEAN_RX_HT_COPY_DMA_PROBE
    clean_observer_dma_init((volatile uint32_t *)CLEAN_OBSERVER_DMA_ADDR);
#endif
    mmio_write32(D11_RX_DMA_ADDRLOW, FW_D11_DMA_RING_ADDR);
    mmio_write32(D11_RX_DMA_ADDRHIGH, 0u);
    mmio_write32(D11_RX_DMA_PTR, FW_D11_DMA_RING_ADDR);
    mmio_write32(D11_RX_DMA_CTL, FW_D11_DMA_CONTROL);
    mmio_write32(D11_RX_DMA_PTR, FW_D11_DMA_RING_ADDR + TRANSPORT(dma_producer) * 16u);
}

static void rx_dma_stop(void) {
    mmio_write32(D11_RX_DMA_CTL, 0u);
    for (uint32_t i = 0u; i < 1000u; ++i) {
        if ((mmio_read32(D11_RX_DMA_STATUS0) >> 28u) == 0u) break;
    }
}

static int rx_dma_poll_and_reclaim(volatile fw_frame_slot_t *slot) {
    uint32_t st0 = mmio_read32(D11_RX_DMA_STATUS0);
    uint32_t st1 = mmio_read32(D11_RX_DMA_STATUS1);
    MULTI->dma_status0 = st0;
    MULTI->dma_status1 = st1;
    uint32_t curr = ((st0 & 0x1fffu) - (FW_D11_DMA_RING_ADDR & 0x1fffu)) >> 4u;
    if (curr >= 32u || curr == TRANSPORT(dma_consumer))
        return 0;

    uint32_t idx = TRANSPORT(dma_consumer);
    const volatile uint8_t *buf = (const volatile uint8_t *)(uintptr_t)(FW_D11_DMA_BUF_ADDR + idx * FW_D11_DMA_BUF_BYTES);
    __asm__ volatile("dsb" ::: "memory");

    uint16_t dma_len = (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8u));
#if CLEAN_RX_HT_COPY_DMA_PROBE
    _Static_assert(CLEAN_OBSERVER_DMA_PREFIX <= FW_D11_DMA_BUF_BYTES,
        "Completed DMA prefix must fit a descriptor buffer");
    observer_dma_sample(buf,idx,st0,st1,0u);
#endif
    if (slot) {
        slot->dma_rx_len = dma_len;
        slot->dma_completed = 1u;
        for (uint32_t i = 0u; i < 38u; ++i)
            slot->dma_hdr[i] = buf[i];
    }

    volatile uint32_t *buf_w = (volatile uint32_t *)(uintptr_t)(FW_D11_DMA_BUF_ADDR + idx * FW_D11_DMA_BUF_BYTES);
    for (uint32_t i = 0u; i < FW_D11_DMA_BUF_BYTES / 4u; ++i)
        buf_w[i] = 0u;

    uint32_t post_slot = TRANSPORT(dma_producer);
    uint32_t post_buf = FW_D11_DMA_BUF_ADDR + post_slot * FW_D11_DMA_BUF_BYTES;
    volatile uint32_t *ring = (volatile uint32_t *)FW_D11_DMA_RING_ADDR;
    ring[4u * post_slot + 0u] = (post_slot == 31u) ? (1u << 28u) : 0u;
    ring[4u * post_slot + 1u] = FW_D11_DMA_BUF_BYTES & 0x7fffu;
    ring[4u * post_slot + 2u] = post_buf;
    ring[4u * post_slot + 3u] = 0u;
    __asm__ volatile("dsb" ::: "memory");

    TRANSPORT(dma_consumer) = (idx + 1u) & 31u;
    TRANSPORT(dma_producer) = (post_slot + 1u) & 31u;
    mmio_write32(D11_RX_DMA_PTR, FW_D11_DMA_RING_ADDR + TRANSPORT(dma_producer) * 16u);
    MULTI->dma_frames++;
    return 1;
}

/* SDIOD Function 2 TX DMA ring management (0x18002200..0x18002214) */
static int sdio_f2_init(void) {
    mmio_write32(SDIOD_TX_DMA_CTL, 0u);
    uint32_t status = 0u;
    for (uint32_t i = 0u; i < 2000u; ++i) {
        status = mmio_read32(SDIOD_TX_DMA_STATUS0);
        if ((status & 0xf0000000u) == 0u) break;
    }
    if (status & 0xf0000000u) return fail(0x87u, SDIOD_TX_DMA_STATUS0, status);
    volatile uint32_t *ring = (volatile uint32_t *)FW_SDIO_TX_RING_ADDR;
    for (uint32_t i = 0u; i < 32u * 4u; ++i)
        ring[i] = 0u;
    __asm__ volatile("dsb" ::: "memory");

    mmio_write32(SDIOD_TX_DMA_ADDRLOW, FW_SDIO_TX_RING_ADDR);
    mmio_write32(SDIOD_TX_DMA_ADDRHIGH, 0u);
    mmio_write32(SDIOD_TX_DMA_PTR, FW_SDIO_TX_RING_ADDR);
    TRANSPORT(sdio_tx_idx) = 0u;
    TRANSPORT(sdio_tx_posted) = 0u;
    MBOX->f2_counts = 0u;
    MBOX->f2_consumed = 0u;
    MBOX->f2_posted = 0u;
#if defined(CLEAN_SDIO_SELF_TEST) && !defined(CLEAN_SDIO_SELF_TEST_DMA32)
    mmio_write32(SDIOD_TX_DMA_CTL, 0x000a0801u);
#else
    mmio_write32(SDIOD_TX_DMA_CTL, CLEAN_SDIO_DMA_CONTROL);
#endif
    uint32_t control = mmio_read32(SDIOD_TX_DMA_CTL);
#if defined(CLEAN_SDIO_SELF_TEST) && !defined(CLEAN_SDIO_SELF_TEST_DMA32)
    if (control != 0x000a0801u)
#else
    if (control != CLEAN_SDIO_DMA_CONTROL)
#endif
        return fail(0x8bu, SDIOD_TX_DMA_CTL, control);
    return 1;
}

static int sdio_f2_send_frame(const volatile fw_frame_slot_t *slot, uint32_t channel) {
    uint32_t rxe_len = slot->rxe_len;
    if (rxe_len < 14u || rxe_len > CLEAN_RXE_BYTES)
        return fail(0x85u, FW_MULTI_ADDR, rxe_len);
    uint32_t mpdu_len = rxe_len - 6u;
    uint32_t sdpcm_len = 18u + mpdu_len;
    uint32_t fragments = sdpcm_len > CLEAN_F2_PACKET_BYTES ?
        (mpdu_len + CLEAN_F2_FRAGMENT_BYTES - 1u) / CLEAN_F2_FRAGMENT_BYTES : 1u;

    int credit = clean_sdio_credit_available(TRANSPORT(sdio_tx_posted), MBOX->f2_consumed,
                                              SDIO_TX_WINDOW);
    if (credit < 0) {
        (void)fail(0x8cu, FW_MBOX_ADDR + CLEAN_SDIO_ACK_OFFSET, MBOX->f2_consumed);
        return 0;
    }
    if (!credit) {
        if (!STATION(station_enabled) && !CLEAN_RX_HT_DATA && !CLEAN_RX_HT_OBSERVER_FIFO)
            MBOX->f2_counts = clean_sdio_count(MBOX->f2_counts, 1u);
        return 0;
    }
    uint32_t status = mmio_read32(SDIOD_TX_DMA_STATUS0);
    for (uint32_t part = 0u; part < fragments; ++part) {
        int available = clean_sdio_available((TRANSPORT(sdio_tx_idx) + part) & 31u, status);
        if (available < 0) {
            (void)fail(0x86u, SDIOD_TX_DMA_STATUS0, status);
            return 0;
        }
        if (!available) {
            if (!STATION(station_enabled) && !CLEAN_RX_HT_DATA && !CLEAN_RX_HT_OBSERVER_FIFO)
                MBOX->f2_counts = clean_sdio_count(MBOX->f2_counts, 1u);
            return 0;
        }
    }

    uint8_t sequence = TRANSPORT(sdpcm_seq)++;
    for (uint32_t part = 0u; part < fragments; ++part) {
        uint32_t offset = part * CLEAN_F2_FRAGMENT_BYTES;
        uint32_t payload = mpdu_len - offset;
        if (fragments > 1u && payload > CLEAN_F2_FRAGMENT_BYTES)
            payload = CLEAN_F2_FRAGMENT_BYTES;
        uint32_t header = fragments > 1u ? CLEAN_F2_FRAGMENT_HEADER : 18u;
        uint32_t physical_len = header + payload;
        uint32_t dma_len = (physical_len + 3u) & ~3u;
        uint32_t idx = TRANSPORT(sdio_tx_idx);
        uint32_t buf_addr = FW_SDIO_TX_BUF_ADDR + idx * FW_SDIO_TX_BUF_BYTES;
        volatile uint8_t *pkt = (volatile uint8_t *)(uintptr_t)buf_addr;
        for (uint32_t i = 0u; i < dma_len; ++i)
            pkt[i] = 0u;

        /* 12-byte SDPCM header + 2-byte pad */
        pkt[0] = (uint8_t)physical_len;
        pkt[1] = (uint8_t)(physical_len >> 8u);
        pkt[2] = (uint8_t)~physical_len;
        pkt[3] = (uint8_t)(~physical_len >> 8u);
        pkt[4] = sequence;
        pkt[5] = 2u; /* Channel 2 = Data */
        pkt[6] = 0u;
        pkt[7] = 14u; /* Data offset = 14 */
        pkt[8] = 0u;
        pkt[9] = 8u; /* Credit */
        pkt[10] = 0u;
        pkt[11] = 0u;
#if CLEAN_RX_HT_OBSERVER
#if CLEAN_RX_HT_OBSERVER_FIFO
        uint16_t correlation = TRANSPORT(observer_capture_ids)[
            (slot->frame_index-1u)&(FW_MAX_FRAMES-1u)];
#else
        /* Passive diagnostic ID; ordinary profiles keep reserved bytes zero. */
        uint16_t correlation = shm_read16(CLEAN_OBSERVER_HELD);
#if CLEAN_RX_HT_ADVANCE
        if (correlation && correlation == shm_read16(CLEAN_OBSERVER_EARLY_SHM) &&
            shm_read16(CLEAN_OBSERVER_ADVANCE) == 1u)
            correlation |= CLEAN_OBSERVER_ADVANCE_TAG;
#endif
#endif
        pkt[10] = (uint8_t)correlation;
        pkt[11] = (uint8_t)(correlation >> 8u);
#endif
        pkt[12] = (uint8_t)CLEAN_RX_PHASE_MAGIC | (uint8_t)(TRANSPORT(rx_after_explicit_tx) << 7u);
        pkt[13] = (uint8_t)(CLEAN_RX_PHASE_MAGIC >> 8u);

        /* 4-byte BDC header at offset 14 */
        pkt[14] = fragments > 1u ? 0x21u : 0x20u;
        pkt[15] = slot->raw[2]; /* PLCP[0] rate/SIGNAL byte */
        pkt[16] = clean_rx_phy_metadata(slot->phy_status0, slot->rxe_status2, channel);
        pkt[17] = clean_ht_rx_version(CLEAN_RX_HT_DATA, slot->phy_status0,
                                      slot->raw + 2u, mpdu_len);
        if (pkt[17] == CLEAN_HT_AMPDU_PROBE_VERSION)
            pkt[16] = CLEAN_RX_HT20 | CLEAN_RX_UNSUPPORTED_PHY | (uint8_t)channel;
        else if (pkt[17]) pkt[16] |= CLEAN_RX_HT20;
#if CLEAN_RX_HT_EARLY_FIFO
        /* Early HT-SIG admission precedes the terminal PHY-status header. */
        if (correlation&CLEAN_OBSERVER_EARLY_TAG)
            pkt[16] = CLEAN_RX_UNSUPPORTED_PHY|(uint8_t)channel;
#endif

        if (fragments > 1u) {
            pkt[18] = (uint8_t)mpdu_len;
            pkt[19] = (uint8_t)(mpdu_len >> 8u);
            pkt[20] = (uint8_t)offset;
            pkt[21] = (uint8_t)(offset >> 8u);
        }
        for (uint32_t i = 0u; i < payload; ++i)
            pkt[header + i] = slot->raw[8u + offset + i];
        __asm__ volatile("dsb" ::: "memory");

        volatile uint32_t *ring = (volatile uint32_t *)FW_SDIO_TX_RING_ADDR;
        ring[4u * idx + 0u] = clean_sdio_flags(idx);
        ring[4u * idx + 1u] = dma_len & 0x7fffu;
        ring[4u * idx + 2u] = buf_addr;
        ring[4u * idx + 3u] = 0u;
        __asm__ volatile("dsb" ::: "memory");

        TRANSPORT(sdio_tx_idx) = (idx + 1u) & 31u;
    }
    /* Both physical packets are ready before publishing one logical-frame credit. */
    ++TRANSPORT(sdio_tx_posted);
    MBOX->f2_posted = TRANSPORT(sdio_tx_posted);
    mmio_write32(SDIOD_TX_DMA_PTR, FW_SDIO_TX_RING_ADDR + TRANSPORT(sdio_tx_idx) * 16u);
    MBOX->f2_counts = clean_sdio_count(MBOX->f2_counts, 0u);
    return 1;
}

/* Transmit an 11 Mbps CCK IEEE 802.11 ACK frame (FC=0x00d4, RA=ta[0..5]) immediately upon unicast reception */

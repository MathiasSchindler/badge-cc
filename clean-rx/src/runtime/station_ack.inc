static void d11_transmit_ack_frame(const uint8_t ta[6], uint32_t channel, uint32_t mac_tx_ctl) {
    uint8_t ack[20];
    ack[0] = 0x6eu; ack[1] = 0x84u; ack[2] = 0x0bu; ack[3] = 0x00u; ack[4] = 0xd0u; ack[5] = 0x9bu;
    ack[6] = 0xd4u; ack[7] = 0x00u; ack[8] = 0x00u; ack[9] = 0x00u;
    for (uint32_t i = 0u; i < 6u; ++i) ack[10u + i] = ta[i];
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 6u; i < 16u; ++i) {
        crc ^= ack[i];
        for (uint32_t b = 0u; b < 8u; ++b)
            crc = (crc & 1u) ? ((crc >> 1u) ^ 0xedb88320u) : (crc >> 1u);
    }
    crc = ~crc;
    ack[16] = (uint8_t)crc;
    ack[17] = (uint8_t)(crc >> 8u);
    ack[18] = (uint8_t)(crc >> 16u);
    ack[19] = (uint8_t)(crc >> 24u);

    mmio_write32(D11_MACCONTROL, 0x84020400u);
    (void)mmio_read32(D11_MACCONTROL);
    phy_tx_hw_configure(channel);
    phy_tx_set_inhibit(0);

    mmio_write32(D11_TPL_WR_PTR, 0u);
    for (uint32_t i = 0u; i < 5u; ++i) {
        const uint8_t *b = ack + 4u * i;
        uint32_t w = (uint32_t)b[0] | ((uint32_t)b[1] << 8u) |
                     ((uint32_t)b[2] << 16u) | ((uint32_t)b[3] << 24u);
        mmio_write32(D11_TPL_WR_DATA, w);
    }

    mmio_write32(D11_MACCONTROL, mac_tx_ctl);
    (void)mmio_read32(D11_MACCONTROL);
    mmio_write16(0x18001570u, 0x0000u);
    mmio_write16(0x18001568u, 0x0000u);
    mmio_write16(0x180017c0u, 0x0100u);
    mmio_write16(0x1800150cu, 0x0040u);
    mmio_write16(0x18001514u, 0x1a02u);
    mmio_write16(0x18001508u, 0x0000u);
    mmio_write16(0x1800150au, 0x0000u);
    mmio_write16(0x1800154cu, 0x0000u);
    mmio_write16(0x1800156au, 20u);
    mmio_write16(0x18001568u, 0x0826u);
    mmio_write16(0x18001500u, 0x0000u);
    mmio_write16(0x18001502u, 0x00d0u);
    (void)mmio_read16(0x18001502u);

    for (uint32_t i = 0u; i < 1000u; ++i) {
        if ((mmio_read16(0x1800150eu) & 0x0080u) == 0u) break;
        (void)delay_ticks(90u);
    }
    for (uint32_t i = 0u; i < 1000u; ++i) {
        if ((mmio_read16(0x1800150eu) & 0x0400u) != 0u) break;
        (void)delay_ticks(90u);
    }
    for (uint32_t i = 0u; i < 1000u; ++i) {
        if ((mmio_read16(0x18001690u) & 0x0100u) == 0u) break;
        (void)delay_ticks(90u);
    }

    mmio_write16(0x18001502u, 0x0000u);
    mmio_write16(0x18001568u, 0x0000u);
    mmio_write16(0x180017c0u, 0x0083u);
    mmio_write32(D11_MACCONTROL, 0x84020400u);
    (void)mmio_read32(D11_MACCONTROL);
    phy_tx_set_inhibit(1);
    shm_write16(0x7afu, (uint16_t)(shm_read16(0x7afu) + 1u));
}


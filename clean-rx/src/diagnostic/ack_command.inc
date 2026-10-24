        if ((cmd & 0xffu) == 9u) {
            int count = clean_test_ack_count(cmd, MAC_RADIO->profile);
            if (!STATION(station_hw) || count < 0) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            uint16_t ack_fault = shm_read16(CLEAN_ACK_TEST_FAULT);
            uint16_t ack_phase = shm_read16(CLEAN_ACK_TEST_PHASE);
            if (ack_fault || ack_phase > 2u) {
                fail(0x87u, ack_fault ? CLEAN_ACK_TEST_FAULT : CLEAN_ACK_TEST_PHASE,
                     ack_fault ? ack_fault : ack_phase);
                break;
            }
            if (!station_policy_boundary(shm_read16(0x601u))) continue;
            mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(LOOP(mac_en)));
            (void)mmio_read32(D11_MACCONTROL);
            if (!station_policy_boundary(shm_read16(0x601u)) || station_phy_busy() ||
                shm_read16(CLEAN_ACK_TEST_PHASE)) {
                mmio_write32(D11_MACCONTROL, LOOP(mac_en));
                (void)mmio_read32(D11_MACCONTROL);
                continue;
            }
            shm_write16(CLEAN_DCF_IDLE_VALID, 0u);
            if (!station_ack_test_configure((uint16_t)count)) break;
            mmio_write32(D11_MACCONTROL, LOOP(mac_en));
            MBOX->host_cmd = 2u;
            __asm__ volatile("dsb" ::: "memory");
            continue;
        }

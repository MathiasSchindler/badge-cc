    _Static_assert(__builtin_offsetof(fw_mbox_t, f2_consumed) == CLEAN_SDIO_TEST_ACK_OFFSET,
                   "Transport acknowledgement offset");
    _Static_assert(__builtin_offsetof(fw_mbox_t, rc_adc) == CLEAN_SDIO_TEST_WINDOW_OFFSET,
                   "Transport window offset");
    _Static_assert(__builtin_offsetof(fw_mbox_t, rc_dacbuf) == CLEAN_SDIO_TEST_PATTERN_OFFSET,
                   "Transport pattern offset");
#ifdef CLEAN_SDIO_SELF_TEST_LARGE
    const uint32_t test_pattern = 2u;
#elif defined(CLEAN_SDIO_SELF_TEST_TRANSITION)
    const uint32_t test_pattern = 1u;
#else
    const uint32_t test_pattern = 0u;
#endif
    if (!sdio_f2_init()) return;
    MBOX->rc_lpf = 0u;
    MBOX->rc_adc = CLEAN_SDIO_SELF_TEST_WINDOW;
    MBOX->rc_dacbuf = test_pattern;
#ifdef CLEAN_SDIO_SELF_TEST_PACED
    MBOX->fw_substage = CLEAN_SDIO_TEST_PACING_TICKS;
#else
    MBOX->fw_substage = 0u;
#endif
    MBOX->fw_state = FW_STATE_PSM_READY;
    while (MBOX->host_cmd < 2u) __asm__ volatile("nop");
    MBOX->fw_state = FW_STATE_RX_ACTIVE;
    uint32_t sequence = 0u;
    while (1) {
        uint32_t command = MBOX->host_cmd;
        if ((command & 255u) != 7u) continue;
        uint32_t count = command >> 8u;
        if (count < 1u || count > 16u) {
            (void)fail(0x88u, FW_MBOX_ADDR + 24u, command);
            return;
        }
        for (uint32_t i = 0u; i < count; ++i) {
#if CLEAN_SDIO_SELF_TEST_WINDOW > 0u
            uint32_t waits = 0u;
            while (!clean_sdio_test_window_open(sequence, MBOX->f2_consumed,
                                                CLEAN_SDIO_SELF_TEST_WINDOW) &&
                   waits < 20000000u) ++waits;
            if (waits == 20000000u) {
                (void)fail(0x8au, FW_MBOX_ADDR + CLEAN_SDIO_TEST_ACK_OFFSET, MBOX->f2_consumed);
                return;
            }
#endif
            volatile fw_frame_slot_t *slot = &MULTI->frames[0];
            uint32_t length = clean_sdio_test_pattern_length(++sequence, test_pattern);
            slot->rxe_len = (uint16_t)(length + 6u);
            for (uint32_t byte = 0u; byte < length; ++byte)
                slot->raw[8u + byte] = clean_sdio_test_byte(sequence, byte);
            sdio_f2_send_frame(slot, 6u);
            if (MBOX->fw_state == FW_STATE_ERROR) return;
#ifdef CLEAN_SDIO_SELF_TEST_PACED
            if (!delay_ticks(CLEAN_SDIO_TEST_PACING_TICKS)) {
                (void)fail(0x89u, 0x18003090u, CLEAN_SDIO_TEST_PACING_TICKS);
                return;
            }
#endif
        }
        MBOX->host_cmd = 2u;
        __asm__ volatile("dsb" ::: "memory");
    }

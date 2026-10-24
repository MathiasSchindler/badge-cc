static int station_ack_test_configure(uint16_t count) {
    ack_test_configured = count;
    ++ack_test_epoch;
    ack_expiry_at = ack_expiry_started = ack_expiry_window = 0u;
    for (uint32_t i = 0u; i < CLEAN_ACK_OBSERVE_WORDS; ++i)
        shm_write16(CLEAN_ACK_OBSERVE_SHM + i, 0u);
    for (uint32_t i = 0u; i < 3u; ++i)
        shm_write16(CLEAN_ACK_ARITH_SHM + i, 0u);
    for (uint32_t i = 0u; i < CLEAN_ACK_TEST_WORDS; ++i)
        shm_write16(CLEAN_ACK_TEST_SHM + i, 0u);
    shm_write16(CLEAN_TEST_ACK_REMAINING, count);
    shm_write16(CLEAN_TEST_ACK_DROPPED, 0u);
    shm_write16(CLEAN_ACK_TEST_LIMIT, count);
    shm_write16(CLEAN_ACK_TEST_EPOCH, ack_test_epoch);
    for (uint32_t i = 0u; i < CLEAN_ACK_TEST_WORDS; ++i)
        if (!station_policy_word(CLEAN_ACK_TEST_SHM + i,
                i == 0u ? count : i == 9u ? ack_test_epoch : 0u)) return 0;
    for (uint32_t i = 0u; i < CLEAN_ACK_OBSERVE_WORDS; ++i)
        if (!station_policy_word(CLEAN_ACK_OBSERVE_SHM + i, 0u)) return 0;
    for (uint32_t i = 0u; i < 3u; ++i)
        if (!station_policy_word(CLEAN_ACK_ARITH_SHM + i, 0u)) return 0;
    return station_policy_word(CLEAN_TEST_ACK_REMAINING, count) &&
        station_policy_word(CLEAN_TEST_ACK_DROPPED, 0u);
}

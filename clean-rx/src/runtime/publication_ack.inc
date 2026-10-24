static void snapshot_ack_test(void) {
    volatile clean_ack_test_status_t *s =
        (volatile clean_ack_test_status_t *)CLEAN_ACK_TEST_ADDR;
    s->magic = 0u;
    s->configured = STATION(ack_test_configured);
    s->limit = shm_read16(CLEAN_ACK_TEST_LIMIT);
    s->remaining = shm_read16(CLEAN_TEST_ACK_REMAINING);
    s->dropped = shm_read16(CLEAN_TEST_ACK_DROPPED);
    s->fault = shm_read16(CLEAN_ACK_TEST_FAULT);
    s->phase = shm_read16(CLEAN_ACK_TEST_PHASE);
    s->before = shm_read16(CLEAN_ACK_TEST_BEFORE);
    s->after = shm_read16(CLEAN_ACK_TEST_AFTER);
    s->dropped_before = shm_read16(CLEAN_ACK_TEST_DROPPED_BEFORE);
    s->clock = shm_read16(CLEAN_ACK_TEST_CLOCK);
    s->frame_control = shm_read16(CLEAN_ACK_TEST_FC);
    s->sequence = shm_read16(CLEAN_ACK_TEST_SEQUENCE);
    s->epoch = shm_read16(CLEAN_ACK_TEST_EPOCH);
    s->reserved = 0u;
    __asm__ volatile("dsb" ::: "memory");
    s->magic = CLEAN_ACK_TEST_MAGIC;
    volatile clean_ack_observe_t *o =
        (volatile clean_ack_observe_t *)CLEAN_ACK_OBSERVE_ADDR;
    o->magic = 0u;
    volatile uint16_t *words = (volatile uint16_t *)(CLEAN_ACK_OBSERVE_ADDR + 4u);
    for (uint32_t i = 0u; i < CLEAN_ACK_OBSERVE_WORDS; ++i)
        words[i] = shm_read16(CLEAN_ACK_OBSERVE_SHM + i);
    o->arm_at = STATION(ack_expiry_at);
    o->arm_started = STATION(ack_expiry_started);
    o->arm_window = STATION(ack_expiry_window);
    __asm__ volatile("dsb" ::: "memory");
    o->magic = CLEAN_ACK_OBSERVE_MAGIC;
    volatile clean_ack_arith_t *a = (volatile clean_ack_arith_t *)CLEAN_ACK_ARITH_ADDR;
    a->magic = 0u;
    a->airtime = shm_read16(CLEAN_ACK_TEST_AIRTIME);
    a->sum = shm_read16(CLEAN_ACK_TEST_SUM);
    a->limit = shm_read16(CLEAN_ACK_TEST_LIMIT_SEEN);
    a->reserved = 0u;
    __asm__ volatile("dsb" ::: "memory");
    a->magic = CLEAN_ACK_ARITH_MAGIC;
}


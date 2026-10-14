#include "cyw43_hw.h"

#define PMU_HT_MASK 0x70000000u
#define PMU_CC_HT   0x00020000u
#define PMU_RESOURCE_MASK 0x7fffffffu

typedef struct {
    uint32_t resource;
    uint32_t timing;
} pmu_timing_step_t;

static const uint32_t pll_words[9] = {
    0u, 0u, 0x431u, 0xba2e8cu, 0x04040404u, 0x00040104u, 0x080004e2u, 0xau, 0x106au
};

static const pmu_timing_step_t res_timings[33] = {
    {12u, 0x00160002u}, { 2u, 0x000d0004u}, { 7u, 0x000a0001u}, { 8u, 0x000d0004u},
    {10u, 0x000d0004u}, {13u, 0x00070001u}, {23u, 0x00040004u}, {27u, 0x00010004u},
    {28u, 0x00070001u}, { 0u, 0x00020002u}, { 1u, 0x00010001u}, { 3u, 0x00020002u},
    { 4u, 0x00010001u}, { 5u, 0x00010001u}, { 6u, 0x00010001u}, { 9u, 0x00050001u},
    {11u, 0x00010001u}, {12u, 0x00300002u}, {14u, 0x00010001u}, {15u, 0x00010001u},
    {16u, 0x00010001u}, {17u, 0x00010001u}, {18u, 0x00030003u}, {19u, 0x00010001u},
    {20u, 0x00010001u}, {21u, 0x00010001u}, {22u, 0x00010001u}, {24u, 0x00130002u},
    {25u, 0x00130002u}, {26u, 0x00130002u}, {29u, 0x00010001u}, {30u, 0x00010001u},
    {12u, 0x00300002u}
};

static const uint32_t res_dependencies[31] = {
    0u, 1u, 3u, 0u, 7u, 0x1157u, 0x117u, 0x157u, 3u, 0u, 3u, 0x407u, 0x157u, 0x15d7u,
    0x0ec2bdf7u, 0x0ec23df7u, 0u, 0x0ec03df7u, 0x0ec23df7u, 0x0ec23df7u,
    0x06c03df7u, 0x0eda3df7u, 0x06003df7u, 0x06403df7u, 0u, 0x1d7u, 0xdd7u,
    0x06c03df7u, 0x0eca3df7u, 0x1eca3df7u, 0x3efa3df7u
};

static void pmu_bank_rmw(uint32_t sel_addr, uint32_t index, uint32_t mask, uint32_t val) {
    mmio_write32(sel_addr, index);
    (void)mmio_read32(sel_addr);
    uint32_t cur = mmio_read32(sel_addr + 4u);
    mmio_write32(sel_addr + 4u, (cur & ~mask) | val);
    (void)mmio_read32(sel_addr + 4u);
}

static void pmu_pll_prepare_pulse(void) {
    pmu_bank_rmw(PMU_PLL_CONTROL_ADDR, 5u, 0xffu, 60u);
    pmu_bank_rmw(PMU_REG_CONTROL_ADDR, 6u, 0x00800000u, 0x00800000u);
    (void)delay_ticks(30u * 40u);
    pmu_bank_rmw(PMU_REG_CONTROL_ADDR, 6u, 0x00800000u, 0u);
}

static uint32_t timer_sample(void) {
    uint32_t first = mmio_read32(PMU_TIMER);
    uint32_t second = mmio_read32(PMU_TIMER);
    return (first == second) ? first : mmio_read32(PMU_TIMER);
}

static int pending_wait(int zero, uint32_t bound, uint32_t *elapsed, int *matched) {
    uint32_t previous = timer_sample();
    *elapsed = 0u;
    *matched = 0;
    for (uint32_t i = 0u; i < 4096u; ++i) {
        uint32_t pending = mmio_read32(PMU_RES_PENDING);
        if ((pending == 0u) == zero) {
            *matched = 1;
            return 1;
        }
        if (*elapsed >= bound)
            return 1;
        uint32_t tick = timer_sample();
        uint32_t delta = tick - previous;
        previous = tick;
        if (delta >= 0x80000000u)
            return 0;
        if (delta > ((0xffffffffu - *elapsed) >> 5u))
            *elapsed = 0xffffffffu;
        else
            *elapsed += delta << 5u;
    }
    return 0;
}

static int pmu_wait_pending_settle(void) {
    uint32_t total = 0u;
    for (uint32_t pass = 0u; pass < 64u; ++pass) {
        uint32_t zero_elapsed = 0u, quiet_elapsed = 0u;
        int matched = 0;
        if (!pending_wait(1, 15000u, &zero_elapsed, &matched)) return 0;
        if (!matched) return 0;
        if (!pending_wait(0, 64u, &quiet_elapsed, &matched)) return 0;
        if (!matched) return 1;
        if (zero_elapsed >= 15000u || quiet_elapsed >= 15000u ||
            total >= 15000u - zero_elapsed || total + zero_elapsed >= 15000u - quiet_elapsed)
            return 0;
        total += zero_elapsed + quiet_elapsed;
    }
    return 0;
}

int pmu_init_pll_and_resources(void) {
    /* 1. Initialize PMU_CONTROL and configure 37.4 MHz crystal divisor */
    uint32_t ctl = mmio_read32(PMU_CONTROL);
    mmio_write32(PMU_CONTROL, ctl | 0x200u);
    (void)mmio_read32(PMU_CONTROL);

    pmu_pll_prepare_pulse();

    ctl = mmio_read32(PMU_CONTROL);
    uint32_t xtal_div = ((37400u + 127u) / 128u) - 1u;
    uint32_t ctl_checked = ((ctl | 0x200u) & 0x0000ff83u) | (xtal_div << 16u) | 0x3cu;
    mmio_write32(PMU_CONTROL, ctl_checked);
    (void)mmio_read32(PMU_CONTROL);

    uint32_t saved_min = mmio_read32(PMU_MIN_RES_MASK);
    uint32_t saved_max = mmio_read32(PMU_MAX_RES_MASK);
    uint32_t saved_clk = mmio_read32(CC_CLK_CTL_ST);

    if ((saved_clk & PMU_CC_HT) == 0u) {
        if (!pmu_wait_pending_settle()) return -1;
    }

    /* 2. Clear HT clock bits in MIN and MAX resource masks while programming PLL */
    mmio_write32(PMU_MIN_RES_MASK, mmio_read32(PMU_MIN_RES_MASK) & ~PMU_HT_MASK);
    (void)mmio_read32(PMU_MIN_RES_MASK);
    mmio_write32(PMU_MAX_RES_MASK, mmio_read32(PMU_MAX_RES_MASK) & ~PMU_HT_MASK);
    (void)mmio_read32(PMU_MAX_RES_MASK);

    uint32_t ht_off = 0u;
    for (uint32_t i = 0u; i < 64u; ++i) {
        if ((mmio_read32(CC_CLK_CTL_ST) & PMU_CC_HT) == 0u) {
            ht_off = 1u;
            break;
        }
        (void)delay_ticks(10u * 40u);
    }
    if (!ht_off) return -2;
    (void)delay_ticks(100u * 40u);

    pmu_pll_prepare_pulse();

    /* 3. Write 9-word PMU PLL configuration table and commit (bit 0x400) */
    for (uint32_t i = 0u; i < 9u; ++i) {
        mmio_write32(PMU_PLL_CONTROL_ADDR, i);
        (void)mmio_read32(PMU_PLL_CONTROL_ADDR);
        mmio_write32(PMU_PLL_CONTROL_DATA, pll_words[i]);
        (void)mmio_read32(PMU_PLL_CONTROL_DATA);
    }
    mmio_write32(PMU_CONTROL, mmio_read32(PMU_CONTROL) | 0x400u);
    (void)mmio_read32(PMU_CONTROL);

    if (saved_max & PMU_HT_MASK) {
        mmio_write32(PMU_MAX_RES_MASK, mmio_read32(PMU_MAX_RES_MASK) | (saved_max & PMU_HT_MASK));
        (void)mmio_read32(PMU_MAX_RES_MASK);
    }
    if (saved_min & PMU_HT_MASK) {
        mmio_write32(PMU_MIN_RES_MASK, mmio_read32(PMU_MIN_RES_MASK) | (saved_min & PMU_HT_MASK));
        (void)mmio_read32(PMU_MIN_RES_MASK);
    }

    /* 4. Program PMU resource up/down timers (33 records in reverse table order) */
    for (uint32_t i = 0u; i < 33u; ++i) {
        const pmu_timing_step_t *row = &res_timings[32u - i];
        mmio_write32(PMU_RES_TABLE_SEL, row->resource);
        mmio_write32(PMU_RES_UPDN_TIMER, row->timing);
    }

    /* 5. Program PMU resource dependency masks (resources 30 down to 0) */
    for (uint32_t i = 31u; i > 0u; --i) {
        uint32_t idx = i - 1u;
        mmio_write32(PMU_RES_TABLE_SEL, idx);
        mmio_write32(PMU_RES_DEP_MASK, res_dependencies[idx]);
    }

    /* 6. Install minimum (0x0ec23df7) and maximum (0x7fffffff) resource masks */
    uint32_t init_max = mmio_read32(PMU_MAX_RES_MASK);
    mmio_write32(PMU_MAX_RES_MASK, init_max | PMU_RESOURCE_MASK);
    mmio_write32(PMU_MIN_RES_MASK, 0x0ec23df7u);
    mmio_write32(PMU_MAX_RES_MASK, PMU_RESOURCE_MASK);

    if (!pmu_wait_pending_settle()) return -3;
    return delay_ticks(200000u) ? 1 : -4;
}

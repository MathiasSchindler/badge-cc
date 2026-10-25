#include "probe.h"

/* Reuse initialization and private calibration operations without changing
 * clean-rx's ordinary controller, command admission or channel table. */
#include "../../../clean-rx/src/radio_phy.c"

#define MBOX ((volatile fw_mbox_t *)FW_MBOX_ADDR)
#define REPORT ((volatile wf_report_t *)WF_ADDRESS)

static void fault(uint32_t error, uint32_t address, uint32_t value) {
    MBOX->fw_error = error;
    MBOX->fw_error_addr = address;
    MBOX->fw_error_val = value;
    MBOX->fw_state = FW_STATE_ERROR;
    __asm__ volatile("dsb" ::: "memory");
}

static int wait_command(uint32_t command) {
    uint32_t start = ARM_CYCLE_COUNTER;
    for (uint32_t i = 0u; i < 20000000u; ++i) {
        if (MBOX->host_cmd == command) return 1;
        if (ARM_CYCLE_COUNTER - start >= 180000000u) break;
    }
    fault(0xe1u, FW_MBOX_ADDR + 24u, MBOX->host_cmd);
    return 0;
}

static uint32_t retune(uint32_t mhz, uint32_t *vco) {
    uint32_t divider = wf_divider(mhz);
    phy_reg_mod(0x440u, 1u, 0u);
    radio_reg_write(0x55u, (uint16_t)mhz);
    radio_reg_write(0x4eu, (uint16_t)(divider >> 16u));
    radio_reg_write(0x4fu, (uint16_t)divider);
    /* Same VCO-calibration edges as clean-rx; analog settings remain those
     * of the nearest existing channel, not a claimed out-of-band calibration. */
    static const reg_op_t trigger[] = {
        {OP_RAD_OR, 0x8au, 0u, 0x0100u},
        {OP_RAD_MOD, 0x1bu, 0x1000u, 0u},
        {OP_RAD_MOD, 0x51u, 1u, 0u}, {OP_DELAY, 1u, 0u, 0u},
        {OP_RAD_MOD, 0x51u, 1u, 1u},
        {OP_RAD_MOD, 0xa0u, 0x0100u, 0x0100u},
        {OP_RAD_MOD, 0x42u, 0x0010u, 0u},
        {OP_RAD_MOD, 0xa0u, 0x0200u, 0x0200u},
        {OP_RAD_MOD, 0x42u, 0x0020u, 0u}, {OP_DELAY, 1u, 0u, 0u},
        {OP_RAD_MOD, 0x42u, 0x0010u, 0x0010u},
        {OP_RAD_MOD, 0x42u, 0x0020u, 0x0020u}, {OP_DELAY, 1u, 0u, 0u},
        {OP_RAD_MOD, 0x42u, 0x0080u, 0x0080u},
        {OP_RAD_MOD, 0x42u, 0x0040u, 0x0040u}, {OP_DELAY, 1u, 0u, 0u},
        {OP_RAD_MOD, 0x42u, 0x0080u, 0u},
        {OP_RAD_MOD, 0x42u, 0x0040u, 0u}, {OP_DELAY, 50u, 0u, 0u}
    };
    run_ops(trigger, sizeof(trigger) / sizeof(trigger[0]));
    uint32_t result = WF_LOCK_NOT_INDICATED;
    for (uint32_t i = 0u; i < 100u; ++i) {
        if (!delay_ticks(1800u)) { result = WF_TIMER_FAILED; break; }
        *vco = radio_reg_read(0x65u);
        if (*vco != 0xffffu && (*vco & 1u)) {
            result = WF_READBACK_OK;
            break;
        }
    }
    if (!delay_ticks(18000u)) result = WF_TIMER_FAILED;
    radio_reg_mod(0x8au, 0x0100u, 0u);
    return result;
}

static void probe(volatile wf_record_t *out, uint32_t mhz) {
    uint32_t start = ARM_CYCLE_COUNTER;
    uint32_t channel = wf_nearest_channel(mhz), vco = 0u, idac = 0u, dcoe = 0u;
    out->mhz = mhz;
    out->baseline = channel;
    out->divider = wf_divider(mhz);
    out->result = WF_BASELINE_FAILED;
    if (!phy_tune_channel_2g(channel, &vco, &idac, &dcoe)) return;
    out->vco_before = radio_reg_read(0x65u);
    out->result = retune(mhz, &vco);
    out->vco_after = vco;
    out->frequency_readback = radio_reg_read(0x55u);
    out->divider_readback = ((uint32_t)radio_reg_read(0x4eu) << 16u) |
        radio_reg_read(0x4fu);
    if (out->frequency_readback != mhz || out->divider_readback != out->divider)
        out->result = WF_READBACK_FAILED;
    if (out->result == WF_READBACK_OK) {
        if (!phy_run_dccal(&idac, &dcoe)) out->result = WF_DC_FAILED;
        out->dc_idac = idac;
        out->dc_dcoe = dcoe;
    }
    enforce_tx_inhibit_guards();
    out->phy_guard = (uint32_t)phy_reg_read(0x44cu) |
        ((uint32_t)phy_reg_read(0x44du) << 16u);
    out->radio_guard = (uint32_t)radio_reg_read(0x79u) |
        ((uint32_t)radio_reg_read(0xa4u) << 16u);
    out->maccontrol = mmio_read32(D11_MACCONTROL);
    out->phy_control = phy_reg_read(0x440u);
    out->phy_version = phy_reg_read(0u);
    if (!wf_guards_valid(out->phy_guard, out->radio_guard, out->maccontrol))
        out->result = WF_TX_GUARD_FAILED;
    out->cycles = ARM_CYCLE_COUNTER - start;
}

void cyw43_main(void) {
    MBOX->magic = FW_MAGIC;
    MBOX->fw_state = FW_STATE_PMU_PLL;
    static const uint32_t layout[CLEAN_RX_LAYOUT_WORDS] = CLEAN_RX_LAYOUT_VALUES;
    for (uint32_t i = 0u; i < CLEAN_RX_LAYOUT_WORDS; ++i)
        ((volatile uint32_t *)CLEAN_RX_LAYOUT_ADDR)[i] = layout[i];
    int pmu = pmu_init_pll_and_resources();
    if (pmu != 1) { fault(0x11u, PMU_RES_PENDING, (uint32_t)-pmu); return; }
    MBOX->fw_state = FW_STATE_WAIT_HT;
    __asm__ volatile("dsb" ::: "memory");
    if (!wait_command(1u)) return;
    if ((mmio_read32(CC_CLK_CTL_ST) & 0x30000u) != 0x30000u) {
        fault(0x30u, CC_CLK_CTL_ST, mmio_read32(CC_CLK_CTL_ST));
        return;
    }
    MBOX->fw_state = FW_STATE_COLD_PHY;
    uint32_t lpf = 0u, adc = 0u, dacbuf = 0u, vco = 0u, idac = 0u, dcoe = 0u;
    if (!d11_and_phy_cold_init(&lpf, &adc, &dacbuf, &vco, &idac, &dcoe)) {
        fault(0x51u, D11_PHY_REG_ADDR, 0u);
        return;
    }
    mmio_write32(D11_MACCONTROL, 0x84020400u);
    enforce_tx_inhibit_guards();
    REPORT->magic = WF_MAGIC;
    REPORT->version = WF_VERSION;
    REPORT->generation = MBOX->generation;
    REPORT->lpf = lpf; REPORT->adc = adc; REPORT->dacbuf = dacbuf;
    REPORT->initial_vco = vco;
    MBOX->fw_state = FW_STATE_PSM_READY;
    __asm__ volatile("dsb" ::: "memory");
    if (!wait_command(2u)) return;
    MBOX->fw_state = FW_STATE_RX_ACTIVE;
    __asm__ volatile("dsb" ::: "memory");
    if (!wait_command(WF_COMMAND)) return;
    for (uint32_t i = 0u; i < WF_RECORDS; ++i) {
        probe(&REPORT->records[i], wf_frequency(i));
        REPORT->count = i + 1u;
        if (REPORT->records[i].result != WF_READBACK_OK) {
            REPORT->error = REPORT->records[i].result;
            break;
        }
    }
    enforce_tx_inhibit_guards();
    REPORT->complete = 1u;
    __asm__ volatile("dsb" ::: "memory");
    MBOX->fw_state = FW_STATE_RX_DONE;
}

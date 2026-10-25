#ifndef WATERFALL_PROBE_H
#define WATERFALL_PROBE_H

#include "../../../clean-rx/src/types.h"

#define WF_MAGIC 0x31504657u
#define WF_VERSION 1u
#define WF_ADDRESS 0x20400u
#define WF_COMMAND 0x57504631u
#define WF_RECORDS 31u
#define WF_REPORT_BYTES (64u + WF_RECORDS * 64u)

enum {
    WF_PENDING, WF_READBACK_OK, WF_BASELINE_FAILED, WF_READBACK_FAILED,
    WF_LOCK_NOT_INDICATED, WF_DC_FAILED, WF_TIMER_FAILED, WF_TX_GUARD_FAILED
};

typedef struct {
    uint32_t mhz, baseline, divider, frequency_readback, divider_readback;
    uint32_t vco_before, vco_after, dc_idac, dc_dcoe, result, cycles;
    uint32_t phy_guard, radio_guard, maccontrol, phy_control, phy_version;
} wf_record_t;

typedef struct {
    uint32_t magic, version, generation, count, complete, error;
    uint32_t lpf, adc, dacbuf, initial_vco, reserved[6];
    wf_record_t records[WF_RECORDS];
} wf_report_t;

_Static_assert(sizeof(wf_record_t) == 64u, "Fixed probe record");
_Static_assert(sizeof(wf_report_t) == WF_REPORT_BYTES, "Fixed probe report");
_Static_assert(WF_ADDRESS + WF_REPORT_BYTES < 0x20d80u, "Probe below capture storage");

static inline uint32_t wf_channel_mhz(uint32_t channel) {
    return channel == 14u ? 2484u : 2407u + channel * 5u;
}

static inline uint32_t wf_frequency(uint32_t index) {
    if (index < 14u) return wf_channel_mhz(index + 1u);
    if (index < 30u) return 2300u + (index - 14u) * 20u;
    return 2412u;
}

static inline uint32_t wf_nearest_channel(uint32_t mhz) {
    uint32_t best = 1u, distance = 0xffffffffu;
    for (uint32_t ch = 1u; ch <= 14u; ++ch) {
        uint32_t f = wf_channel_mhz(ch);
        uint32_t d = f > mhz ? f - mhz : mhz - f;
        if (d < distance) { best = ch; distance = d; }
    }
    return best;
}

/* Exact floor(f_MHz * 60 * 65536 / 187), without a target 64-bit divide. */
static inline uint32_t wf_divider(uint32_t mhz) {
    uint32_t scaled = mhz * 60u;
    return ((scaled / 187u) << 16u) + ((scaled % 187u) << 16u) / 187u;
}

static inline int wf_guards_valid(uint32_t phy, uint32_t radio, uint32_t mac) {
    return (phy & 0x40004000u) == 0x00004000u &&
        (radio & 0x00040001u) == 0x00040000u && !(mac & 3u);
}

static inline int wf_record_valid(const wf_record_t *r, uint32_t index) {
    return index < WF_RECORDS && r->mhz == wf_frequency(index) &&
        r->baseline == wf_nearest_channel(r->mhz) && r->divider == wf_divider(r->mhz) &&
        r->frequency_readback == r->mhz && r->divider_readback == r->divider &&
        r->result == WF_READBACK_OK && (r->vco_after & 1u) && r->vco_after != 0xffffu &&
        (r->dc_idac & 0xffu) == 0xffu && (r->dc_dcoe & 0xfffu) == 0xfffu &&
        r->cycles != 0u &&
        wf_guards_valid(r->phy_guard, r->radio_guard, r->maccontrol);
}

#endif

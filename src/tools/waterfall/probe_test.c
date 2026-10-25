#include "probe.h"

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

int main(void) {
    static const uint32_t expected[14] = {
        0x305e75bu, 0x307820du, 0x3091cbfu, 0x30ab771u,
        0x30c5223u, 0x30decd5u, 0x30f8787u, 0x3112239u,
        0x312bcebu, 0x314579du, 0x315f24fu, 0x3178d01u,
        0x31927b3u, 0x31d015eu
    };
    for (uint32_t ch = 1u; ch <= 14u; ++ch) {
        CHECK(wf_divider(wf_channel_mhz(ch)) == expected[ch - 1u]);
        CHECK(wf_frequency(ch - 1u) == wf_channel_mhz(ch));
        CHECK(wf_nearest_channel(wf_channel_mhz(ch)) == ch);
    }
    for (uint32_t f = 2300u; f <= 2600u; ++f) {
        CHECK(wf_divider(f) == (uint32_t)((uint64_t)f * 60u * 65536u / 187u));
        CHECK(wf_nearest_channel(f) >= 1u && wf_nearest_channel(f) <= 14u);
    }
    CHECK(wf_frequency(14u) == 2300u && wf_frequency(29u) == 2600u);
    CHECK(wf_frequency(30u) == 2412u);
    CHECK(wf_guards_valid(0x4000u, 0x00040000u, 0x84020400u));
    CHECK(!wf_guards_valid(0u, 0x00040000u, 0x84020400u));
    CHECK(!wf_guards_valid(0x40004000u, 0x00040000u, 0x84020400u));
    CHECK(!wf_guards_valid(0x4000u, 0x00040001u, 0x84020400u));
    CHECK(!wf_guards_valid(0x4000u, 0u, 0x84020400u));
    CHECK(!wf_guards_valid(0x4000u, 0x00040000u, 0x84020403u));
    wf_record_t r = {0};
    r.mhz = 2412u; r.baseline = 1u; r.divider = wf_divider(r.mhz);
    r.frequency_readback = r.mhz; r.divider_readback = r.divider;
    r.vco_after = 1u; r.dc_idac = 0xffu; r.dc_dcoe = 0xfffu;
    r.result = WF_READBACK_OK; r.cycles = 1u;
    r.phy_guard = 0x4000u; r.radio_guard = 0x40000u; r.maccontrol = 0x84020400u;
    CHECK(wf_record_valid(&r, 0u));
    CHECK(!wf_record_valid(&r, 1u));
    CHECK(!wf_record_valid(&r, WF_RECORDS));
    r.vco_after = 0xffffu; CHECK(!wf_record_valid(&r, 0u)); r.vco_after = 1u;
    r.dc_idac = 0u; CHECK(!wf_record_valid(&r, 0u)); r.dc_idac = 0xffu;
    r.dc_dcoe = 0u; CHECK(!wf_record_valid(&r, 0u)); r.dc_dcoe = 0xfffu;
    r.divider_readback ^= 1u; CHECK(!wf_record_valid(&r, 0u)); r.divider_readback ^= 1u;
    r.frequency_readback++; CHECK(!wf_record_valid(&r, 0u)); r.frequency_readback--;
    r.result = WF_PENDING; CHECK(!wf_record_valid(&r, 0u));
    return 0;
}

#define phy_fast_switch_channel_2g activity_switch_channel
#include "../../../clean-rx/src/main.c"
#undef phy_fast_switch_channel_2g

int phy_fast_switch_channel_2g(uint32_t channel, uint32_t *vco_status_out);

int activity_switch_channel(uint32_t channel, uint32_t *vco_status_out) {
    volatile uint32_t *status = (volatile uint32_t *)0x7f000u;
    int result = phy_fast_switch_channel_2g(channel, vco_status_out);
    status[0] = 0x31414657u;
    status[1] = channel;
    status[2] = (uint32_t)result;
    ++status[3];
    __asm__ volatile("dsb" ::: "memory");
    return result;
}

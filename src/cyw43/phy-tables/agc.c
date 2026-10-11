#include "agc.h"

static const r3_phy_op_t settings[]={
    {R3_RADIO_MOD,0x36u,0x7000u,0u},{R3_RADIO_MOD,0x36u,0x700u,0x400u},{R3_RADIO_MOD,0x36u,0x70u,0x40u},
    {R3_RADIO_MOD,0x36u,7u,0u},{R3_RADIO_MOD,0x35u,0x7000u,0u},{R3_RADIO_MOD,0x35u,0x700u,0x700u},
    {R3_RADIO_MOD,0x35u,0x8000u,0u},{R3_RADIO_MOD,0x35u,0x7eu,0x20u},{R3_RADIO_MOD,0x3au,0x1eu,0x10u},
    {R3_RADIO_MOD,0x3au,1u,0u},{R3_PHY_MOD,0x50au,1u,0u},{R3_PHY_MOD,0x471u,0xfu,4u},
    {R3_PHY_MOD,0x471u,0xf0u,0u},{R3_PHY_MOD,0x41fu,0x7fu,0x41u},{R3_PHY_MOD,0x420u,0xffu,0xcu},
    {R3_PHY_MOD,0x5e0u,0xffu,0u},{R3_PHY_MOD,0x5e0u,0xff00u,0x3000u},{R3_PHY_MOD,0x5e1u,0xffu,0x27u},
    {R3_PHY_MOD,0x5e1u,0xff00u,0x2700u},{R3_PHY_MOD,0x5e2u,0xffu,0x27u},{R3_PHY_MOD,0x5e2u,0xff00u,0x1e00u},
    {R3_PHY_MOD,0x5e3u,0xffu,0x1eu},{R3_PHY_MOD,0x5e3u,0xff00u,0x1e00u},{R3_PHY_MOD,0x5e4u,0xffu,0x1eu},
    {R3_PHY_MOD,0xa96u,0xffu,0x15u},{R3_PHY_MOD,0xa96u,0xff00u,0xc00u},{R3_PHY_MOD,0xa97u,0xffu,0u},
    {R3_PHY_MOD,0x5e5u,0x7fu,0u},{R3_PHY_MOD,0x5e5u,0x3f80u,0x780u},{R3_PHY_MOD,0x5e6u,0x7fu,0x15u},
    {R3_PHY_MOD,0x5e6u,0x3f80u,0u},{R3_PHY_MOD,0x5e7u,0x7fu,0u},{R3_PHY_MOD,0x5e7u,0x3f80u,0x980u},
    {R3_PHY_MOD,0x5e8u,0x7fu,0u},{R3_PHY_MOD,0x5e8u,0x3f80u,0u},{R3_PHY_MOD,0x5e9u,0x7fu,0u},
    {R3_PHY_MOD,0x5e9u,0x3f80u,0x900u},{R3_PHY_MOD,0xa91u,0x7fu,0x16u},{R3_PHY_MOD,0xa91u,0x3f80u,0xc80u},
    {R3_PHY_MOD,0x5fau,0xffu,0x36u},{R3_PHY_MOD,0x5f5u,0xffu,0u},{R3_PHY_MOD,0x5f5u,0xff00u,0x2d00u},
    {R3_PHY_MOD,0x5f6u,0xffu,0x24u},{R3_PHY_MOD,0x5f6u,0xff00u,0x2400u},{R3_PHY_MOD,0x5f7u,0xffu,0x24u},
    {R3_PHY_MOD,0x5f7u,0xff00u,0x2400u},{R3_PHY_MOD,0x5f8u,0xffu,0x24u},{R3_PHY_MOD,0x5f8u,0xff00u,0x2400u},
    {R3_PHY_MOD,0x5f9u,0xffu,0x24u},{R3_PHY_MOD,0xa8fu,0xffu,0x1bu},{R3_PHY_MOD,0xa8fu,0xff00u,0x1200u},
    {R3_PHY_MOD,0xa90u,0xffu,6u},{R3_PHY_MOD,0x5ffu,0x3f80u,0u},{R3_PHY_MOD,0x5d1u,0x7fu,0x28u},
    {R3_PHY_MOD,0x5d1u,0x3f80u,0x1100u},{R3_PHY_MOD,0x5d2u,0x7fu,0u},{R3_PHY_MOD,0x5d2u,0x3f80u,0u},
    {R3_PHY_MOD,0x5d3u,0x7fu,0u},{R3_PHY_MOD,0x5d3u,0x3f80u,0u},{R3_PHY_MOD,0x5d4u,0x7fu,0u},
    {R3_PHY_MOD,0x5d4u,0x3f80u,0u},{R3_PHY_MOD,0xa94u,0x7fu,5u},{R3_PHY_MOD,0xa94u,0x3f80u,0x500u},
    {R3_PHY_MOD,0xa95u,0x7fu,0x12u},{R3_PHY_MOD,0x5f9u,0xff00u,0x3000u},{R3_PHY_MOD,0x5edu,0xff00u,0xfa00u},
    {R3_PHY_MOD,0x5e4u,0xf00u,0x100u},{R3_PHY_MOD,0x565u,0x18u,0x10u},{R3_PHY_MOD,0x30u,7u,7u},
    {R3_PHY_MOD,0x471u,0xff00u,0x4b00u},{R3_PHY_MOD,0x5eau,0x3fu,0xfffau},{R3_PHY_MOD,0x5eau,0xff00u,0xf700u},
    {R3_PHY_MOD,0x437u,0xffu,0u},{R3_PHY_MOD,0x429u,0x7c00u,0x4800u},{R3_PHY_MOD,0x472u,0xffu,0x51u},
    {R3_PHY_MOD,0x542u,0xff00u,0x5400u},{R3_PHY_MOD,0x534u,0xffu,0x17u},{R3_PHY_MOD,0x5d5u,0xffu,0u},
    {R3_PHY_MOD,0x550u,0x3ffu,0x169u},{R3_PHY_MOD,0xa52u,0x1fe0u,0x40u},{R3_PHY_MOD,0xa53u,0xffu,2u},
    {R3_PHY_MOD,0xa58u,0xfu,1u},{R3_PHY_MOD,0x542u,0xffu,0x2eu},{R3_PHY_MOD,0x441u,0xff00u,0u},
    {R3_PHY_MOD,0x442u,0xffu,0x62u},{R3_PHY_MOD,0x5d5u,0xff00u,0x5f00u},{R3_PHY_MOD,0xa28u,0xff00u,0x5f00u},
    {R3_PHY_MOD,0xa28u,0xffu,0x5fu},{R3_PHY_MOD,0xa29u,0xff00u,0x5f00u},{R3_PHY_MOD,0xa29u,0xffu,0x5fu},
    {R3_PHY_MOD,0xa55u,0xffu,0x5eu}
};
static const r3_phy_op_t aci[]={
    {R3_PHY_MOD,0xb00u,1u,0u},{R3_PHY_MOD,0xb00u,2u,0u},{R3_PHY_MOD,0xb00u,4u,0u},
    {R3_PHY_MOD,0xb00u,8u,0u},{R3_PHY_MOD,0xb00u,0xf0u,0xf0u},{R3_PHY_MOD,0xb00u,0xf00u,0xf00u},
    {R3_PHY_MOD,0xb00u,0x1000u,0u},{R3_PHY_MOD,0xb07u,1u,0u},{R3_PHY_MOD,0xb07u,6u,4u},
    {R3_PHY_MOD,0xb07u,0x38u,8u},{R3_PHY_MOD,0xb07u,0x1c0u,0u},{R3_PHY_MOD,0xb0bu,0x30u,0x10u},
    {R3_PHY_MOD,0xb0bu,0x40u,0x40u},{R3_PHY_MOD,0xb0bu,0x780u,0x400u},{R3_PHY_MOD,0xb0bu,0x7800u,0x2000u},
    {R3_PHY_MOD,0xb0cu,0xffffu,0x1000u},{R3_PHY_MOD,0xb0du,0xffffu,0xd000u},{R3_PHY_MOD,0xb0eu,0xffffu,0xf800u},
    {R3_PHY_MOD,0xb0fu,0xffffu,0x10u},{R3_PHY_MOD,0xb10u,0xffffu,0xf800u},{R3_PHY_MOD,0xb11u,0xffffu,0xfd00u},
    {R3_PHY_MOD,0xb12u,0xfu,0u},{R3_PHY_MOD,0xb12u,0xf0u,0u},{R3_PHY_MOD,0xb12u,0xf00u,0u},
    {R3_PHY_MOD,0xb13u,0xfu,0u},{R3_PHY_MOD,0xb13u,0xf0u,0u},{R3_PHY_MOD,0xb13u,0xf00u,0u},
    {R3_PHY_MOD,0xb14u,0xfu,0u},{R3_PHY_MOD,0xb14u,0xf0u,0u},{R3_PHY_MOD,0xb14u,0xf00u,0u},
    {R3_PHY_MOD,0xb15u,0xfu,0u},{R3_PHY_MOD,0xb15u,0xf0u,0u},{R3_PHY_MOD,0xb15u,0xf00u,0u},
    {R3_PHY_MOD,0xb16u,3u,1u},{R3_PHY_MOD,0xb16u,0xcu,0u},{R3_PHY_MOD,0xb16u,0x30u,0x10u},
    {R3_PHY_MOD,0xb16u,0xc0u,0x40u},{R3_PHY_MOD,0xb16u,0x300u,0u},{R3_PHY_MOD,0xb16u,0xc00u,0x400u},
    {R3_PHY_MOD,0xb17u,3u,0u},{R3_PHY_MOD,0xb17u,0xcu,0xfffcu},{R3_PHY_MOD,0xb17u,0x30u,0xfff0u},
    {R3_PHY_MOD,0xb17u,0xc0u,0u},{R3_PHY_MOD,0xb17u,0x300u,0xff00u},{R3_PHY_MOD,0xb17u,0xc00u,0xfc00u},
    {R3_PHY_MOD,0xb06u,1u,0u},{R3_PHY_MOD,0xb05u,0x1fu,0x1fu},{R3_PHY_MOD,0xb04u,0xffffu,0xffffu},
    {R3_PHY_MOD,0xb08u,0xffffu,0xffffu},{R3_PHY_MOD,0xb09u,0xffffu,0x7fffu},{R3_PHY_MOD,0xb0au,0xffffu,8u},
    {R3_PHY_MOD,0xb02u,0xffffu,0x3fu},{R3_PHY_MOD,0xb03u,0xffffu,0xffu},{R3_PHY_MOD,0xb01u,0xfu,0xfu},
    {R3_PHY_MOD,0xb01u,0xf0u,0xf0u},{R3_PHY_MOD,0xb00u,1u,1u}
};

static void clear(r3_agc_report_t *r) {
    volatile uint8_t *bytes=(volatile uint8_t *)r;
    for(uint32_t i=0u;i<sizeof(*r);++i)bytes[i]=0u;
}
static r3_agc_error_t fail(r3_agc_report_t *r,r3_agc_error_t error) {
    r->error=error;return error;
}
static int check_guard(r3_tx_guard_t *g,r3_agc_report_t *r) {
    uint32_t before=g->completed_io;
    r3_tx_guard_error_t error=r3_tx_guard_check(g);
    r->io_completed+=g->completed_io-before;
    if(error){fail(r,R3_AGC_GUARD);return 0;}
    ++r->guard_checks;return 1;
}
static int begin(r3_tx_guard_t *g,r3_agc_report_t *r) {
    if(!g){fail(r,R3_AGC_ARGUMENT);return 0;}
    if(g->phase!=R3_TG_ARMED||g->pa_phase!=R3_TG_PA_ARMED) {
        r->reset_required=g->phase==R3_TG_FAULT;
        fail(r,R3_AGC_GUARD);return 0;
    }
    if(g->bus.context!=g||!g->bus.read||!g->bus.write||g->bus.base!=R3_PHY_REGISTER_BASE||
       !g->raw.read||!g->raw.write||g->raw.base!=R3_PHY_REGISTER_BASE) {
        fail(r,R3_AGC_ARGUMENT);return 0;
    }
    r->reset_required=1u;return check_guard(g,r);
}
static int operations(r3_tx_guard_t *g,const r3_phy_op_t *ops,uint32_t count,r3_agc_report_t *r) {
    for(uint32_t i=0u;i<count;++i) {
        int radio=ops[i].kind==R3_RADIO_MOD;
        r3_phy_error_t error=radio?r3_radio_execute(&g->bus,&ops[i],&r->access):
                                  r3_phy_execute(&g->bus,&ops[i],&r->access);
        r->io_completed+=r->access.io_completed;
        if(error){fail(r,radio?R3_AGC_RADIO_IO:R3_AGC_PHY_IO);return 0;}
        if(radio)++r->radio_operations;else ++r->phy_operations;
        if(!radio&&ops[i].index==0x44au&&!check_guard(g,r))return 0;
    }
    return 1;
}
static r3_agc_error_t complete(r3_tx_guard_t *g,r3_agc_report_t *r) {
    if(!check_guard(g,r))return r->error;
    r->phase=R3_AGC_CONFIGURED;r->reset_required=0u;return R3_AGC_OK;
}
r3_agc_error_t r3_rx_agc_configure(r3_tx_guard_t *g,const r3_rx_agc_config_t *config,r3_agc_report_t *r) {
    static const r3_phy_op_t alternate[]={
        {R3_PHY_MOD,0x9b3u,0x100u,0x100u},{R3_PHY_MOD,0x655u,0xf000u,0x5000u},
        {R3_PHY_MOD,0x655u,0xfffu,0u}
    };
    static const r3_phy_op_t aci_threshold[]={
        {R3_PHY_MOD,0xa57u,0xffu,97u},{R3_PHY_MOD,0xa57u,0xff00u,0x6100u}
    };
    static const r3_phy_op_t reset[]={
        {R3_PHY_WRITE,0xa00u,0u,0u},{R3_PHY_MOD,0x440u,1u,0u},
        {R3_PHY_OR,0x44au,0u,0x44u},{R3_PHY_WRITE,0x44au,0u,0x80u},
        {R3_PHY_WRITE,0xa00u,0u,0xffu},{R3_PHY_MOD,0x440u,1u,1u}
    };
    if(!r)return R3_AGC_ARGUMENT;
    clear(r);
    if(!config)return fail(r,R3_AGC_ARGUMENT);
    const r3_rx_agc_config_t c=*config;
    if(c.alternate_gain_settings>1u||c.threshold_104>1u||c.aci_threshold_reset>1u||
       c.gain_787_high>255u||c.gain_432_high>255u)return fail(r,R3_AGC_ARGUMENT);
    const r3_phy_op_t branch={R3_PHY_MOD,0xa4fu,0xffu,c.alternate_gain_settings?97u:91u};
    uint32_t threshold=c.threshold_104?104u:97u;
    const r3_phy_op_t thresholds[]={
        {R3_PHY_MOD,0xa57u,0xff00u,threshold<<8u},{R3_PHY_MOD,0xa57u,0xffu,threshold}
    };
    const r3_phy_op_t gains[]={
        {R3_PHY_MOD,0x788u,0x80u,0x80u},{R3_PHY_MOD,0x788u,2u,0u},
        {R3_PHY_MOD,0x787u,0xff00u,c.gain_787_high<<8u},
        {R3_PHY_MOD,0x432u,0xff00u,c.gain_432_high<<8u}
    };
    if(!begin(g,r))return r->error;
    r->phase=R3_AGC_SETTINGS;
    if(!operations(g,settings,sizeof(settings)/sizeof(settings[0]),r)||
       !operations(g,&branch,1u,r))return r->error;
    if(c.alternate_gain_settings&&!operations(g,alternate,3u,r))return r->error;
    r->phase=R3_AGC_THRESHOLD;
    if(!operations(g,thresholds,2u,r))return r->error;
    r->phase=R3_AGC_GAIN;
    if(!operations(g,gains,4u,r))return r->error;
    r->phase=R3_AGC_ACI;
    if(c.aci_threshold_reset&&!operations(g,aci_threshold,2u,r))return r->error;
    if(!operations(g,aci,sizeof(aci)/sizeof(aci[0]),r))return r->error;
    r->phase=R3_AGC_RESET;
    if(!check_guard(g,r)||!operations(g,reset,6u,r))return r->error;
    return complete(g,r);
}
r3_agc_error_t r3_rx_deaf_set(r3_tx_guard_t *g,uint32_t deaf,r3_agc_report_t *r) {
    if(!r)return R3_AGC_ARGUMENT;
    clear(r);
    if(deaf>1u)return fail(r,R3_AGC_ARGUMENT);
    if(!begin(g,r))return r->error;
    r->phase=R3_AGC_DEAF;
    const r3_phy_op_t op={R3_PHY_MOD,0x440u,1u,!deaf};
    if(!operations(g,&op,1u,r))return r->error;
    return complete(g,r);
}
r3_agc_error_t r3_rx_finalize(r3_tx_guard_t *g,r3_agc_report_t *r) {
    static const r3_phy_op_t ops[]={
        {R3_PHY_MOD,0x440u,0x200u,0x200u},{R3_PHY_MOD,0x805u,0x10u,0x10u},
        {R3_PHY_MOD,0x304u,1u,1u},{R3_PHY_MOD,0x315u,0xfu,0xau},
        {R3_PHY_MOD,0x327u,0xfffu,0xfffu}
    };
    if(!r)return R3_AGC_ARGUMENT;
    clear(r);
    if(!begin(g,r))return r->error;
    r->phase=R3_AGC_FINALIZE;
    if(!operations(g,ops,5u,r))return r->error;
    return complete(g,r);
}

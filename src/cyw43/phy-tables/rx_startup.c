#include "rx_startup.h"

static r3_rx_path_error_t fail(r3_rx_path_report_t *r,r3_rx_path_error_t error) {
    r->error=error;return error;
}
static int check_guard(r3_tx_guard_t *g,r3_rx_path_report_t *r) {
    uint32_t before=g->completed_io;
    r3_tx_guard_error_t error=r3_tx_guard_check(g);
    r->io_completed+=g->completed_io-before;
    if(error){fail(r,R3_RX_PATH_GUARD);return 0;}
    ++r->guard_checks;return 1;
}
static int operation(r3_tx_guard_t *g,const r3_phy_op_t *op,r3_rx_path_report_t *r) {
    int radio=op->kind==R3_RADIO_MOD;
    r3_phy_error_t error=radio?r3_radio_execute(&g->bus,op,&r->access):
                              r3_phy_execute(&g->bus,op,&r->access);
    r->io_completed+=r->access.io_completed;
    if(error){fail(r,radio?R3_RX_PATH_RADIO_IO:R3_RX_PATH_PHY_IO);return 0;}
    if(radio)++r->radio_operations;else ++r->phy_operations;
    return op->index!=0x44au||radio||check_guard(g,r);
}
static int operations(r3_tx_guard_t *g,const r3_phy_op_t *ops,uint32_t count,r3_rx_path_report_t *r) {
    for(uint32_t i=0u;i<count;++i)if(!operation(g,&ops[i],r))return 0;
    return 1;
}
static int table(r3_tx_guard_t *g,uint32_t id,uint32_t count,uint32_t width,
                 const uint32_t *data,r3_rx_path_report_t *r) {
    const r3_table_request_t request={id,0u,count,width};
    r3_table_error_t error=r3_phy_table_write(&g->bus,&request,data,count,&r->table);
    r->io_completed+=r->table.io_completed;r->entries_completed+=r->table.entries_completed;
    if(error){fail(r,R3_RX_PATH_TABLE_IO);return 0;}
    ++r->table_operations;return 1;
}
static int config_result(r3_rx_config_error_t error,r3_rx_path_report_t *r) {
    r->io_completed+=r->config.io_completed;r->phy_operations+=r->config.phy_operations;
    r->table_operations+=r->config.table_operations;r->entries_completed+=r->config.entries_completed;
    if(error){fail(r,R3_RX_PATH_CONFIG);return 0;}
    return 1;
}
r3_rx_path_error_t r3_rx_radio_reset(r3_tx_guard_t *g,r3_rx_path_report_t *r) {
    if(!r)return R3_RX_PATH_ARGUMENT;
    volatile uint8_t *bytes=(volatile uint8_t *)r;
    for(uint32_t i=0u;i<sizeof(*r);++i)bytes[i]=0u;
    if(!g)return fail(r,R3_RX_PATH_ARGUMENT);
    if(g->phase!=R3_TG_ARMED||g->pa_phase!=R3_TG_PA_UNBOUND) {
        r->reset_required=g->phase==R3_TG_FAULT;
        return fail(r,R3_RX_PATH_GUARD);
    }
    r->reset_required=1u;
    if(!check_guard(g,r))return r->error;
    r->phase=R3_RX_PATH_RESET;
    r3_phy_error_t error=r3_phy_pulse_044a(&g->bus,&r->access);
    r->io_completed+=r->access.io_completed;r->phy_operations=r->access.operations;
    if(error)return fail(r,R3_RX_PATH_PHY_IO);
    if(!check_guard(g,r))return r->error;
    return R3_RX_PATH_OK;
}
static r3_rx_path_error_t defaults_apply(r3_tx_guard_t *g,r3_rx_path_report_t *r,
                                       uint32_t band,uint32_t defaults) {
    static const r3_phy_op_t ops[]={
        {R3_PHY_MOD,0x448u,0x8000u,0x8000u},{R3_PHY_MOD,0x408u,0x100u,0u},
        {R3_PHY_MOD,0x440u,1u,0u},{R3_PHY_WRITE,0x44au,0u,0x4fu},
        {R3_PHY_WRITE,0x43au,0u,0u},{R3_PHY_WRITE,0x575u,0u,0u},
        {R3_PHY_WRITE,0x44cu,0u,0x4000u},{R3_PHY_WRITE,0x4b0u,0u,0u},
        {R3_PHY_WRITE,0x4f9u,0u,0u},{R3_PHY_WRITE,0x938u,0u,0x800u},
        {R3_PHY_WRITE,0x53du,0u,0u},{R3_PHY_WRITE,0x571u,0u,0u},
        {R3_PHY_WRITE,0x573u,0u,0u},{R3_PHY_WRITE,0x44eu,0u,0u},
        {R3_PHY_MOD,0x534u,0xffu,0x12u},{R3_PHY_MOD,0x4e4u,0xff00u,0x600u},
        {R3_PHY_MOD,0x44du,0x200u,0u},{R3_PHY_MOD,0x44cu,0x200u,0x200u},
        {R3_PHY_MOD,0x536u,0x10u,0x10u},{R3_PHY_MOD,0x440u,1u,1u},
        {R3_PHY_WRITE,0x44au,0u,0u},{R3_PHY_MOD,0x655u,0xf000u,0x8000u},
        {R3_PHY_MOD,0xadau,0x4000u,0x4000u}
    };
    if(!r)return R3_RX_PATH_ARGUMENT;
    volatile uint8_t *bytes=(volatile uint8_t *)r;
    for(uint32_t i=0u;i<sizeof(*r);++i)bytes[i]=0u;
    if(!g)return fail(r,R3_RX_PATH_ARGUMENT);
    if(g->phase!=R3_TG_ARMED||g->pa_phase!=R3_TG_PA_ARMED) {
        r->reset_required=g->phase==R3_TG_FAULT;
        return fail(r,R3_RX_PATH_GUARD);
    }
    r->reset_required=1u;
    if(!check_guard(g,r))return r->error;
    r->phase=defaults?R3_RX_PATH_DEFAULTS:R3_RX_PATH_BAND;
    uint32_t end=defaults?sizeof(ops)/sizeof(ops[0]):2u;
    for(uint32_t i=band?0u:2u;i<end;++i) {
        if(!operation(g,&ops[i],r))return r->error;
        if(ops[i].kind==R3_PHY_WRITE&&(ops[i].index==0x44cu||ops[i].index==0x938u))
            ++r->explicit_policy_writes;
    }
    if(!check_guard(g,r))return r->error;
    r->phase=R3_RX_PATH_CONFIGURED;r->reset_required=0u;
    return R3_RX_PATH_OK;
}
r3_rx_path_error_t r3_rx_defaults_apply(r3_tx_guard_t *g,r3_rx_path_report_t *r) {
    return defaults_apply(g,r,1u,1u);
}
r3_rx_path_error_t r3_rx_band_apply(r3_tx_guard_t *g,r3_rx_path_report_t *r) {
    return defaults_apply(g,r,1u,0u);
}
r3_rx_path_error_t r3_rx_register_defaults_apply(r3_tx_guard_t *g,r3_rx_path_report_t *r) {
    return defaults_apply(g,r,0u,1u);
}
r3_rx_path_error_t r3_rx_path_configure(r3_tx_guard_t *g,uint32_t channel,r3_rx_path_report_t *r) {
    static const r3_phy_op_t power[]={
        {R3_PHY_MOD,0x53bu,0x100u,0x100u},{R3_PHY_MOD,0x53bu,0x200u,0x200u},
        {R3_PHY_MOD,0x53bu,0x400u,0x400u},{R3_PHY_MOD,0x4b1u,4u,4u},
        {R3_RADIO_MOD,0x19u,1u,1u},{R3_RADIO_MOD,0x9bu,2u,2u},
        {R3_RADIO_MOD,0x1cu,1u,1u},{R3_RADIO_MOD,0x17u,0x4000u,0x4000u},
        {R3_RADIO_MOD,0x9au,1u,1u},{R3_RADIO_MOD,0x3du,0x4000u,0x4000u},
        {R3_RADIO_MOD,0x9fu,0x20u,0x20u},{R3_RADIO_MOD,0x3eu,0x10u,0u},
        {R3_RADIO_MOD,0x9fu,2u,2u},{R3_RADIO_MOD,0x3eu,0x20u,0x20u},
        {R3_RADIO_MOD,0x9fu,4u,4u},{R3_RADIO_MOD,0x26u,0x1000u,0x1000u},
        {R3_RADIO_MOD,0x9cu,4u,4u},{R3_PHY_MOD,0x4b1u,4u,4u},
        {R3_PHY_MOD,0x9a5u,0x40u,0x40u},{R3_PHY_MOD,0x463u,0xff00u,0x7200u}
    };
    static const r3_phy_op_t dcoe[]={
        {R3_PHY_MOD,0x976u,0xfffu,0x20u},{R3_PHY_MOD,0x966u,0xfff7u,0x8985u},
        {R3_PHY_MOD,0x967u,0xffu,0x19u}
    };
    static const r3_phy_op_t idacc[]={
        {R3_PHY_MOD,0x96eu,0xfffu,0u},{R3_PHY_MOD,0x965u,4u,4u},
        {R3_PHY_MOD,0x968u,0xfff7u,0x5d86u},{R3_PHY_MOD,0x967u,0xff00u,0x2800u},
        {R3_PHY_MOD,0x969u,0xffu,1u}
    };
    static const r3_phy_op_t dc_tail[]={
        {R3_PHY_MOD,0x971u,0xffu,1u},{R3_PHY_MOD,0x965u,8u,8u},
        {R3_PHY_MOD,0x4b1u,1u,0u},{R3_PHY_MOD,0x4b0u,7u,0u},
        {R3_RADIO_MOD,0x32u,0x7eu,4u},{R3_PHY_MOD,0x96bu,0xff00u,0x2800u},
        {R3_PHY_MOD,0x96du,0x3fu,0x18u}
    };
    static const r3_phy_op_t reset[]={
        {R3_PHY_WRITE,0xa00u,0u,0u},{R3_PHY_MOD,0x440u,1u,0u},
        {R3_PHY_OR,0x44au,0u,0x44u},{R3_PHY_WRITE,0x44au,0u,0x80u},
        {R3_PHY_WRITE,0xa00u,0u,0xffu},{R3_PHY_MOD,0x440u,1u,1u}
    };
    static const r3_phy_op_t tail[]={
        {R3_RADIO_MOD,0x30u,0x3fu,8u},{R3_RADIO_MOD,0x31u,0x7eu,0x10u},
        {R3_RADIO_MOD,0x39u,0xfu,0u}
    };
    static const uint32_t zero[12]={0u};
    static const uint32_t gmap[8]={0x28u,0x128u,0x138u,0x148u,0x158u,0x155u,0x150u,0x350u};
    if(!r)return R3_RX_PATH_ARGUMENT;
    volatile uint8_t *bytes=(volatile uint8_t *)r;
    for(uint32_t i=0u;i<sizeof(*r);++i)bytes[i]=0u;
    if(!g||channel<1u||channel>14u)return fail(r,R3_RX_PATH_ARGUMENT);
    if(g->phase!=R3_TG_ARMED) {
        r->reset_required=g->phase==R3_TG_FAULT;
        return fail(r,R3_RX_PATH_GUARD);
    }
    r->reset_required=1u;
    if(!check_guard(g,r))return r->error;
    r->phase=R3_RX_PATH_TIMING;
    if(!config_result(r3_rx_timing_apply(&g->bus,channel,&r->config),r))return r->error;
    r->phase=R3_RX_PATH_RFSEQ;
    if(!config_result(r3_rx_rfseq_write(&g->bus,&r->config),r))return r->error;
    r->phase=R3_RX_PATH_POWER;
    if(!operations(g,power,sizeof(power)/sizeof(power[0]),r))return r->error;
    r->phase=R3_RX_PATH_DC;
    if(!operations(g,dcoe,3u,r)||!table(g,34u,12u,32u,zero,r)||
       !operations(g,idacc,5u,r)||!table(g,36u,8u,16u,gmap,r)||
       !table(g,35u,8u,32u,zero,r)||!operations(g,dc_tail,7u,r))return r->error;
    r->phase=R3_RX_PATH_RESET;
    if(!operations(g,reset,6u,r))return r->error;
    r->phase=R3_RX_PATH_TAIL;
    if(!operations(g,tail,3u,r)||!check_guard(g,r))return r->error;
    r->phase=R3_RX_PATH_CONFIGURED;r->reset_required=0u;
    return R3_RX_PATH_OK;
}

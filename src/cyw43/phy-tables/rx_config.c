#include "rx_config.h"

static void clear_report(r3_rx_config_report_t *report) {
    volatile uint8_t *p=(volatile uint8_t *)report;
    for(uint32_t i=0u;i<sizeof(*report);++i)p[i]=0u;
}
static int bus_valid(const r3_phy_bus_t *bus) {
    return bus && bus->base==R3_PHY_REGISTER_BASE && bus->read && bus->write;
}
static r3_rx_config_error_t fail(r3_rx_config_report_t *r,r3_rx_config_error_t error) {
    r->error=error;return error;
}
static uint32_t gcd(uint32_t a,uint32_t b) {
    while(b) { uint32_t next=a%b;a=b;b=next; }
    return a;
}
r3_rx_config_error_t r3_rx_timing_plan(uint32_t channel,r3_rx_timing_t *out) {
    if(!out||channel<1u||channel>14u)return R3_RX_CONFIG_ARGUMENT;
    uint32_t frequency=channel==14u?2484u:2407u+5u*channel;
    uint32_t mu=(frequency*65536u+2u)/5u,period=1280u/gcd(1280u,frequency);
    out->channel=channel;out->frequency_mhz=frequency;out->farrow_mu=mu;out->drift_period=period;
    out->operations[0]=(r3_phy_op_t){R3_PHY_MOD,0x9adu,0xffffu,mu&0xffffu};
    out->operations[1]=(r3_phy_op_t){R3_PHY_MOD,0x9aeu,0x03ffu,mu>>16u};
    out->operations[2]=(r3_phy_op_t){R3_PHY_MOD,0x9aeu,0x4000u,0u};
    out->operations[3]=(r3_phy_op_t){R3_PHY_MOD,0x9b1u,0x1fffu,period};
    out->operations[4]=(r3_phy_op_t){R3_PHY_MOD,0x9acu,0x0400u,0x0400u};
    out->operations[5]=(r3_phy_op_t){R3_PHY_MOD,0x657u,0x0fffu,((frequency*4u/5u)+1u)/2u};
    out->operations[6]=(r3_phy_op_t){R3_PHY_MOD,0x658u,0x0fffu,((5242880u/frequency)+1u)/2u};
    return R3_RX_CONFIG_OK;
}
static int phy_operation(const r3_phy_bus_t *bus,const r3_phy_op_t *op,r3_rx_config_report_t *r) {
    r3_phy_error_t error=r3_phy_execute(bus,op,&r->phy);
    r->io_completed+=r->phy.io_completed;
    if(error) {
        r->reset_required=1u;fail(r,R3_RX_CONFIG_PHY_IO);return 0;
    }
    ++r->phy_operations;return 1;
}
static int table_write(const r3_phy_bus_t *bus,uint32_t id,uint32_t offset,uint32_t width,
                        const uint32_t *data,uint32_t count,r3_rx_config_report_t *r) {
    const r3_table_request_t request={id,offset,count,width};
    r3_table_error_t error=r3_phy_table_write(bus,&request,data,count,&r->table);
    r->io_completed+=r->table.io_completed;r->entries_completed+=r->table.entries_completed;
    if(error) {
        r->reset_required=1u;fail(r,R3_RX_CONFIG_TABLE_IO);return 0;
    }
    ++r->table_operations;return 1;
}
r3_rx_config_error_t r3_rx_timing_apply(const r3_phy_bus_t *bus,uint32_t channel,r3_rx_config_report_t *r) {
    if(!r)return R3_RX_CONFIG_ARGUMENT;
    clear_report(r);
    if(!bus_valid(bus))return fail(r,R3_RX_CONFIG_ARGUMENT);
    r3_rx_timing_t plan;
    if(r3_rx_timing_plan(channel,&plan))return fail(r,R3_RX_CONFIG_ARGUMENT);
    r->reset_required=1u;
    for(uint32_t i=0u;i<R3_RX_TIMING_OPS;++i)if(!phy_operation(bus,&plan.operations[i],r))return r->error;
    r->reset_required=0u;
    return R3_RX_CONFIG_OK;
}
r3_rx_config_error_t r3_rx_rfseq_write(const r3_phy_bus_t *bus,r3_rx_config_report_t *r) {
    static const uint32_t initial[19]={10u,5u,8u,1600u,8u,5u,8u,8u,119u,8u,138u,8u,1u,5u,1u,10u,1u,5u,1600u};
    static const uint32_t turnaround[3]={60u,1u,8u};
    const r3_phy_op_t clear={R3_PHY_WRITE,0x9cfu,0u,0u};
    if(!r)return R3_RX_CONFIG_ARGUMENT;
    clear_report(r);
    if(!bus_valid(bus))return fail(r,R3_RX_CONFIG_ARGUMENT);
    r->reset_required=1u;
    if(!phy_operation(bus,&clear,r)||!table_write(bus,8u,3u,16u,initial,19u,r)||
       !table_write(bus,8u,29u,16u,turnaround,3u,r))return r->error;
    r->reset_required=0u;
    return R3_RX_CONFIG_OK;
}
r3_rx_config_error_t r3_rx_gains_write(const r3_phy_bus_t *bus,const r3_rx_gains_t *g,r3_rx_config_report_t *r) {
    if(!r)return R3_RX_CONFIG_ARGUMENT;
    clear_report(r);
    if(!bus_valid(bus)||!g||!g->gainval||!g->gain||!g->gainidx||
       g->gainval_capacity<R3_RX_GAINVAL_COUNT||g->gain_capacity<R3_RX_GAIN_COUNT||
       g->gainidx_capacity<R3_RX_GAINIDX_COUNT)return fail(r,R3_RX_CONFIG_ARGUMENT);
    for(uint32_t i=0u;i<R3_RX_GAINVAL_COUNT;++i)if(g->gainval[i]>0xffu)return fail(r,R3_RX_CONFIG_VALUE);
    r->reset_required=1u;
    if(!table_write(bus,17u,0u,8u,g->gainval,R3_RX_GAINVAL_COUNT,r)||
       !table_write(bus,18u,0u,32u,g->gain,R3_RX_GAIN_COUNT,r)||
       !table_write(bus,13u,0u,32u,g->gainidx,R3_RX_GAINIDX_COUNT,r))return r->error;
    r->reset_required=0u;
    return R3_RX_CONFIG_OK;
}
r3_rx_config_error_t r3_rx_common_write(const r3_phy_bus_t *bus,const r3_rx_common_t *c,r3_rx_config_report_t *r) {
    static const uint32_t ids[5]={2u,1u,20u,23u,0u};
    static const uint32_t widths[5]={16u,16u,8u,16u,16u};
    static const uint32_t counts[5]={128u,64u,256u,154u,108u};
    if(!r)return R3_RX_CONFIG_ARGUMENT;
    clear_report(r);
    if(!bus_valid(bus)||!c)return fail(r,R3_RX_CONFIG_ARGUMENT);
    for(uint32_t i=0u;i<R3_RX_COMMON_TABLES;++i) {
        if(!c->data[i]||c->capacity[i]<counts[i])return fail(r,R3_RX_CONFIG_ARGUMENT);
        uint32_t maximum=widths[i]==8u?0xffu:0xffffu;
        for(uint32_t j=0u;j<counts[i];++j)
            if(c->data[i][j]>maximum)return fail(r,R3_RX_CONFIG_VALUE);
    }
    r->reset_required=1u;
    for(uint32_t i=0u;i<R3_RX_COMMON_TABLES;++i)
        if(!table_write(bus,ids[i],0u,widths[i],c->data[i],counts[i],r))return r->error;
    r->reset_required=0u;return R3_RX_CONFIG_OK;
}

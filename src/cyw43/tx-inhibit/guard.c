#include "guard.h"

static uint32_t protected_mask(uint32_t index) {
    if(index==0x44cu||index==0x44du)return 0x4000u;
    if(index==0x938u||index==0x939u)return 0x800u;
    return 0u;
}
static int desired(uint32_t index,uint32_t value) {
    uint32_t mask=protected_mask(index);
    return (value&mask)==((index==0x44cu||index==0x938u)?mask:0u);
}
static int radio_desired(uint32_t index,uint32_t value) {
    if(index==0x79u)return !(value&1u);
    if(index==0xa4u)return (value&4u)!=0u;
    return 1;
}
static int fail(r3_tx_guard_t *g,r3_tx_guard_error_t error) {
    if(g->phase!=R3_TG_FAULT)g->error=error;
    g->phase=R3_TG_FAULT;return 0;
}
static int writable(r3_tx_guard_t *g,uint32_t index,uint32_t value) {
    return desired(index,value)?1:fail(g,R3_TG_DEFEATING_WRITE);
}
static int guarded_write(void *context,uint32_t address,uint32_t width,uint32_t value) {
    r3_tx_guard_t *g=context;
    if(g->phase==R3_TG_FAULT)return 0;
    g->last_address=address;g->last_value=value;
    if(address==R3_PHY_REGISTER_BASE+0x3fcu) {
        if(width==4u) {
            uint32_t index=value&0xffffu;
            if(index>0x1fffu)return fail(g,R3_TG_SELECTOR);
            if(!writable(g,index,value>>16u))return 0;
            g->have_phy=0u;
        } else if(width==2u&&value<=0x1fffu) {
            g->phy_selector=value;g->have_phy=1u;
        } else return fail(g,R3_TG_SELECTOR);
    } else if(address==R3_PHY_REGISTER_BASE+0x3feu) {
        if(width!=2u||value>0xffffu||!g->have_phy)return fail(g,R3_TG_SELECTOR);
        if(!writable(g,g->phy_selector,value))return 0;
    } else if(address==R3_PHY_REGISTER_BASE+0x3d8u) {
        if(width!=2u||value>0x1fffu||value==1u)return fail(g,R3_TG_SELECTOR);
        g->radio_selector=value;g->have_radio=1u;
    } else if(address==R3_PHY_REGISTER_BASE+0x3dau) {
        if(width!=2u||value>0xffffu||!g->have_radio)return fail(g,R3_TG_SELECTOR);
        if(g->pa_phase!=R3_TG_PA_UNBOUND&&!radio_desired(g->radio_selector,value))
            return fail(g,R3_TG_DEFEATING_WRITE);
    } else return fail(g,R3_TG_REGISTER);
    if(g->raw.write(g->raw.context,address,width,value)!=1)return fail(g,R3_TG_IO);
    ++g->completed_io;return 1;
}
static int guarded_read(void *context,uint32_t address,uint32_t width,uint32_t *value) {
    r3_tx_guard_t *g=context;
    if(g->phase==R3_TG_FAULT)return 0;
    g->last_address=address;
    if(!value||width!=2u)return fail(g,R3_TG_REGISTER);
    int phy=address==R3_PHY_REGISTER_BASE+0x3feu;
    if((phy&&!g->have_phy)||(!phy&&(address!=R3_PHY_REGISTER_BASE+0x3dau||!g->have_radio)))
        return fail(g,R3_TG_SELECTOR);
    if(g->raw.read(g->raw.context,address,width,value)!=1)return fail(g,R3_TG_IO);
    ++g->completed_io;g->last_value=*value;
    if(*value>0xffffu)return fail(g,R3_TG_IO);
    if(phy&&g->phase==R3_TG_ARMED&&!desired(g->phy_selector,*value))return fail(g,R3_TG_INVARIANT);
    if(!phy&&g->pa_phase==R3_TG_PA_ARMED&&!radio_desired(g->radio_selector,*value))
        return fail(g,R3_TG_INVARIANT);
    return 1;
}
r3_tx_guard_error_t r3_tx_guard_init(r3_tx_guard_t *g,const r3_phy_bus_t *raw) {
    if(!g||!raw||!raw->read||!raw->write||raw->base!=R3_PHY_REGISTER_BASE||
       raw->read==guarded_read||raw->write==guarded_write)return R3_TG_ARGUMENT;
    if(g->phase!=R3_TG_NEW)return R3_TG_STATE;
    g->raw.context=raw->context;g->raw.read=raw->read;g->raw.write=raw->write;g->raw.base=raw->base;
    g->bus.context=g;g->bus.read=guarded_read;g->bus.write=guarded_write;g->bus.base=R3_PHY_REGISTER_BASE;
    g->phase=R3_TG_BOUND;g->error=R3_TG_OK;
    g->pa_phase=R3_TG_PA_UNBOUND;
    g->have_phy=g->have_radio=g->completed_io=g->verified_registers=0u;
    return R3_TG_OK;
}
r3_tx_guard_error_t r3_tx_guard_check(r3_tx_guard_t *g) {
    if(!g)return R3_TG_ARGUMENT;
    if(g->phase==R3_TG_FAULT)return g->error;
    if(g->phase!=R3_TG_BOUND&&g->phase!=R3_TG_ARMED)return R3_TG_STATE;
    const uint32_t indices[4]={0x44du,0x44cu,0x939u,0x938u};
    g->verified_registers=0u;
    for(uint32_t i=0u;i<4u;++i) {
        const r3_phy_op_t op={R3_PHY_READ,indices[i],0u,0u};r3_phy_report_t report;
        if(r3_phy_execute(&g->bus,&op,&report)!=R3_PHY_OK)return g->error;
        if(!desired(indices[i],report.value)){(void)fail(g,R3_TG_INVARIANT);return g->error;}
        ++g->verified_registers;
    }
    if(g->pa_phase!=R3_TG_PA_UNBOUND) {
        const uint32_t radio_indices[2]={0x79u,0xa4u};
        for(uint32_t i=0u;i<2u;++i) {
            const r3_phy_op_t op={R3_RADIO_READ,radio_indices[i],0u,0u};r3_phy_report_t report;
            if(r3_radio_execute(&g->bus,&op,&report)!=R3_PHY_OK)return g->error;
            if(!radio_desired(radio_indices[i],report.value)){(void)fail(g,R3_TG_INVARIANT);return g->error;}
            ++g->verified_registers;
        }
    }
    return R3_TG_OK;
}
r3_tx_guard_error_t r3_tx_guard_arm(r3_tx_guard_t *g) {
    if(!g)return R3_TG_ARGUMENT;
    if(g->phase==R3_TG_FAULT)return g->error;
    if(g->phase!=R3_TG_BOUND)return R3_TG_STATE;
    static const r3_phy_op_t ops[4]={
        {R3_PHY_MOD,0x44du,0x4000u,0u},{R3_PHY_MOD,0x44cu,0x4000u,0x4000u},
        {R3_PHY_MOD,0x939u,0x800u,0u},{R3_PHY_MOD,0x938u,0x800u,0x800u}
    };
    for(uint32_t i=0u;i<4u;++i) {
        r3_phy_report_t report;
        if(r3_phy_execute(&g->bus,&ops[i],&report)!=R3_PHY_OK)return g->error;
    }
    r3_tx_guard_error_t error=r3_tx_guard_check(g);
    if(!error)g->phase=R3_TG_ARMED;
    return error;
}
r3_tx_guard_error_t r3_tx_guard_arm_radio_pa(r3_tx_guard_t *g) {
    if(!g)return R3_TG_ARGUMENT;
    if(g->phase==R3_TG_FAULT)return g->error;
    if(g->phase!=R3_TG_ARMED||g->pa_phase!=R3_TG_PA_UNBOUND)return R3_TG_STATE;
    r3_tx_guard_error_t error=r3_tx_guard_check(g);
    if(error)return error;
    g->pa_phase=R3_TG_PA_ARMING;
    static const r3_phy_op_t ops[2]={
        {R3_RADIO_MOD,0x79u,1u,0u},{R3_RADIO_MOD,0xa4u,4u,4u}
    };
    for(uint32_t i=0u;i<2u;++i) {
        r3_phy_report_t report;
        if(r3_radio_execute(&g->bus,&ops[i],&report)!=R3_PHY_OK)return g->error;
    }
    error=r3_tx_guard_check(g);
    if(!error)g->pa_phase=R3_TG_PA_ARMED;
    return error;
}
const char *r3_tx_guard_error_name(r3_tx_guard_error_t error) {
    switch(error) {
    case R3_TG_OK:return "candidate override register policy satisfied, not RF proof";
    case R3_TG_ARGUMENT:return "invalid guard argument";
    case R3_TG_STATE:return "invalid guard lifecycle";
    case R3_TG_IO:return "guard bus read/write failure";
    case R3_TG_REGISTER:return "unapproved register access";
    case R3_TG_SELECTOR:return "invalid or missing selector";
    case R3_TG_DEFEATING_WRITE:return "write would release or reverse candidate inhibit";
    case R3_TG_INVARIANT:return "candidate override readback invariant lost";
    default:return "unknown guard error";
    }
}

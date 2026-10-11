#include "radio_startup.h"
#include "radio_calibration.h"

typedef struct { uint32_t delay; r3_phy_kind_t kind; uint32_t index,mask,value; } action_t;
static const action_t trigger[]={
    {0u,R3_RADIO_OR,0x8au,0u,0x100u},
    {0u,R3_RADIO_MOD,0x1bu,0x700u,0u},
    {0u,R3_RADIO_MOD,0x51u,1u,0u},
    {1u,R3_RADIO_MOD,0u,0u,0u},
    {0u,R3_RADIO_MOD,0x51u,1u,1u},
    {0u,R3_RADIO_MOD,0xa0u,0x100u,0x100u},
    {0u,R3_RADIO_MOD,0x42u,0x10u,0u},
    {0u,R3_RADIO_MOD,0xa0u,0x200u,0x200u},
    {0u,R3_RADIO_MOD,0x42u,0x20u,0u},
    {1u,R3_RADIO_MOD,0u,0u,0u},
    {0u,R3_RADIO_MOD,0x42u,0x10u,0x10u},
    {0u,R3_RADIO_MOD,0x42u,0x20u,0x20u},
    {1u,R3_RADIO_MOD,0u,0u,0u},
    {0u,R3_RADIO_MOD,0x42u,0x80u,0x80u},
    {0u,R3_RADIO_MOD,0x42u,0x40u,0x40u},
    {1u,R3_RADIO_MOD,0u,0u,0u},
    {0u,R3_RADIO_MOD,0x42u,0x80u,0u},
    {0u,R3_RADIO_MOD,0x42u,0x40u,0u},
    {50u,R3_RADIO_MOD,0u,0u,0u}
};
static void clear(r3_vco_report_t *r) {
    volatile uint8_t *p=(volatile uint8_t *)r;
    for(uint32_t i=0u;i<sizeof(*r);++i)p[i]=0u;
}
static int fail(r3_vco_report_t *r,r3_vco_error_t error) {
    r->error=error;r->reset_required=1u;return 0;
}
static int cancellation(const r3_vco_time_t *time,r3_vco_report_t *r) {
    ++r->cancel_checks;
    int value=time->cancelled(time->context);
    if(value==1){r->wait.result=R3_WAIT_CANCELLED;return fail(r,R3_VCO_CANCELLED);}
    if(value!=0)return fail(r,R3_VCO_SERVICE_ERROR);
    return 1;
}
static int clock_read(const r3_vco_time_t *time,r3_vco_report_t *r,uint32_t *now) {
    ++r->clock_samples;
    if(time->now(time->context,now)!=1)return fail(r,R3_VCO_CLOCK_IO);
    return 1;
}
static int check_time(const r3_vco_time_t *time,r3_vco_report_t *r) {
    uint32_t now;
    if(!cancellation(time,r)||!clock_read(time,r,&now))return 0;
    r->wait.elapsed=now-r->wait.start;
    if(r->wait.elapsed>=r->wait.budget) {
        r->wait.result=R3_WAIT_TIMEOUT;return fail(r,R3_VCO_TIMEOUT);
    }
    return 1;
}
static int delay(const r3_vco_time_t *time,uint32_t argument,r3_vco_report_t *r) {
    if(!check_time(time,r))return 0;
    r->last_delay=argument;++r->delay_attempts;
    if(time->delay_argument(time->context,argument)!=1)return fail(r,R3_VCO_DELAY_IO);
    ++r->delay_completed;r->nominal_delay_total+=argument;
    return check_time(time,r);
}
static int register_operation(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                               const r3_phy_op_t *op,r3_vco_report_t *r,uint32_t *value,int radio) {
    if(!check_time(time,r))return 0;
    r3_phy_report_t *access=radio?&r->radio:&r->phy;
    r3_phy_error_t error=radio?r3_radio_execute(bus,op,access):r3_phy_execute(bus,op,access);
    r->io_completed+=access->io_completed;
    if(error) {
        r->wait.result=R3_WAIT_IO_ERROR;return fail(r,radio?R3_VCO_RADIO_IO:R3_VCO_PHY_IO);
    }
    if(radio)++r->radio_operations;else ++r->phy_operations;
    if(value)*value=access->value;
    return check_time(time,r);
}
static int radio_operation(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                            const r3_phy_op_t *op,r3_vco_report_t *r,uint32_t *value) {
    return register_operation(bus,time,op,r,value,1);
}
r3_vco_error_t r3_vco_protocol_run(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                                  const r3_vco_limits_t *limits,r3_vco_report_t *r) {
    if(!r)return R3_VCO_ARGUMENT;
    clear(r);
    if(!bus||bus->base!=R3_PHY_REGISTER_BASE||!bus->read||!bus->write||
       !time||!time->now||!time->delay_argument||!time->cancelled||!limits||
       !limits->budget_ticks||limits->budget_ticks>0x7fffffffu||
       !limits->sample_limit||limits->sample_limit>100u) {
        r->error=R3_VCO_ARGUMENT;return r->error;
    }
    uint32_t start;
    if(!cancellation(time,r)||!clock_read(time,r,&start))return r->error;
    (void)r3_wait_begin(&r->wait,start,limits->budget_ticks,limits->sample_limit);
    r->reset_required=1u;r->phase=R3_VCO_TRIGGER;
    for(uint32_t i=0u;i<sizeof(trigger)/sizeof(trigger[0]);++i) {
        const action_t *a=&trigger[i];
        if(a->delay) {
            if(!delay(time,a->delay,r))return r->error;
        } else {
            const r3_phy_op_t op={a->kind,a->index,a->mask,a->value};
            if(!radio_operation(bus,time,&op,r,0))return r->error;
        }
    }
    r->phase=R3_VCO_POLL;
    while(r->wait.result==R3_WAIT_PENDING) {
        const r3_phy_op_t read={R3_RADIO_READ,0x65u,0u,0u};
        if(!delay(time,10u,r)||!radio_operation(bus,time,&read,r,&r->last_status))return r->error;
        if(r->last_status==0xffffu){(void)fail(r,R3_VCO_VALUE);return r->error;}
        r3_wait_sample_t sample=(r->last_status&1u)?R3_SAMPLE_READY:R3_SAMPLE_PENDING;
        (void)r3_wait_update(&r->wait,r->wait.start+r->wait.elapsed,sample);
    }
    if(r->wait.result!=R3_WAIT_READY) {
        (void)fail(r,r->wait.result==R3_WAIT_TIMEOUT?R3_VCO_TIMEOUT:R3_VCO_SAMPLE_LIMIT);
        return r->error;
    }
    r->completion_bit_observed=1u;r->phase=R3_VCO_SETTLE;
    if(!delay(time,100u,r))return r->error;
    r->phase=R3_VCO_RELEASE;
    const r3_phy_op_t release={R3_RADIO_AND,0x8au,0xfeffu,0u};
    if(!radio_operation(bus,time,&release,r,0))return r->error;
    r->reset_required=0u;r->phase=R3_VCO_COMPLETE;return R3_VCO_OK;
}

static int begin_io(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                     uint32_t budget,int needs_delay,r3_vco_report_t *r) {
    clear(r);
    if(!bus||bus->base!=R3_PHY_REGISTER_BASE||!bus->read||!bus->write||
       !time||!time->now||(needs_delay&&!time->delay_argument)||!time->cancelled||
       !budget||budget>0x7fffffffu) { r->error=R3_VCO_ARGUMENT;return 0; }
    uint32_t start;
    if(!cancellation(time,r)||!clock_read(time,r,&start))return 0;
    (void)r3_wait_begin(&r->wait,start,budget,100u);
    r->reset_required=1u;return 1;
}
static int begin_protocol(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                          uint32_t budget,r3_vco_report_t *r) {
    return begin_io(bus,time,budget,1,r);
}
static int core_read(const r3_phy_bus_t *bus,const r3_vco_time_t *time,uint32_t address,
                     uint32_t *value,uint32_t *last_address,uint32_t *last_value,r3_vco_report_t *io) {
    if(!check_time(time,io))return 0;
    *last_address=address;
    if(bus->read(bus->context,address,4u,value)!=1)return fail(io,R3_VCO_CORE_IO);
    ++io->io_completed;*last_value=*value;
    return check_time(time,io);
}
static int core_write(const r3_phy_bus_t *bus,const r3_vco_time_t *time,uint32_t address,
                      uint32_t value,uint32_t *last_address,uint32_t *last_value,
                      uint32_t *writes,r3_vco_report_t *io) {
    if(!check_time(time,io))return 0;
    *last_address=address;*last_value=value;
    if(bus->write(bus->context,address,4u,value)!=1)return fail(io,R3_VCO_CORE_IO);
    ++io->io_completed;++*writes;
    return check_time(time,io);
}
static int d11_read(const r3_phy_bus_t *bus,const r3_vco_time_t *time,uint32_t address,
                    uint32_t *value,r3_d11_report_t *r) {
    return core_read(bus,time,address,value,&r->last_address,&r->last_value,&r->transport);
}
static int d11_write(const r3_phy_bus_t *bus,const r3_vco_time_t *time,uint32_t address,
                     uint32_t value,r3_d11_report_t *r) {
    return core_write(bus,time,address,value,&r->last_address,&r->last_value,&r->writes,&r->transport);
}
static int d11_expect(const r3_phy_bus_t *bus,const r3_vco_time_t *time,uint32_t address,
                      uint32_t expected,uint32_t *value,r3_d11_report_t *r) {
    if(!d11_read(bus,time,address,value,r))return 0;
    return *value==expected?1:fail(&r->transport,R3_VCO_READBACK);
}
static int d11_idle(const r3_phy_bus_t *bus,const r3_vco_time_t *time,uint32_t limit,r3_d11_report_t *r) {
    for(uint32_t i=0u;i<limit;++i) {
        uint32_t status;
        if(!d11_read(bus,time,0x18101804u,&status,r))return 0;
        ++r->status_polls;
        if(status==0xffffffffu)return fail(&r->transport,R3_VCO_VALUE);
        if(!status)return 1;
        if(i+1u<limit&&!delay(time,10u,&r->transport))return 0;
    }
    return fail(&r->transport,R3_VCO_SAMPLE_LIMIT);
}
static int d11_mac_stopped(const r3_phy_bus_t *bus,const r3_vco_time_t *time,r3_d11_report_t *r) {
    if(!d11_read(bus,time,0x18001120u,&r->maccontrol,r))return 0;
    return (r->maccontrol&3u)?fail(&r->transport,R3_VCO_CORE_STATE):1;
}
static int d11_begin(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                     const r3_vco_limits_t *limits,uint32_t flags,r3_d11_report_t *r) {
    volatile uint8_t *bytes=(volatile uint8_t *)r;
    for(uint32_t i=0u;i<sizeof(*r);++i)bytes[i]=0u;
    if(!limits||!limits->sample_limit||limits->sample_limit>31u||(flags&~0x2000u)) {
        r->transport.error=R3_VCO_ARGUMENT;return 0;
    }
    return begin_protocol(bus,time,limits->budget_ticks,&r->transport);
}
r3_vco_error_t r3_d11_prepare(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                             const r3_vco_limits_t *limits,uint32_t flags,r3_d11_report_t *r) {
    if(!r)return R3_VCO_ARGUMENT;
    if(!d11_begin(bus,time,limits,flags,r))return r->transport.error;
    r3_vco_report_t *io=&r->transport;
    if(!d11_expect(bus,time,0x18101800u,1u,&r->resetctrl,r)||
       !d11_expect(bus,time,0x18101408u,0u,&r->ioctrl,r))return io->error;
    r->phase=R3_D11_IDLE;
    if(!d11_idle(bus,time,limits->sample_limit,r))return io->error;
    r->phase=R3_D11_ASSERT;
    if(!d11_write(bus,time,0x18101800u,1u,r)||!delay(time,10u,io)||
       !d11_idle(bus,time,limits->sample_limit,r)||
       !d11_expect(bus,time,0x18101800u,1u,&r->resetctrl,r))return io->error;
    r->phase=R3_D11_CLOCK;
    if(!d11_write(bus,time,0x18101408u,flags|0xfu,r)||
       !d11_expect(bus,time,0x18101408u,flags|0xfu,&r->ioctrl,r)||
       !d11_idle(bus,time,limits->sample_limit,r))return io->error;
    r->phase=R3_D11_RELEASE;
    if(!d11_expect(bus,time,0x18101800u,1u,&r->resetctrl,r)||
       !d11_idle(bus,time,limits->sample_limit,r)||
       !d11_write(bus,time,0x18101800u,0u,r)||
       !d11_idle(bus,time,limits->sample_limit,r)||
       !d11_expect(bus,time,0x18101800u,0u,&r->resetctrl,r))return io->error;
    if(!d11_write(bus,time,0x18101408u,flags|0xdu,r)||
       !d11_expect(bus,time,0x18101408u,flags|0xdu,&r->ioctrl,r)||!delay(time,1u,io)||
       !d11_expect(bus,time,0x18101800u,0u,&r->resetctrl,r)||
       !d11_mac_stopped(bus,time,r))return io->error;
    r->core_prepared=1u;r->phase=R3_D11_PHY_HELD;
    return R3_VCO_OK;
}
r3_vco_error_t r3_d11_phy_start(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                               const r3_vco_limits_t *limits,uint32_t flags,r3_d11_report_t *r) {
    static const uint32_t masks[3]={0xccu,0xeu,6u},values[3]={0x4cu,2u,4u},delays[3]={2u,1u,1u};
    if(!r)return R3_VCO_ARGUMENT;
    if(!d11_begin(bus,time,limits,flags,r))return r->transport.error;
    r3_vco_report_t *io=&r->transport;
    if(!d11_expect(bus,time,0x18101800u,0u,&r->resetctrl,r)||
       !d11_expect(bus,time,0x18101408u,flags|0xdu,&r->ioctrl,r)||
       !d11_mac_stopped(bus,time,r))return io->error;
    r->core_prepared=1u;r->phase=R3_D11_PHY_CLOCK;
    for(uint32_t i=0u;i<3u;++i) {
        uint32_t old;
        if(!d11_expect(bus,time,0x18101408u,r->ioctrl,&old,r))return io->error;
        uint32_t value=(old&~masks[i])|values[i];
        if(!d11_write(bus,time,0x18101408u,value,r)||
           !d11_expect(bus,time,0x18101408u,value,&r->ioctrl,r)||
           !delay(time,delays[i],io))return io->error;
    }
    if(!d11_expect(bus,time,0x18101800u,0u,&r->resetctrl,r)||
       !d11_mac_stopped(bus,time,r))return io->error;
    r->phy_released=1u;r->phase=R3_D11_COMPLETE;io->reset_required=0u;
    return R3_VCO_OK;
}
static int gci_read(const r3_phy_bus_t *bus,const r3_vco_time_t *time,uint32_t address,
                    uint32_t *value,r3_gci_report_t *r) {
    if(!core_read(bus,time,address,value,&r->last_address,&r->last_value,&r->transport))return 0;
    return *value==0xffffffffu?fail(&r->transport,R3_VCO_VALUE):1;
}
r3_vco_error_t r3_gci_radio_notify(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                                   const r3_vco_limits_t *limits,r3_gci_report_t *r) {
    static const uint32_t addresses[4]={0x18000d74u,0x18000d64u,0x18000d64u,0x18000d64u};
    static const uint32_t masks[4]={8u,8u,0xf000u,0xe00u},values[4]={8u,8u,0x1000u,0x200u};
    static const uint32_t pre_addresses[5]={0x18101800u,0x18101408u,0x18001120u,0x18000004u,0x180000acu};
    static const uint32_t pre_values[5]={0u,0x45u,0x04000404u,0x10400000u,0x1eu};
    if(!r)return R3_VCO_ARGUMENT;
    volatile uint8_t *bytes=(volatile uint8_t *)r;
    for(uint32_t i=0u;i<sizeof(*r);++i)bytes[i]=0u;
    r3_vco_report_t *io=&r->transport;
    if(!limits||limits->sample_limit<4u||limits->sample_limit>100u) {
        io->error=R3_VCO_ARGUMENT;return io->error;
    }
    if(!begin_io(bus,time,limits->budget_ticks,0,io))return io->error;
    io->wait.limit=4u;
    uint32_t *pre_fields[5]={&r->resetctrl,&r->ioctrl,&r->maccontrol,&r->capabilities,&r->capabilities_ext};
    for(uint32_t i=0u;i<5u;++i) {
        if(!gci_read(bus,time,pre_addresses[i],pre_fields[i],r))return io->error;
        if(*pre_fields[i]!=pre_values[i]){(void)fail(io,R3_VCO_CORE_STATE);return io->error;}
    }
    for(uint32_t i=0u;i<4u;++i) {
        r->phase=R3_GCI_BEFORE;
        if(!gci_read(bus,time,addresses[i],&r->before[i],r))return io->error;
        r->requested[i]=(r->before[i]&~masks[i])|values[i];
        if(r->requested[i]==0xffffffffu){(void)fail(io,R3_VCO_VALUE);return io->error;}
        r->phase=R3_GCI_WRITE;
        if(!core_write(bus,time,addresses[i],r->requested[i],&r->last_address,&r->last_value,&r->writes,io))
            return io->error;
        r->phase=R3_GCI_READBACK;
        if(!gci_read(bus,time,addresses[i],&r->readback[i],r))return io->error;
        if(r->readback[i]!=r->requested[i]){(void)fail(io,R3_VCO_READBACK);return io->error;}
        ++r->steps;
        (void)r3_wait_update(&io->wait,io->wait.start+io->wait.elapsed,i==3u?R3_SAMPLE_READY:R3_SAMPLE_PENDING);
    }
    r->phase=R3_GCI_COMPLETE;io->reset_required=0u;return R3_VCO_OK;
}
static int dc_modify(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                      uint32_t index,uint32_t mask,uint32_t value,r3_vco_report_t *r) {
    const r3_phy_op_t op={R3_PHY_MOD,index,mask,value};
    return register_operation(bus,time,&op,r,0,0);
}
r3_vco_error_t r3_dccal_run(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                           const r3_vco_limits_t *limits,r3_dccal_report_t *r) {
    if(!r)return R3_VCO_ARGUMENT;
    volatile uint8_t *bytes=(volatile uint8_t *)r;
    for(uint32_t i=0u;i<sizeof(*r);++i)bytes[i]=0u;
    r3_vco_report_t *io=&r->transport;
    if(!limits||!limits->sample_limit||limits->sample_limit>10u) {
        io->error=R3_VCO_ARGUMENT;return io->error;
    }
    if(!begin_protocol(bus,time,limits->budget_ticks,io))return io->error;
    io->wait.limit=limits->sample_limit;
    r->phase=R3_DCCAL_FORCE;
    if(!dc_modify(bus,time,0x965u,4u,4u,io)||!dc_modify(bus,time,0x965u,8u,8u,io)||
       !dc_modify(bus,time,0x975u,0x4fu,9u,io)||!dc_modify(bus,time,0x969u,0x3800u,0u,io)||
       !dc_modify(bus,time,0x440u,4u,4u,io))return io->error;
    r->phase=R3_DCCAL_POLL;
    while(io->wait.result==R3_WAIT_PENDING) {
        const r3_phy_op_t set={R3_PHY_MOD,0x44au,1u,1u};
        const r3_phy_op_t clear_bit={R3_PHY_MOD,0x44au,1u,0u};
        const r3_phy_op_t idac={R3_PHY_READ,0x978u,0u,0u};
        const r3_phy_op_t dcoe={R3_PHY_READ,0x96fu,0u,0u};
        if(!register_operation(bus,time,&set,io,0,0)||
           !register_operation(bus,time,&clear_bit,io,0,0))return io->error;
        ++r->pulses;
        if(!delay(time,32u,io)||
           !register_operation(bus,time,&idac,io,&r->idac_status,0))return io->error;
        if(r->idac_status==0xffffu){(void)fail(io,R3_VCO_VALUE);return io->error;}
        int ready=0;
        if((r->idac_status&0xffu)==0xffu) {
            if(!register_operation(bus,time,&dcoe,io,&r->dcoe_status,0))return io->error;
            if(r->dcoe_status==0xffffu){(void)fail(io,R3_VCO_VALUE);return io->error;}
            ready=(r->dcoe_status&0xfffu)==0xfffu;
        }
        (void)r3_wait_update(&io->wait,io->wait.start+io->wait.elapsed,
                             ready?R3_SAMPLE_READY:R3_SAMPLE_PENDING);
    }
    if(io->wait.result!=R3_WAIT_READY) {
        (void)fail(io,io->wait.result==R3_WAIT_TIMEOUT?R3_VCO_TIMEOUT:R3_VCO_SAMPLE_LIMIT);
        return io->error;
    }
    r->predicate_observed=1u;r->phase=R3_DCCAL_RELEASE;
    if(!dc_modify(bus,time,0x975u,0x4fu,0xfu,io)||!dc_modify(bus,time,0x969u,0x3800u,0u,io)||
       !dc_modify(bus,time,0x440u,4u,0u,io))return io->error;
    r->phase=R3_DCCAL_COMPLETE;io->reset_required=0u;
    return R3_VCO_OK;
}
static int modify_radio(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                        uint32_t index,uint32_t mask,uint32_t value,r3_vco_report_t *r) {
    const r3_phy_op_t op={R3_RADIO_MOD,index,mask,value};
    return radio_operation(bus,time,&op,r,0);
}
static int read_radio(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                      uint32_t index,uint32_t *value,r3_vco_report_t *r) {
    const r3_phy_op_t op={R3_RADIO_READ,index,0u,0u};
    return radio_operation(bus,time,&op,r,value);
}
typedef struct { const r3_vco_time_t *time;r3_vco_report_t *report; } init_time_t;
static int init_now(void *context,uint32_t *value) {
    init_time_t *bound=context;r3_vco_report_t *r=bound->report;
    if(!clock_read(bound->time,r,value))return 0;
    r->wait.elapsed=*value-r->wait.start;
    if(r->wait.elapsed>=r->wait.budget) {
        r->wait.result=R3_WAIT_TIMEOUT;return fail(r,R3_VCO_TIMEOUT);
    }
    return 1;
}
static int init_delay(void *context,uint32_t argument) {
    init_time_t *bound=context;return delay(bound->time,argument,bound->report);
}
static int init_cancelled(void *context) {
    init_time_t *bound=context;
    if(cancellation(bound->time,bound->report))return 0;
    return bound->report->error==R3_VCO_CANCELLED?1:-1;
}
static int init_actions(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                        const action_t *actions,uint32_t count,r3_vco_report_t *r) {
    for(uint32_t i=0u;i<count;++i) {
        const action_t *a=&actions[i];
        if(a->delay) {
            if(!delay(time,a->delay,r))return 0;
        } else {
            const r3_phy_op_t op={a->kind,a->index,a->mask,a->value};
            if(!radio_operation(bus,time,&op,r,0))return 0;
        }
    }
    return 1;
}
r3_vco_error_t r3_radio_channel_run(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                                   const r3_vco_limits_t *limits,const r3_radio_channel_t *c,
                                   r3_radio_channel_report_t *r) {
    static const uint16_t indices[31]={
        0x62,0x53,0x54,0x57,0x58,0x64,0x51,0x5c,0x5d,0x5a,0x5b,0x63,0x56,0x59,0x61,0x55,
        0x5f,0x52,0x4e,0x4f,0x4d,0x4c,0x4a,0x4b,0x48,0x49,0x45,0x46,0x18,0x3c,0x86
    };
    static const action_t band[3]={
        {0u,R3_RADIO_MOD,0x3du,0x1000u,0u},{0u,R3_RADIO_MOD,0x71u,0x1000u,0u},
        {0u,R3_RADIO_MOD,0x72u,0x2000u,0u}
    };
    if(!r)return R3_VCO_ARGUMENT;
    volatile uint8_t *bytes=(volatile uint8_t *)r;
    for(uint32_t i=0u;i<sizeof(*r);++i)bytes[i]=0u;
    r3_vco_report_t *io=&r->programming;
    if(!c||!c->words||c->capacity<31u||c->channel<1u||c->channel>14u||
       c->frequency_mhz!=(c->channel==14u?2484u:2407u+5u*c->channel)||
       (c->xtal_hz!=19200000u&&c->xtal_hz!=26000000u&&c->xtal_hz!=37400000u)||
       !limits||!limits->sample_limit||limits->sample_limit>100u) {
        io->error=R3_VCO_ARGUMENT;return io->error;
    }
    if(!begin_protocol(bus,time,limits->budget_ticks,io))return io->error;
    for(uint32_t i=0u;i<31u;++i) {
        const r3_phy_op_t op={R3_RADIO_WRITE,indices[i],0u,c->words[i]};
        if(!radio_operation(bus,time,&op,io,0))return io->error;
        ++r->words_completed;
    }
    for(uint32_t i=0u;i<3u;++i) {
        if(!init_actions(bus,time,&band[i],1u,io))return io->error;
        ++r->band_operations;
    }
    init_time_t bound={time,io};
    const r3_vco_time_t nested={&bound,init_now,init_delay,init_cancelled};
    r3_vco_error_t error=r3_vco_protocol_run(bus,&nested,limits,&r->vco);
    if(error){if(!io->error)(void)fail(io,error);return io->error;}
    r->configured=1u;io->reset_required=0u;return R3_VCO_OK;
}
r3_vco_error_t r3_radio_init_run(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                                const r3_vco_limits_t *limits,uint32_t xtal_hz,
                                const uint8_t small[52],const uint16_t large[56],
                                r3_radio_init_report_t *r) {
    static const action_t power[]={
        {0u,R3_RADIO_MOD,0x9bu,0x40u,0x40u},{0u,R3_RADIO_MOD,0x19u,0x80u,0x80u},
        {0u,R3_RADIO_MOD,0x9bu,0x10u,0x10u},{0u,R3_RADIO_MOD,0x19u,8u,8u},
        {100u,R3_RADIO_MOD,0u,0u,0u},
        {0u,R3_RADIO_MOD,0xa7u,1u,1u},{0u,R3_RADIO_MOD,0x89u,1u,1u},
        {0u,R3_RADIO_MOD,0x97u,1u,1u},{0u,R3_RADIO_MOD,4u,1u,1u},
        {20u,R3_RADIO_MOD,0u,0u,0u},
        {0u,R3_RADIO_MOD,0x9bu,0x20u,0x20u},{0u,R3_RADIO_MOD,0x9bu,8u,8u},
        {0u,R3_RADIO_MOD,0x9bu,4u,4u},{0u,R3_RADIO_MOD,0x9bu,2u,2u},
        {0u,R3_RADIO_MOD,0x19u,0x10u,0x10u},{0u,R3_RADIO_MOD,0x19u,4u,4u},
        {0u,R3_RADIO_MOD,0x19u,2u,2u},{0u,R3_RADIO_MOD,0x19u,1u,1u},
        {200u,R3_RADIO_MOD,0u,0u,0u},
        {0u,R3_RADIO_MOD,0x43u,1u,1u},{0u,R3_RADIO_MOD,0x51u,1u,0u},
        {0u,R3_RADIO_MOD,0xa0u,0x20u,0x20u},{0u,R3_RADIO_MOD,0x42u,2u,2u},
        {0u,R3_RADIO_MOD,0xa0u,0x40u,0x40u},{0u,R3_RADIO_MOD,0x42u,4u,4u},
        {0u,R3_RADIO_MOD,0xa0u,0x10u,0x10u},{0u,R3_RADIO_MOD,0x42u,1u,1u},
        {0u,R3_RADIO_MOD,0xa0u,0x80u,0x80u},{0u,R3_RADIO_MOD,0x42u,8u,8u},
        {0u,R3_RADIO_MOD,0x97u,4u,4u},{0u,R3_RADIO_MOD,4u,0xf0u,0x70u},
        {0u,R3_RADIO_MOD,0x97u,2u,0u},{0u,R3_RADIO_AND,0x8au,0xfdffu,0u}
    };
    static const action_t dac[]={
        {0u,R3_RADIO_MOD,8u,0xf0u,0x40u},{0u,R3_RADIO_MOD,8u,0xeu,8u},
        {0u,R3_RADIO_MOD,0x6fu,0x200u,0x200u},{0u,R3_RADIO_MOD,0x6fu,0xc00u,0u},
        {0u,R3_RADIO_MOD,4u,2u,2u},{0u,R3_RADIO_MOD,0x6cu,0x30u,0x10u}
    };
    static const action_t preferred[]={
        {0u,R3_RADIO_MOD,0x89u,0x300u,0x300u},
        {0u,R3_RADIO_MOD,0x18u,7u,0u},{0u,R3_RADIO_MOD,0x18u,0x70u,0u},
        {0u,R3_RADIO_MOD,0x17u,0xe0u,0x40u},{0u,R3_RADIO_MOD,0x17u,0x700u,0x700u},
        {0u,R3_RADIO_MOD,0x8du,0xf0u,0x10u},
        {0u,R3_RADIO_MOD,0x79u,0x20u,0u},{0u,R3_RADIO_MOD,0x79u,0x10u,0u},
        {0u,R3_RADIO_MOD,0x79u,8u,0u},
        {0u,R3_RADIO_MOD,0x7au,0xff00u,0x3000u},{0u,R3_RADIO_MOD,0x7au,0xf0u,0xf0u},
        {0u,R3_RADIO_MOD,0x7au,0xfu,0u},
        {0u,R3_RADIO_MOD,0x7cu,0x3fu,0x19u},{0u,R3_RADIO_MOD,0x7cu,0x3f00u,0x1900u},
        {0u,R3_RADIO_MOD,0x7du,0x780u,0u},{0u,R3_RADIO_MOD,0x7du,0x3fu,0x21u},
        {0u,R3_RADIO_MOD,0x83u,0x3eu,0x2au},
        {0u,R3_RADIO_MOD,0x87u,0xf000u,0u},{0u,R3_RADIO_MOD,0x87u,0xfc0u,0xa00u},
        {0u,R3_RADIO_MOD,0x87u,0x3fu,0x16u},
        {0u,R3_RADIO_MOD,0x7bu,0x700u,0x400u},{0u,R3_RADIO_MOD,0x7bu,0x70u,0x40u},
        {0u,R3_RADIO_MOD,0x7bu,3u,1u},{0u,R3_RADIO_MOD,0x1eu,0x80u,0u}
    };
    if(!r)return R3_VCO_ARGUMENT;
    volatile uint8_t *bytes=(volatile uint8_t *)r;
    for(uint32_t i=0u;i<sizeof(*r);++i)bytes[i]=0u;
    r3_vco_report_t *io=&r->envelope;
    if(!small||!large||!limits||!limits->sample_limit||limits->sample_limit>100u||
       (xtal_hz!=19200000u&&xtal_hz!=26000000u&&xtal_hz!=37400000u)) {
        io->error=R3_VCO_ARGUMENT;return io->error;
    }
    if(!begin_protocol(bus,time,limits->budget_ticks,io))return io->error;
    init_time_t bound={time,io};
    const r3_vco_time_t nested={&bound,init_now,init_delay,init_cancelled};
    r3_vco_error_t error;
    r->phase=R3_RADIO_INIT_POWER;
    if(!modify_radio(bus,time,0x8cu,1u,xtal_hz==37400000u,io)||
       !init_actions(bus,time,power,sizeof(power)/sizeof(power[0]),io))return io->error;
    r->phase=R3_RADIO_INIT_RC;
    error=r3_rccal_run(bus,&nested,limits,xtal_hz,&r->rc);
    if(error){if(!io->error)(void)fail(io,error);return io->error;}
    r->phase=R3_RADIO_INIT_ADC;
    error=r3_adc_coefficients(r->rc.adc,r->adc);
    if(!error)error=r3_adc_apply(bus,&nested,limits->budget_ticks,r->adc,&r->adc_io);
    if(error){if(!io->error)(void)fail(io,error);return io->error;}
    r->phase=R3_RADIO_INIT_DAC;
    if(!init_actions(bus,time,dac,sizeof(dac)/sizeof(dac[0]),io))return io->error;
    r->phase=R3_RADIO_INIT_TIA;
    error=r3_tia_apply(bus,&nested,limits->budget_ticks,small,large,&r->tia_io);
    if(error){if(!io->error)(void)fail(io,error);return io->error;}
    r->phase=R3_RADIO_INIT_PREFERRED;
    if(!init_actions(bus,time,preferred,sizeof(preferred)/sizeof(preferred[0]),io))return io->error;
    r->phase=R3_RADIO_INIT_CONFIGURED;io->reset_required=0u;
    return R3_VCO_OK;
}
r3_vco_error_t r3_rccal_run(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                           const r3_vco_limits_t *limits,uint32_t xtal_hz,r3_rccal_report_t *r) {
    if(!r)return R3_VCO_ARGUMENT;
    volatile uint8_t *bytes=(volatile uint8_t *)r;
    for(uint32_t i=0u;i<sizeof(*r);++i)bytes[i]=0u;
    r3_vco_report_t *io=&r->transport;
    if(!limits||!limits->sample_limit||limits->sample_limit>100u||
       (xtal_hz!=19200000u&&xtal_hz!=26000000u&&xtal_hz!=37400000u)) {
        io->error=R3_VCO_ARGUMENT;return io->error;
    }
    if(!begin_protocol(bus,time,limits->budget_ticks,io))return io->error;
    uint32_t old,values[2],raw;
    if(!read_radio(bus,time,0x8au,&old,io)||
       !modify_radio(bus,time,0x8au,0x7f0u,(old&0x7f0u)|0x4400u,io))return io->error;
    for(uint32_t cycle=0u;cycle<2u;++cycle) {
        r->phase=cycle?R3_RCCAL_ADC:R3_RCCAL_LPF;
        if(!modify_radio(bus,time,0x1eu,0x40u,0x40u,io)||
           !modify_radio(bus,time,0x1eu,0x30u,cycle?0x10u:0u,io)||
           !modify_radio(bus,time,0x8fu,0xff00u,cycle?0x7000u:0x1c00u,io)||
           !modify_radio(bus,time,0x90u,0x1fffu,cycle?0xf0u:0x22du,io)||
           !modify_radio(bus,time,0x1eu,1u,0u,io)||!delay(time,1u,io)||
           !modify_radio(bus,time,0x1eu,1u,1u,io)||!delay(time,35u,io)||
           !modify_radio(bus,time,0x8fu,1u,1u,io))return io->error;
        io->wait.samples=0u;io->wait.result=R3_WAIT_PENDING;
        io->wait.limit=limits->sample_limit;
        while(io->wait.result==R3_WAIT_PENDING) {
            if(!delay(time,100u,io)||!read_radio(bus,time,0x91u,&io->last_status,io))return io->error;
            if(io->last_status==0xffffu){(void)fail(io,R3_VCO_VALUE);return io->error;}
            (void)r3_wait_update(&io->wait,io->wait.start+io->wait.elapsed,
                                (io->last_status&1u)?R3_SAMPLE_READY:R3_SAMPLE_PENDING);
            r->samples[cycle]=io->wait.samples;
        }
        if(io->wait.result!=R3_WAIT_READY) {
            (void)fail(io,R3_VCO_SAMPLE_LIMIT);return io->error;
        }
        if(!modify_radio(bus,time,0x8fu,1u,0u,io)||
           !read_radio(bus,time,0x92u,&r->n0[cycle],io)||
           !read_radio(bus,time,0x93u,&r->n1[cycle],io))return io->error;
        r->n0[cycle]&=0x1fffu;r->n1[cycle]&=0x1fffu;
        if(r->n1[cycle]<=r->n0[cycle]) {
            (void)fail(io,R3_VCO_VALUE);return io->error;
        }
        values[cycle]=(cycle?102051u:101541u)*(r->n1[cycle]-r->n0[cycle])/(xtal_hz>>12u);
        if(!values[cycle]||values[cycle]>0xffffu) {
            (void)fail(io,R3_VCO_VALUE);return io->error;
        }
        if(!modify_radio(bus,time,0x1eu,1u,0u,io))return io->error;
        ++r->completed;
    }
    r->phase=R3_RCCAL_RELEASE;
    if(!read_radio(bus,time,0x94u,&raw,io)||!modify_radio(bus,time,0xa3u,0x100u,0u,io))return io->error;
    const r3_phy_op_t release={R3_RADIO_AND,0x8au,0xfbffu,0u};
    if(!radio_operation(bus,time,&release,io,0))return io->error;
    r->lpf=r->lpf_rc=(uint16_t)values[0];r->adc=(uint16_t)values[1];
    r->dacbuf=(uint8_t)((raw&0x3e0u)>>5u);
    io->reset_required=0u;io->completion_bit_observed=1u;r->phase=R3_RCCAL_COMPLETE;
    return R3_VCO_OK;
}

static int32_t positive(int32_t value) { return value<1?1:value; }
static uint16_t ceiling(int32_t value,uint32_t maximum) {
    return (uint16_t)(value>(int32_t)maximum?(int32_t)maximum:value);
}
static int32_t asr12(uint32_t value) {
    return value&0x80000000u?-(int32_t)((~value)>>12u)-1:(int32_t)(value>>12u);
}
r3_vco_error_t r3_adc_coefficients(uint32_t raw,uint16_t out[11]) {
    if(!out||!raw||raw>0xffffu)return R3_VCO_ARGUMENT;
    uint32_t reciprocal=(16777216u/raw)&0xfffffu;
    int32_t g=(int32_t)(reciprocal?reciprocal:1u);
    int32_t square=positive(asr12((uint32_t)g*(uint32_t)g));
    uint16_t v[11];
    v[0]=16u;
    v[1]=ceiling(511999/(68182015/square+1)+1,127u);
    v[2]=ceiling(255999/(52346879/g+1)+1,127u);
    v[3]=ceiling(255999/(55926783/g+1)+1,127u);
    v[4]=42u;
    v[5]=ceiling(positive((1994997759/g-128000)/4000+1),127u);
    v[6]=ceiling(255999/positive((int32_t)((uint32_t)(161939455/g)&0xfffffu)-7999)+1,127u);
    v[7]=ceiling(255999/positive((int32_t)((uint32_t)(138575871/g)&0xfffffu)-3999)+1,127u);
    v[8]=ceiling(255999/positive((int32_t)((uint32_t)(238493695/g)&0xfffffu)-31999)+1,127u);
    v[9]=10u;
    v[10]=ceiling(127999/positive(32767999/g-1999)+1,127u);
    for(uint32_t i=0u;i<11u;++i)out[i]=v[i];
    return R3_VCO_OK;
}
r3_vco_error_t r3_adc_apply(const r3_phy_bus_t *bus,const r3_vco_time_t *time,
                           uint32_t budget,const uint16_t values[11],r3_vco_report_t *r) {
    if(!r)return R3_VCO_ARGUMENT;
    clear(r);
    if(!values) { r->error=R3_VCO_ARGUMENT;return r->error; }
    for(uint32_t i=0u;i<11u;++i)if(values[i]>(i==9u?255u:127u)) {
        r->error=R3_VCO_ARGUMENT;return r->error;
    }
    if(!begin_protocol(bus,time,budget,r))return r->error;
    const r3_phy_op_t ops[]={
        {R3_RADIO_MOD,0x9cu,8u,0u},{R3_RADIO_MOD,0x9cu,4u,0u},
        {R3_RADIO_MOD,0x1fu,0xe0u,0x80u},{R3_RADIO_MOD,0x26u,0x800u,0u},
        {R3_RADIO_MOD,0x24u,0xffu,0x10u},{R3_RADIO_MOD,0x24u,0xff00u,0x800u},
        {R3_RADIO_MOD,0x25u,0xffu,8u},{R3_RADIO_MOD,0x25u,0xff00u,0x800u},
        {R3_RADIO_MOD,0x26u,0x300u,0x100u},{R3_RADIO_MOD,0x26u,0xffu,0x40u},
        {R3_RADIO_MOD,0x20u,0x7fu,values[0]},{R3_RADIO_MOD,0x21u,0x7fu,values[1]},
        {R3_RADIO_MOD,0x21u,0x7f00u,(uint32_t)values[2]<<8u},{R3_RADIO_MOD,0x22u,0x7fu,values[3]},
        {R3_RADIO_MOD,0x28u,0x7fu,values[6]},{R3_RADIO_MOD,0x28u,0x7f00u,(uint32_t)values[7]<<8u},
        {R3_RADIO_MOD,0x29u,0x7fu,values[8]},{R3_RADIO_MOD,0x29u,0xff00u,(uint32_t)values[9]<<8u},
        {R3_RADIO_MOD,0x23u,0x7f00u,(uint32_t)values[4]<<8u},{R3_RADIO_MOD,0x23u,0x7fu,values[5]},
        {R3_RADIO_MOD,0x22u,0x7f00u,(uint32_t)values[10]<<8u},
        {R3_RADIO_MOD,0x2au,0x1f00u,0x200u},{R3_RADIO_MOD,0x2au,0x6000u,0x2000u},
        {R3_RADIO_MOD,0x9cu,0x10u,0x10u},{R3_RADIO_MOD,0x1fu,4u,4u},
        {R3_RADIO_MOD,0x1fu,4u,0u},{R3_RADIO_MOD,0x9cu,0x10u,0u}
    };
    for(uint32_t i=0u;i<sizeof(ops)/sizeof(ops[0]);++i)
        if(!radio_operation(bus,time,&ops[i],r,0))return r->error;
    r->reset_required=0u;return R3_VCO_OK;
}
r3_vco_error_t r3_tia_apply(const r3_phy_bus_t *bus,const r3_vco_time_t *time,uint32_t budget,
                           const uint8_t small[52],const uint16_t large[56],r3_vco_report_t *r) {
    if(!r)return R3_VCO_ARGUMENT;
    clear(r);
    if(!small||!large) { r->error=R3_VCO_ARGUMENT;return r->error; }
    if(!begin_protocol(bus,time,budget,r))return r->error;
    for(uint32_t i=0u;i<108u;++i) {
        const r3_phy_op_t op={R3_RADIO_WRITE,0xa9u+i,0u,i<52u?small[i]:large[i-52u]};
        if(!radio_operation(bus,time,&op,r,0))return r->error;
    }
    r->reset_required=0u;return R3_VCO_OK;
}

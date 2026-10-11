#include "table.h"

static void clear_report(r3_table_report_t *r) {
    volatile r3_table_report_t *p=r;
    p->error=R3_TABLE_OK;p->phy_error=R3_PHY_OK;
    p->entries_completed=p->io_completed=p->last_address=p->last_value=0u;
    p->saved_9a5=p->saved_574=p->saved_575=p->saved_mask=0u;
    p->restore_writes_completed=p->reset_required=0u;
}
static r3_table_error_t fail(r3_table_report_t *r,r3_table_error_t error) {
    r->error=error;return error;
}
static r3_table_error_t validate(const r3_phy_bus_t *bus,const r3_table_request_t *q,
                                  const void *values,uint32_t capacity,r3_table_report_t *r) {
    if(!bus||!bus->read||!bus->write||bus->base!=R3_PHY_REGISTER_BASE||!q||!values)
        return fail(r,R3_TABLE_ARGUMENT);
    if(!q->count||q->count>R3_PHY_TABLE_MAX_ENTRIES||q->id>0xffffu||
       q->offset>0xffffu||q->count>0x10000u-q->offset||capacity<q->count)
        return fail(r,R3_TABLE_RANGE);
    if(q->width!=8u&&q->width!=16u&&q->width!=32u)return fail(r,R3_TABLE_WIDTH);
    return R3_TABLE_OK;
}
static int operation(const r3_phy_bus_t *bus,r3_phy_kind_t kind,uint32_t index,
                     uint32_t mask,uint32_t value,r3_table_report_t *r,uint32_t *out) {
    const r3_phy_op_t op={kind,index,mask,value};
    r3_phy_report_t step;
    r3_phy_error_t error=r3_phy_execute(bus,&op,&step);
    r->io_completed+=step.io_completed;
    r->last_address=step.address;r->last_value=step.value;
    if(error) {
        r->phy_error=error;r->reset_required=1u;
        fail(r,R3_TABLE_IO);return 0;
    }
    if(out)*out=step.value;
    return 1;
}
static int begin(const r3_phy_bus_t *bus,const r3_table_request_t *q,r3_table_report_t *r) {
    if(!operation(bus,R3_PHY_READ,0x9a5u,0u,0u,r,&r->saved_9a5))return 0;
    r->saved_mask=1u;
    r->reset_required=1u;
    if(!operation(bus,R3_PHY_MOD,0x9a5u,2u,2u,r,0))return 0;
    if(q->id==33u) {
        if(!operation(bus,R3_PHY_READ,0x574u,0u,0u,r,&r->saved_574))return 0;
        r->saved_mask|=2u;
        if(!operation(bus,R3_PHY_READ,0x575u,0u,0u,r,&r->saved_575))return 0;
        r->saved_mask|=4u;
        if(!operation(bus,R3_PHY_MOD,0x574u,0x4000u,0x4000u,r,0)||
           !operation(bus,R3_PHY_MOD,0x575u,0x0400u,0x0400u,r,0))return 0;
    }
    return operation(bus,R3_PHY_WRITE,0x00du,0u,q->id,r,0)&&
           operation(bus,R3_PHY_WRITE,0x00eu,0u,q->offset,r,0);
}
static int finish(const r3_phy_bus_t *bus,const r3_table_request_t *q,r3_table_report_t *r) {
    if(q->id==33u && (!operation(bus,R3_PHY_WRITE,0x574u,0u,r->saved_574,r,0)||
                      !operation(bus,R3_PHY_WRITE,0x575u,0u,r->saved_575,r,0)))return 0;
    if(!operation(bus,R3_PHY_WRITE,0x9a5u,0u,r->saved_9a5,r,0))return 0;
    r->restore_writes_completed=1u;r->reset_required=0u;return 1;
}
r3_table_error_t r3_phy_table_read(const r3_phy_bus_t *bus,const r3_table_request_t *q,
                                  uint32_t *values,uint32_t capacity,r3_table_report_t *r) {
    if(!r)return R3_TABLE_ARGUMENT;
    clear_report(r);
    if(validate(bus,q,values,capacity,r))return r->error;
    if(!begin(bus,q,r))return r->error;
    for(uint32_t i=0u;i<q->count;++i) {
        uint32_t low=0u,high=0u;
        if(!operation(bus,R3_PHY_READ,0x456u,0u,0u,r,&low))return r->error;
        if(q->width==32u && !operation(bus,R3_PHY_READ,0x457u,0u,0u,r,&high))return r->error;
        values[i]=q->width==8u?low&0xffu:low|(high<<16u);
        ++r->entries_completed;
    }
    if(!finish(bus,q,r))return r->error;
    return R3_TABLE_OK;
}
r3_table_error_t r3_phy_table_write(const r3_phy_bus_t *bus,const r3_table_request_t *q,
                                   const uint32_t *values,uint32_t capacity,r3_table_report_t *r) {
    if(!r)return R3_TABLE_ARGUMENT;
    clear_report(r);
    if(validate(bus,q,values,capacity,r))return r->error;
    uint32_t limit=q->width==8u?0xffu:q->width==16u?0xffffu:0xffffffffu;
    for(uint32_t i=0u;i<q->count;++i)if(values[i]>limit)return fail(r,R3_TABLE_VALUE);
    if(!begin(bus,q,r))return r->error;
    for(uint32_t i=0u;i<q->count;++i) {
        if(q->width==32u && !operation(bus,R3_PHY_WRITE,0x457u,0u,values[i]>>16u,r,0))return r->error;
        if(!operation(bus,R3_PHY_WRITE,0x456u,0u,values[i]&0xffffu,r,0))return r->error;
        ++r->entries_completed;
    }
    if(!finish(bus,q,r))return r->error;
    return R3_TABLE_OK;
}

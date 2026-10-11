#include "access.h"
static r3_phy_error_t parse(const uint16_t *words,uint32_t count,uint32_t *at,r3_phy_op_t *out) {
    uint32_t header=words[*at],kind=header>>13u,needed=(kind==0u||kind==4u)?3u:2u;
    if(count-*at<needed)return R3_PHY_TRUNCATED;
    out->kind=(r3_phy_kind_t)kind;out->index=header&0x1fffu;out->mask=0u;out->value=0u;
    if(kind==0u||kind==4u){out->mask=words[*at+1u];out->value=words[*at+2u];}
    else if(kind==1u||kind==5u)out->mask=words[*at+1u];
    else out->value=words[*at+1u];
    *at+=needed;return R3_PHY_OK;
}
r3_phy_error_t r3_phy_decode(const uint16_t *words,uint32_t count,r3_phy_op_t *out,uint32_t capacity,uint32_t *decoded) {
    if(!decoded)return R3_PHY_ARGUMENT;
    *decoded=0u;
    if(!words||!out||!count||count>R3_PHY_MAX_WORDS||!capacity||capacity>R3_PHY_MAX_OPERATIONS)return R3_PHY_ARGUMENT;
    uint32_t at=0u,n=0u;
    while(at<count) {
        if(n==capacity)return R3_PHY_CAPACITY;
        r3_phy_error_t error=parse(words,count,&at,&out[n]);
        if(error)return error;
        ++n;
    }
    *decoded=n;return R3_PHY_OK;
}
static void clear(r3_phy_report_t *report) {
    volatile r3_phy_report_t *p=report;
    p->error=R3_PHY_OK;p->io_completed=0u;p->operations=0u;p->address=0u;p->value=0u;
}
static r3_phy_error_t fail(r3_phy_report_t *r,r3_phy_error_t error){r->error=error;return error;}
static r3_phy_error_t validate(const r3_phy_op_t *op) {
    if(!op||op->index>0x1fffu||op->mask>0xffffu||op->value>0xffffu)return R3_PHY_ARGUMENT;
    switch(op->kind) {
    case R3_PHY_MOD:return R3_PHY_OK;
    case R3_PHY_AND:return op->value?R3_PHY_ARGUMENT:R3_PHY_OK;
    case R3_PHY_WRITE:case R3_PHY_OR:return op->mask?R3_PHY_ARGUMENT:R3_PHY_OK;
    case R3_PHY_READ:return (op->mask||op->value)?R3_PHY_ARGUMENT:R3_PHY_OK;
    case R3_RADIO_MOD:case R3_RADIO_AND:case R3_RADIO_WRITE:case R3_RADIO_OR:case R3_RADIO_READ:return R3_PHY_UNSUPPORTED;
    default:return R3_PHY_ARGUMENT;
    }
}
static int bus_valid(const r3_phy_bus_t *bus) {
    return bus && bus->read && bus->write && bus->base==R3_PHY_REGISTER_BASE;
}
static r3_phy_error_t write_value(const r3_phy_bus_t *bus,r3_phy_report_t *r,uint32_t address,uint32_t width,uint32_t value) {
    r->address=address;r->value=value;
    if(!bus->write(bus->context,address,width,value))return fail(r,R3_PHY_WRITE_ERROR);
    ++r->io_completed;return R3_PHY_OK;
}
static r3_phy_error_t read_value(const r3_phy_bus_t *bus,r3_phy_report_t *r,uint32_t address,uint32_t *value) {
    r->address=address;r->value=0u;
    if(!bus->read(bus->context,address,2u,value))return fail(r,R3_PHY_READ_ERROR);
    ++r->io_completed;r->value=*value;
    return *value>0xffffu?fail(r,R3_PHY_VALUE_ERROR):R3_PHY_OK;
}
static uint32_t modified(const r3_phy_op_t *op,uint32_t old) {
    return op->kind==R3_PHY_AND?old&op->mask:op->kind==R3_PHY_OR?old|op->value:
        (old&~op->mask)|(op->value&op->mask);
}
static r3_phy_error_t apply(const r3_phy_bus_t *bus,const r3_phy_op_t *op,r3_phy_report_t *r) {
    uint32_t selector=bus->base+0x3fcu,data=bus->base+0x3feu,old;
    if(op->kind==R3_PHY_WRITE) {
        if(write_value(bus,r,selector,4u,op->index|(op->value<<16u)))return r->error;
    } else {
        if(write_value(bus,r,selector,2u,op->index))return r->error;
        if(read_value(bus,r,data,&old))return r->error;
        if(op->kind!=R3_PHY_READ) {
            if(write_value(bus,r,data,2u,modified(op,old)))return r->error;
        }
    }
    ++r->operations;return R3_PHY_OK;
}
r3_phy_error_t r3_phy_execute(const r3_phy_bus_t *bus,const r3_phy_op_t *op,r3_phy_report_t *r) {
    if(!r)return R3_PHY_ARGUMENT;
    clear(r);
    if(!bus_valid(bus))return fail(r,R3_PHY_ARGUMENT);
    r3_phy_error_t error=validate(op);
    return error?fail(r,error):apply(bus,op,r);
}
r3_phy_error_t r3_phy_execute_table(const r3_phy_bus_t *bus,const uint16_t *words,uint32_t count,r3_phy_report_t *r) {
    if(!r)return R3_PHY_ARGUMENT;
    clear(r);
    if(!bus_valid(bus)||!words||!count||count>R3_PHY_MAX_WORDS)return fail(r,R3_PHY_ARGUMENT);
    uint32_t at=0u,n=0u;r3_phy_op_t op;
    /* Validate the entire immutable input before making the first bus access. */
    while(at<count) {
        if(n==R3_PHY_MAX_OPERATIONS)return fail(r,R3_PHY_CAPACITY);
        r3_phy_error_t error=parse(words,count,&at,&op);
        if(!error)error=validate(&op);
        if(error)return fail(r,error);
        ++n;
    }
    at=0u;
    while(at<count) {
        if(r->operations==R3_PHY_MAX_OPERATIONS)return fail(r,R3_PHY_CAPACITY);
        r3_phy_error_t error=parse(words,count,&at,&op);
        if(!error)error=validate(&op);
        if(error)return fail(r,error);
        if(apply(bus,&op,r))return r->error;
    }
    return R3_PHY_OK;
}
r3_phy_error_t r3_phy_pulse_044a(const r3_phy_bus_t *bus,r3_phy_report_t *r) {
    static const uint16_t pulse[4]={0x644au,0x0100u,0x244au,0xfeffu};
    return r3_phy_execute_table(bus,pulse,4u,r);
}
r3_phy_error_t r3_phy_override_4b0_43a(const r3_phy_bus_t *bus,uint32_t enabled,r3_phy_report_t *r) {
    if(!r)return R3_PHY_ARGUMENT;
    if(enabled>1u){clear(r);return fail(r,R3_PHY_ARGUMENT);}
    const uint16_t words[6]={0x04b0u,0x0080u,(uint16_t)(enabled<<7u),
                             0x043au,0x0040u,(uint16_t)(enabled<<6u)};
    return r3_phy_execute_table(bus,words,6u,r);
}
r3_phy_error_t r3_radio_execute(const r3_phy_bus_t *bus,const r3_phy_op_t *op,r3_phy_report_t *r) {
    if(!r)return R3_PHY_ARGUMENT;
    clear(r);
    if(!bus_valid(bus)||!op)return fail(r,R3_PHY_ARGUMENT);
    r3_phy_op_t normalized=*op;
    switch(op->kind) {
    case R3_RADIO_MOD:normalized.kind=R3_PHY_MOD;break;
    case R3_RADIO_AND:normalized.kind=R3_PHY_AND;break;
    case R3_RADIO_OR:normalized.kind=R3_PHY_OR;break;
    case R3_RADIO_WRITE:normalized.kind=R3_PHY_WRITE;break;
    case R3_RADIO_READ:normalized.kind=R3_PHY_READ;break;
    default:return fail(r,R3_PHY_UNSUPPORTED);
    }
    r3_phy_error_t error=validate(&normalized);
    if(error)return fail(r,error);
    if(op->index==1u)return fail(r,R3_PHY_UNSUPPORTED);
    uint32_t selector=bus->base+0x3d8u,data=bus->base+0x3dau,value=op->value;
    if(normalized.kind!=R3_PHY_WRITE) {
        uint32_t old;
        if(write_value(bus,r,selector,2u,op->index)||read_value(bus,r,data,&old))return r->error;
        if(normalized.kind==R3_PHY_READ){++r->operations;return R3_PHY_OK;}
        value=modified(&normalized,old);
    }
    if(write_value(bus,r,selector,2u,op->index)||write_value(bus,r,data,2u,value))return r->error;
    ++r->operations;return R3_PHY_OK;
}

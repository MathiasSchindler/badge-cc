#include "access.h"

static pmu_pll_status_t callback_status(pmu_pll_callback_t rc) {
    return rc==PMU_PLL_CB_OK?PMU_PLL_OK:rc==PMU_PLL_CB_FAILED?PMU_PLL_IO:
        rc==PMU_PLL_CB_CANCELLED?PMU_PLL_CANCELLED:rc==PMU_PLL_CB_TIMEOUT?PMU_PLL_DEADLINE:PMU_PLL_CALLBACK;
}
static pmu_pll_callback_t model_status(pmu_pll_status_t status) {
    return status==PMU_PLL_OK?PMU_PLL_CB_OK:status==PMU_PLL_CANCELLED?PMU_PLL_CB_CANCELLED:
        status==PMU_PLL_DEADLINE || status==PMU_PLL_POLL_LIMIT?PMU_PLL_CB_TIMEOUT:PMU_PLL_CB_FAILED;
}
static pmu_pll_access_event_t *record(pmu_pll_access_t *a,pmu_pll_operation_t op,uint32_t offset,uint32_t value) {
    uint32_t index=a->report.journal_count%PMU_PLL_ACCESS_JOURNAL;
    if(a->report.journal_count>=PMU_PLL_ACCESS_JOURNAL)++a->report.journal_overwritten;
    pmu_pll_access_event_t *e=&a->report.journal[index];
    e->sequence=++a->report.journal_count;e->call=a->report.calls;e->operation=op;
    e->offset=offset;e->value=value;e->status=PMU_PLL_OK;e->callback_result=PMU_PLL_CB_OK;
    return e;
}
static int halt(pmu_pll_access_t *a,pmu_pll_status_t status,pmu_pll_operation_t op,uint32_t offset,
                pmu_pll_callback_t rc) {
    if(a->report.status==PMU_PLL_OK) {
        a->report.status=status;a->report.failed_operation=op;
        a->report.failed_offset=offset;a->report.callback_result=rc;a->report.poisoned=1u;
        if(a->report.writes_attempted) {
            a->report.partial_write_uncertainty=1u;a->report.external_recovery_required=1u;
        }
    }
    return 0;
}
static int error(pmu_pll_access_t *a,pmu_pll_status_t status,pmu_pll_operation_t op,uint32_t offset,
                 pmu_pll_callback_t rc) {
    if(a->report.status!=PMU_PLL_OK)return 0;
    pmu_pll_access_event_t *e=record(a,op,offset,0u);e->status=status;e->callback_result=rc;
    return halt(a,status,op,offset,rc);
}
static int clock_check(pmu_pll_access_t *a,uint32_t *tick) {
    if(a->report.status!=PMU_PLL_OK)return 0;
    ++a->report.clocks;
    pmu_pll_callback_t rc=a->raw->now(a->raw->context,tick);
    if(a->report.status!=PMU_PLL_OK)return 0;
    if(rc!=PMU_PLL_CB_OK)return error(a,callback_status(rc),PMU_PLL_OP_CLOCK,0u,rc);
    a->report.elapsed=*tick-a->started;
    if(a->report.elapsed>=a->deadline)return error(a,PMU_PLL_DEADLINE,PMU_PLL_OP_CLOCK,0u,PMU_PLL_CB_OK);
    a->remaining=a->deadline-a->report.elapsed;
    if(a->active) {
        uint32_t elapsed=*tick-a->call_started;
        if(elapsed>=a->call_limit)return error(a,PMU_PLL_DEADLINE,PMU_PLL_OP_CLOCK,0u,PMU_PLL_CB_OK);
        uint32_t remaining=a->call_limit-elapsed;
        if(remaining<a->remaining)a->remaining=remaining;
    }
    return 1;
}
static int guard(pmu_pll_access_t *a) {
    uint32_t tick;
    if(!clock_check(a,&tick))return 0;
    ++a->report.services;
    pmu_pll_callback_t rc=a->raw->service(a->raw->context,a->remaining);
    if(a->report.status!=PMU_PLL_OK)return 0;
    if(rc!=PMU_PLL_CB_OK)return error(a,callback_status(rc),PMU_PLL_OP_SERVICE,0u,rc);
    return clock_check(a,&tick);
}
static int enter(pmu_pll_access_t *a,uint32_t budget) {
    if(!a || !a->initialized)return 0;
    if(a->report.status!=PMU_PLL_OK)return 0;
    if(a->active || !budget || budget>=0x80000000u)
        return error(a,PMU_PLL_INPUT,PMU_PLL_OP_NONE,0u,PMU_PLL_CB_OK);
    uint32_t tick;
    if(!clock_check(a,&tick))return 0;
    ++a->report.calls;a->active=1u;a->call_started=tick;
    a->call_limit=budget<a->remaining?budget:a->remaining;
    return 1;
}
static pmu_pll_status_t status(const pmu_pll_access_t *a) {
    return a && a->initialized?a->report.status:PMU_PLL_INPUT;
}
static pmu_pll_status_t leave(pmu_pll_access_t *a) {
    a->active=0u;return a->report.status;
}
static int valid_register(pmu_pll_access_t *a,uint32_t offset) {
    return (!(offset&3u) && offset<0x1000u) ||
        error(a,PMU_PLL_INPUT,PMU_PLL_OP_NONE,offset,PMU_PLL_CB_OK);
}
static int completed(pmu_pll_access_t *a,pmu_pll_access_event_t *e,pmu_pll_callback_t rc) {
    if(a->report.status!=PMU_PLL_OK)return 0;
    e->callback_result=rc;e->status=callback_status(rc);
    if(rc!=PMU_PLL_CB_OK)return halt(a,e->status,e->operation,e->offset,rc);
    return 1;
}
static int raw_read(pmu_pll_access_t *a,uint32_t offset,uint32_t *value) {
    if(!guard(a))return 0;
    pmu_pll_access_event_t *e=record(a,PMU_PLL_OP_READ,offset,0u);++a->report.reads;
    if(!completed(a,e,a->raw->read(a->raw->context,offset,a->remaining,value)))return 0;
    e->value=*value;uint32_t tick;return clock_check(a,&tick);
}
static int raw_write(pmu_pll_access_t *a,uint32_t offset,uint32_t value) {
    if(!guard(a))return 0;
    pmu_pll_access_event_t *e=record(a,PMU_PLL_OP_WRITE,offset,value);++a->report.writes_attempted;
    if(!completed(a,e,a->raw->write(a->raw->context,offset,value,a->remaining)))return 0;
    ++a->report.writes_acknowledged;uint32_t tick;return clock_check(a,&tick);
}
static int rmw(pmu_pll_access_t *a,uint32_t offset,uint32_t mask,uint32_t value,uint32_t *out) {
    if(mask && a->revision>=22u && (offset==0x614u || offset==0x634u || offset==0x644u)) {
        uint32_t observed=0u;
        for(uint32_t i=0u;i<a->poll_limit;++i) {
            if(!raw_read(a,0x608u,&observed))return 0;
            ++a->report.barrier_observations;
            if(!(observed&0x400u))break;
            if(i+1u==a->poll_limit)
                return error(a,PMU_PLL_POLL_LIMIT,PMU_PLL_OP_READ,0x608u,PMU_PLL_CB_OK);
        }
    }
    if(mask || value) {
        uint32_t old;
        if(!raw_read(a,offset,&old) || !raw_write(a,offset,(old&~mask)|value))return 0;
    }
    uint32_t readback;
    if(!raw_read(a,offset,&readback))return 0;
    *out=readback;a->report.last_readback=readback;++a->report.rmw_completed;return 1;
}
static int bank_rmw(pmu_pll_access_t *a,pmu_pll_bank_t bank,uint32_t index,uint32_t mask,uint32_t value,uint32_t *out) {
    if((bank!=PMU_PLL_BANK_PLL && bank!=PMU_PLL_BANK_REGULATOR) ||
       (bank==PMU_PLL_BANK_PLL && index>=PMU_PLL_WORDS) ||
       (bank==PMU_PLL_BANK_REGULATOR && index!=6u))
        return error(a,PMU_PLL_INPUT,PMU_PLL_OP_MASKED,index,PMU_PLL_CB_OK);
    uint32_t selector=bank==PMU_PLL_BANK_PLL?0x660u:0x658u,readback;
    if(!rmw(a,selector,0xffffffffu,index,&readback) || !rmw(a,selector+4u,mask,value,out))return 0;
    if(bank==PMU_PLL_BANK_PLL) {
        a->report.pll_readbacks[index]=*out;a->report.pll_readback_mask|=1u<<index;
    }
    return 1;
}
pmu_pll_status_t pmu_pll_access_init(pmu_pll_access_t *a,const pmu_pll_raw_io_t *raw,uint32_t revision,
                                    const pmu_pll_access_limits_t *limits,pmu_pll_owner_t owner) {
    if(!a)return PMU_PLL_INPUT;
    if(a->initialized) {
        error(a,PMU_PLL_INPUT,PMU_PLL_OP_NONE,0u,PMU_PLL_CB_OK);return a->report.status;
    }
    a->initialized=1u;
    if(!raw || !raw->read || !raw->write || !raw->service || !raw->now ||
       !limits || !limits->deadline_ticks || limits->deadline_ticks>=0x80000000u ||
       !limits->barrier_observations || limits->barrier_observations>PMU_PLL_ACCESS_POLLS_MAX ||
       revision>255u || owner!=PMU_PLL_OWNER_EXCLUSIVE) {
        error(a,PMU_PLL_INPUT,PMU_PLL_OP_NONE,0u,PMU_PLL_CB_OK);return a->report.status;
    }
    a->raw=raw;a->revision=revision;a->deadline=limits->deadline_ticks;a->poll_limit=limits->barrier_observations;
    ++a->report.clocks;
    pmu_pll_callback_t rc=raw->now(raw->context,&a->started);
    if(rc!=PMU_PLL_CB_OK)error(a,callback_status(rc),PMU_PLL_OP_CLOCK,0u,rc);
    return a->report.status;
}
pmu_pll_status_t pmu_pll_access_rmw(pmu_pll_access_t *a,uint32_t offset,uint32_t mask,uint32_t value,
                                   uint32_t budget,uint32_t *out) {
    if(!enter(a,budget))return status(a);
    if(!out)error(a,PMU_PLL_INPUT,PMU_PLL_OP_NONE,offset,PMU_PLL_CB_OK);
    else if(valid_register(a,offset))rmw(a,offset,mask,value,out);
    return leave(a);
}
pmu_pll_status_t pmu_pll_access_bank(pmu_pll_access_t *a,pmu_pll_bank_t bank,uint32_t index,uint32_t mask,
                                    uint32_t value,uint32_t budget,uint32_t *out) {
    if(!enter(a,budget))return status(a);
    if(!out)error(a,PMU_PLL_INPUT,PMU_PLL_OP_MASKED,index,PMU_PLL_CB_OK);
    else bank_rmw(a,bank,index,mask,value,out);
    return leave(a);
}
pmu_pll_status_t pmu_pll_access_pre_mask(pmu_pll_access_t *a,uint32_t owned_enable,uint32_t budget) {
    if(!enter(a,budget))return status(a);
    if(owned_enable>1u)error(a,PMU_PLL_INPUT,PMU_PLL_OP_NONE,0u,PMU_PLL_CB_OK);
    else if(!owned_enable)a->report.premask=PMU_PLL_PREMASK_DISABLED;
    else if(raw_read(a,0x504u,&a->report.sr0)) {
        if(!(a->report.sr0&1u))a->report.premask=PMU_PLL_PREMASK_SR_OFF;
        else {
            a->report.premask=PMU_PLL_PREMASK_READ_PLL;
            if(bank_rmw(a,PMU_PLL_BANK_PLL,8u,0u,0u,&a->report.pll8_before)) {
                if(!(a->report.pll8_before&2u))a->report.premask=PMU_PLL_PREMASK_PLL_CLEAR;
                else {
                    a->report.premask=PMU_PLL_PREMASK_CLEAR_PLL;
                    if(bank_rmw(a,PMU_PLL_BANK_PLL,8u,2u,0u,&a->report.pll8_readback)) {
                        a->report.premask=PMU_PLL_PREMASK_COMMIT;
                        if(rmw(a,0x600u,0x400u,0x400u,&a->report.commit_readback))
                            a->report.premask=PMU_PLL_PREMASK_APPLIED;
                    }
                }
            }
        }
    }
    return leave(a);
}
static pmu_pll_callback_t model_read(void *context,pmu_pll_register_t reg,uint32_t budget,uint32_t *out) {
    return model_status(pmu_pll_access_rmw(context,(uint32_t)reg,0u,0u,budget,out));
}
static pmu_pll_callback_t model_write(void *context,pmu_pll_register_t reg,uint32_t value,uint32_t budget) {
    pmu_pll_access_t *a=context;
    if(!enter(a,budget))return model_status(status(a));
    /* The table loop uses direct stores, not the masked-access gateway. */
    if(reg!=PMU_PLL_CONTROL && reg!=PMU_PLL_MIN && reg!=PMU_PLL_MAX &&
       reg!=PMU_PLL_SELECTOR && reg!=PMU_PLL_DATA)
        error(a,PMU_PLL_INPUT,PMU_PLL_OP_WRITE,(uint32_t)reg,PMU_PLL_CB_OK);
    else raw_write(a,(uint32_t)reg,value);
    return model_status(leave(a));
}
static pmu_pll_callback_t model_masked(void *context,pmu_pll_bank_t bank,uint32_t index,
                                     uint32_t mask,uint32_t value,uint32_t budget) {
    uint32_t readback;
    return model_status(pmu_pll_access_bank(context,bank,index,mask,value,budget,&readback));
}
static pmu_pll_callback_t model_delay(void *context,uint32_t argument,uint32_t budget) {
    pmu_pll_access_t *a=context;
    if(!enter(a,budget))return model_status(status(a));
    if(guard(a)) {
        pmu_pll_access_event_t *e=record(a,PMU_PLL_OP_DELAY,0u,argument);++a->report.delays;
        if(completed(a,e,a->raw->delay(a->raw->context,argument,a->remaining))) {
            uint32_t tick;clock_check(a,&tick);
        }
    }
    return model_status(leave(a));
}
static pmu_pll_callback_t model_service(void *context,uint32_t budget) {
    pmu_pll_access_t *a=context;
    if(!enter(a,budget))return model_status(status(a));
    guard(a);return model_status(leave(a));
}
static pmu_pll_callback_t model_now(void *context,uint32_t *tick) {
    pmu_pll_access_t *a=context;
    if(!a || !a->initialized)return PMU_PLL_CB_FAILED;
    clock_check(a,tick);return model_status(status(a));
}
static pmu_pll_status_t model_io(pmu_pll_access_t *a,pmu_pll_model_io_t *out) {
    if(!a || !a->initialized)return PMU_PLL_INPUT;
    if(a->report.status!=PMU_PLL_OK)return a->report.status;
    if(!out || !a->raw->delay || a->revision!=24u || a->active) {
        error(a,PMU_PLL_INPUT,PMU_PLL_OP_NONE,0u,PMU_PLL_CB_OK);return a->report.status;
    }
    out->context=a;out->read=model_read;out->write=model_write;out->masked_update=model_masked;
    out->delay=model_delay;out->service=model_service;out->now=model_now;return PMU_PLL_OK;
}
pmu_pll_status_t pmu_pll_access_run(pmu_pll_access_t *a,const pmu_pll_profile_t *profile,
                                   const pmu_pll_limits_t *limits,pmu_pll_report_t *report) {
    pmu_pll_model_io_t io;
    pmu_pll_status_t rc=model_io(a,&io);
    if(rc!=PMU_PLL_OK) {
        if(report) {
            for(uint32_t i=0u;i<sizeof(*report);++i)((uint8_t *)report)[i]=0u;
            report->status=rc;
            if(a && a->initialized) {
                report->operation=a->report.failed_operation;report->callback_result=a->report.callback_result;
                report->partial_write_uncertainty=a->report.partial_write_uncertainty;
                report->external_recovery_required=a->report.external_recovery_required;
            }
        }
        return rc;
    }
    rc=pmu_pll_model_run(profile,limits,&io,report);
    if(rc!=PMU_PLL_OK && a->report.status==PMU_PLL_OK)
        error(a,rc,report?report->operation:PMU_PLL_OP_NONE,report?report->last_register:0u,
              report?report->callback_result:PMU_PLL_CB_OK);
    if(rc!=PMU_PLL_OK && a->report.status!=PMU_PLL_OK) {
        rc=a->report.status;
        if(report)report->status=rc;
    }
    return rc;
}

#include "pll.h"

const uint8_t pmu_pll_firmware_sha256[32]={
    0x20,0x4e,0x80,0x2a,0x87,0xc9,0x7d,0x69,0x1b,0x14,0xeb,0x7c,0x42,0xc4,0x1d,0xba,
    0x6f,0xe1,0x09,0xc2,0x64,0x51,0x52,0x9f,0x4f,0x7a,0x05,0x36,0x34,0x2a,0x06,0x5f
};
static const uint32_t words[PMU_PLL_WORDS]={
    0u,0u,0x431u,0xba2e8cu,0x04040404u,0x00040104u,0x080004e2u,0xau,0x106au
};
pmu_pll_status_t pmu_pll_profile_check(const pmu_pll_profile_t *p) {
    if(!p)return PMU_PLL_INPUT;
    for(uint32_t i=0u;i<32u;++i)if(p->firmware_sha256[i]!=pmu_pll_firmware_sha256[i])return PMU_PLL_PROFILE;
    if(p->capabilities!=0x39d25f18u || (p->capabilities&255u)!=24u ||
       ((p->capabilities&0x003e0000u)>>17u)!=PMU_PLL_WORDS ||
       ((p->capabilities&0x1f00u)>>8u)!=31u || p->xtal_khz!=37400u || p->spur!=3u ||
       p->pll_overrides!=PMU_PLL_OVERRIDE_ABSENT)return PMU_PLL_PROFILE;
    return PMU_PLL_OK;
}
pmu_pll_status_t pmu_pll_word(const pmu_pll_profile_t *p,uint32_t index,uint32_t *out) {
    if(!out || index>=PMU_PLL_WORDS)return PMU_PLL_INPUT;
    pmu_pll_status_t status=pmu_pll_profile_check(p);
    if(status==PMU_PLL_OK)*out=words[index];
    return status;
}
pmu_pll_status_t pmu_pll_control(const pmu_pll_profile_t *p,uint32_t old,uint32_t *out) {
    if(!out)return PMU_PLL_INPUT;
    pmu_pll_status_t status=pmu_pll_profile_check(p);
    if(status==PMU_PLL_OK)
        *out=((old|0x200u)&0x0000ff83u)|((((p->xtal_khz+127u)/128u)-1u)<<16u)|0x3cu;
    return status;
}

typedef struct {
    const pmu_pll_model_io_t *io;
    pmu_pll_report_t *r;
    uint32_t deadline,pending_limit,remaining;
} model_t;
static int fail(model_t *m,pmu_pll_status_t status) {
    if(m->r->status==PMU_PLL_OK)m->r->status=status;
    if(m->r->writes_attempted || m->r->masked_attempted) {
        m->r->partial_write_uncertainty=1u;
        m->r->external_recovery_required=1u;
    }
    return 0;
}
static int result(model_t *m,pmu_pll_callback_t rc) {
    if(rc==PMU_PLL_CB_OK)return 1;
    m->r->callback_result=rc;
    return fail(m,rc==PMU_PLL_CB_FAILED?PMU_PLL_IO:
                  rc==PMU_PLL_CB_CANCELLED?PMU_PLL_CANCELLED:
                  rc==PMU_PLL_CB_TIMEOUT?PMU_PLL_DEADLINE:PMU_PLL_CALLBACK);
}
static int clock_check(model_t *m) {
    uint32_t now=0u;
    pmu_pll_callback_t rc=m->io->now(m->io->context,&now);
    if(rc!=PMU_PLL_CB_OK) { m->r->operation=PMU_PLL_OP_CLOCK;return result(m,rc); }
    m->r->elapsed=now-m->r->started;
    if(m->r->elapsed>=m->deadline)return fail(m,PMU_PLL_DEADLINE);
    m->remaining=m->deadline-m->r->elapsed;return 1;
}
static int guard(model_t *m) {
    if(!clock_check(m))return 0;
    ++m->r->service_calls;
    pmu_pll_callback_t rc=m->io->service(m->io->context,m->remaining);
    if(rc!=PMU_PLL_CB_OK) { m->r->operation=PMU_PLL_OP_SERVICE;return result(m,rc); }
    return clock_check(m);
}
static int read_register(model_t *m,pmu_pll_register_t reg,uint32_t *value) {
    m->r->operation=PMU_PLL_OP_READ;m->r->last_register=(uint32_t)reg;
    if(!guard(m))return 0;
    ++m->r->reads;
    if(!result(m,m->io->read(m->io->context,reg,m->remaining,value)))return 0;
    m->r->last_value=*value;return clock_check(m);
}
static int write_register(model_t *m,pmu_pll_register_t reg,uint32_t value) {
    m->r->operation=PMU_PLL_OP_WRITE;m->r->last_register=(uint32_t)reg;m->r->last_value=value;
    if(!guard(m))return 0;
    ++m->r->writes_attempted;
    if(m->r->state==PMU_PLL_MODEL_COMMIT)m->r->commit_attempted=1u;
    if(!result(m,m->io->write(m->io->context,reg,value,m->remaining)))return 0;
    ++m->r->writes_completed;return clock_check(m);
}
static int masked(model_t *m,pmu_pll_bank_t bank,uint32_t index,uint32_t mask,uint32_t value) {
    m->r->operation=PMU_PLL_OP_MASKED;m->r->last_bank=bank;
    m->r->last_index=index;m->r->last_mask=mask;m->r->last_value=value;
    if(!guard(m))return 0;
    ++m->r->masked_attempted;
    if(!result(m,m->io->masked_update(m->io->context,bank,index,mask,value,m->remaining)))return 0;
    ++m->r->masked_completed;return clock_check(m);
}
static int delay(model_t *m,uint32_t argument) {
    m->r->operation=PMU_PLL_OP_DELAY;m->r->last_value=argument;
    if(!guard(m))return 0;
    ++m->r->delay_calls;
    if(!result(m,m->io->delay(m->io->context,argument,m->remaining)))return 0;
    return clock_check(m);
}
static int prepare(model_t *m) {
    return masked(m,PMU_PLL_BANK_PLL,5u,0xffu,60u) &&
           masked(m,PMU_PLL_BANK_REGULATOR,6u,0x00800000u,0x00800000u) &&
           delay(m,30u) && masked(m,PMU_PLL_BANK_REGULATOR,6u,0x00800000u,0u);
}
static int timer_sample(model_t *m,uint32_t *out) {
    uint32_t second;
    if(!read_register(m,PMU_PLL_TIMER,out) || !read_register(m,PMU_PLL_TIMER,&second))return 0;
    return *out==second || read_register(m,PMU_PLL_TIMER,out);
}
static int pending_wait(model_t *m,int zero,uint32_t bound,uint32_t *elapsed,int *matched) {
    uint32_t previous;
    *elapsed=0u;*matched=0;
    if(!timer_sample(m,&previous))return 0;
    for(;;) {
        if(m->r->pending_zero_observations+m->r->pending_quiet_observations>=m->pending_limit)
            return fail(m,PMU_PLL_POLL_LIMIT);
        uint32_t pending;
        if(!read_register(m,PMU_PLL_PENDING,&pending))return 0;
        if(zero)++m->r->pending_zero_observations;else ++m->r->pending_quiet_observations;
        if((pending==0u)==zero) { *matched=1;return 1; }
        if(*elapsed>=bound)return 1;
        uint32_t tick;
        if(!timer_sample(m,&tick))return 0;
        uint32_t delta=tick-previous;previous=tick;
        if(delta>=0x80000000u)return fail(m,PMU_PLL_TIMER_DISCONTINUITY);
        if(delta>((0xffffffffu-*elapsed)>>5u))*elapsed=0xffffffffu;
        else *elapsed+=delta<<5u;
    }
}
static int settle(model_t *m) {
    uint32_t total=0u;
    for(;;) {
        uint32_t zero_elapsed,quiet_elapsed;int matched;
        if(!pending_wait(m,1,15000u,&zero_elapsed,&matched))return 0;
        if(!matched)return fail(m,PMU_PLL_PENDING_TIMEOUT);
        if(!pending_wait(m,0,64u,&quiet_elapsed,&matched))return 0;
        if(!matched)return 1;
        if(zero_elapsed>=15000u || quiet_elapsed>=15000u ||
           total>=15000u-zero_elapsed || total+zero_elapsed>=15000u-quiet_elapsed)
            return fail(m,PMU_PLL_PENDING_TIMEOUT);
        total+=zero_elapsed+quiet_elapsed;++m->r->pending_restarts;
    }
}
pmu_pll_status_t pmu_pll_settle_run(const pmu_pll_limits_t *limits,
                                  const pmu_pll_model_io_t *io,pmu_pll_report_t *r) {
    if(!r)return PMU_PLL_INPUT;
    for(uint32_t i=0u;i<sizeof(*r);++i)((uint8_t *)r)[i]=0u;
    r->state=PMU_PLL_MODEL_SETTLE;
    if(!limits || !limits->deadline_ticks || limits->deadline_ticks>=0x80000000u ||
       !limits->pending_observations || limits->pending_observations>PMU_PLL_PENDING_OBSERVATIONS_MAX ||
       !io || !io->read || !io->service || !io->now) {
        r->status=PMU_PLL_INPUT;return r->status;
    }
    model_t m={io,r,limits->deadline_ticks,limits->pending_observations,0u};
    r->operation=PMU_PLL_OP_CLOCK;
    if(!result(&m,io->now(io->context,&r->started)) || !settle(&m) || !clock_check(&m))return r->status;
    r->state=PMU_PLL_MODEL_END;r->operation=PMU_PLL_OP_NONE;
    return r->status;
}
static int wait_ht(model_t *m,int on) {
    uint32_t *observations=on?&m->r->ht_on_observations:&m->r->ht_off_observations;
    for(uint32_t i=0u;i<PMU_PLL_HT_OBSERVATIONS;++i) {
        uint32_t value;
        if(!read_register(m,PMU_PLL_CC_CLOCK,&value))return 0;
        ++*observations;
        if(((value&PMU_PLL_CC_HT)!=0u)==on)return 1;
        if(i+1u==PMU_PLL_HT_OBSERVATIONS)return fail(m,PMU_PLL_POLL_LIMIT);
        if(!delay(m,10u))return 0;
    }
    return fail(m,PMU_PLL_POLL_LIMIT);
}
pmu_pll_status_t pmu_pll_model_run(const pmu_pll_profile_t *profile,const pmu_pll_limits_t *limits,
                                  const pmu_pll_model_io_t *io,pmu_pll_report_t *r) {
    if(!r)return PMU_PLL_INPUT;
    for(uint32_t i=0u;i<sizeof(*r);++i)((uint8_t *)r)[i]=0u;
    r->status=pmu_pll_profile_check(profile);
    if(r->status!=PMU_PLL_OK)return r->status;
    if(!limits || !limits->deadline_ticks || limits->deadline_ticks>=0x80000000u ||
       !limits->pending_observations || limits->pending_observations>PMU_PLL_PENDING_OBSERVATIONS_MAX ||
       !io || !io->read || !io->write || !io->masked_update || !io->delay || !io->service || !io->now) {
        r->status=PMU_PLL_INPUT;return r->status;
    }
    model_t m={io,r,limits->deadline_ticks,limits->pending_observations,0u};
    r->operation=PMU_PLL_OP_CLOCK;
    if(!result(&m,io->now(io->context,&r->started)))return r->status;
    r->state=PMU_PLL_MODEL_INITIALIZE_CONTROL;
    for(;;) {
        uint32_t value,bits;
        switch(r->state) {
        case PMU_PLL_MODEL_INITIALIZE_CONTROL:
            if(!read_register(&m,PMU_PLL_CONTROL,&r->initial_control_before))return r->status;
            r->initial_control_requested=r->initial_control_before|0x200u;
            if(!write_register(&m,PMU_PLL_CONTROL,r->initial_control_requested))return r->status;
            r->initial_control_acknowledged=1u;
            break;
        case PMU_PLL_MODEL_MODIFYING_CHECK_PREP:
        case PMU_PLL_MODEL_PROGRAM_PREP:
            if(!prepare(&m))return r->status;
            break;
        case PMU_PLL_MODEL_CHECK_CONTROL:
            if(!read_register(&m,PMU_PLL_CONTROL,&r->control_before))return r->status;
            r->status=pmu_pll_control(profile,r->control_before,&r->control_checked);
            if(r->status!=PMU_PLL_OK) { fail(&m,r->status);return r->status; }
            if(!write_register(&m,PMU_PLL_CONTROL,r->control_checked))return r->status;
            break;
        case PMU_PLL_MODEL_SAVE:
            if(!read_register(&m,PMU_PLL_MIN,&r->saved_min) ||
               !read_register(&m,PMU_PLL_MAX,&r->saved_max) ||
               !read_register(&m,PMU_PLL_CC_CLOCK,&r->saved_clock))return r->status;
            break;
        case PMU_PLL_MODEL_SETTLE:
            if(!read_register(&m,PMU_PLL_CC_CLOCK,&value))return r->status;
            if(!(value&PMU_PLL_CC_HT) && !settle(&m))return r->status;
            break;
        case PMU_PLL_MODEL_CLEAR_MIN:
        case PMU_PLL_MODEL_CLEAR_MAX: {
            pmu_pll_register_t reg=r->state==PMU_PLL_MODEL_CLEAR_MIN?PMU_PLL_MIN:PMU_PLL_MAX;
            if(!read_register(&m,reg,&value) || !write_register(&m,reg,value&~PMU_PLL_HT_MASK))return r->status;
            break;
        }
        case PMU_PLL_MODEL_WAIT_OFF:
            if(!wait_ht(&m,0))return r->status;
            break;
        case PMU_PLL_MODEL_DELAY_OFF:
            if(!delay(&m,100u))return r->status;
            break;
        case PMU_PLL_MODEL_TABLE:
            for(uint32_t i=0u;i<PMU_PLL_WORDS;++i) {
                if(!write_register(&m,PMU_PLL_SELECTOR,i) || !write_register(&m,PMU_PLL_DATA,words[i]))return r->status;
                ++r->pll_words_written;
            }
            break;
        case PMU_PLL_MODEL_COMMIT:
            if(!read_register(&m,PMU_PLL_CONTROL,&value) || !write_register(&m,PMU_PLL_CONTROL,value|0x400u))return r->status;
            r->commit_acknowledged=1u;break;
        case PMU_PLL_MODEL_RESTORE_MAX:
            bits=r->saved_max&PMU_PLL_HT_MASK;
            if(bits) {
                if(!read_register(&m,PMU_PLL_MAX,&value) || !write_register(&m,PMU_PLL_MAX,value|bits))return r->status;
                r->restore_max_written=1u;
            }
            break;
        case PMU_PLL_MODEL_RESTORE_MIN:
            bits=r->saved_min&PMU_PLL_HT_MASK;
            if(bits) {
                if(!read_register(&m,PMU_PLL_MIN,&value) || !write_register(&m,PMU_PLL_MIN,value|bits))return r->status;
                r->restore_min_written=1u;
            }
            break;
        case PMU_PLL_MODEL_WAIT_RESTORE:
            if(r->saved_clock&PMU_PLL_CC_HT) {
                r->ht_restore_requested=1u;
                if(!wait_ht(&m,1))return r->status;
                r->ht_restore_observed=1u;
            }
            break;
        case PMU_PLL_MODEL_END:
            if(!clock_check(&m))return r->status;
            r->operation=PMU_PLL_OP_NONE;r->subsequence_applied=1u;return PMU_PLL_OK;
        default:
            fail(&m,PMU_PLL_INPUT);return r->status;
        }
        r->state=(pmu_pll_state_t)((uint32_t)r->state+1u);
    }
}

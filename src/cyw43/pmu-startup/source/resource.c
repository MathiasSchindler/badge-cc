#include "resource.h"
#include "../resources.h"

#define CONTROL 0x600u
#define CAPABILITIES 0x604u
#define CLOCK 0x1e0u
#define SR0 0x504u
#define SR1 0x508u
#define MINIMUM 0x618u
#define MAXIMUM 0x61cu
#define SELECTOR 0x620u
#define DEPENDENCY 0x624u
#define TIMING 0x628u
#define BASE_MINIMUM 0x0e820001u
#define FINAL_MINIMUM 0x0ec23df7u

static pmur_status_t profile_base(const pmur_profile_t *p) {
    if(!p)return PMUR_INPUT;
    for(uint32_t i=0u;i<32u;++i)if(p->firmware_sha256[i]!=pmu_pll_firmware_sha256[i])return PMUR_PROFILE;
    if(p->capabilities!=0x39d25f18u || p->xtal_khz!=37400u || p->pll_verified!=1u ||
       p->exclusive_owner!=1u || p->overrides!=PMUR_OVERRIDE_ABSENT)return PMUR_PROFILE;
    return PMUR_OK;
}
static int clock_state(uint32_t clock) {
    return clock==0x50040u || clock==0x70040u || clock==0x30040u || clock==0xb0040u;
}
pmur_status_t pmu_resource_profile_check(const pmur_profile_t *p) {
    pmur_status_t base=profile_base(p);
    if(base!=PMUR_OK)return base;
    if(p->delay.qualified!=1u || p->delay.control!=0x012403bdu || !clock_state(p->delay.clock) ||
       p->delay.sr0 || p->delay.sr1 || p->delay.counter!=0x18003090u ||
       p->delay.target_ticks!=PMU_RESOURCE_WAIT_TICKS)return PMUR_PROFILE;
    return PMUR_OK;
}
typedef struct {
    const pmur_io_t *io;
    pmur_report_t *r;
    uint32_t deadline,remaining;
} model_t;
static int fail(model_t *m,pmur_status_t status) {
    if(m->r->status==PMUR_OK)m->r->status=status;
    if(m->r->writes_attempted) {
        m->r->partial_write_uncertainty=1u;m->r->external_recovery_required=1u;
    }
    return 0;
}
static int result(model_t *m,pmu_pll_callback_t rc) {
    if(rc==PMU_PLL_CB_OK)return m->r->status==PMUR_OK;
    m->r->callback_result=rc;
    return fail(m,rc==PMU_PLL_CB_FAILED?PMUR_IO:rc==PMU_PLL_CB_CANCELLED?PMUR_CANCELLED:
                  rc==PMU_PLL_CB_TIMEOUT?PMUR_DEADLINE:PMUR_CALLBACK);
}
static int clock_check(model_t *m) {
    if(m->r->status!=PMUR_OK)return 0;
    uint32_t now=0u;
    pmu_pll_callback_t rc=m->io->now(m->io->context,&now);
    if(rc!=PMU_PLL_CB_OK) { m->r->operation=PMU_PLL_OP_CLOCK;return result(m,rc); }
    m->r->elapsed=now-m->r->started;
    if(m->r->elapsed>=m->deadline)return fail(m,PMUR_DEADLINE);
    m->remaining=m->deadline-m->r->elapsed;return m->r->status==PMUR_OK;
}
static int guard(model_t *m) {
    if(!clock_check(m))return 0;
    ++m->r->services;
    pmu_pll_callback_t rc=m->io->service(m->io->context,m->remaining);
    if(rc!=PMU_PLL_CB_OK)m->r->operation=PMU_PLL_OP_SERVICE;
    return result(m,rc) && clock_check(m);
}
static int read_register(model_t *m,uint32_t offset,uint32_t *value) {
    m->r->operation=PMU_PLL_OP_READ;m->r->last_offset=offset;
    if(!guard(m))return 0;
    ++m->r->reads;
    if(!result(m,m->io->read(m->io->context,offset,m->remaining,value)))return 0;
    m->r->last_value=*value;return clock_check(m);
}
static int write_register(model_t *m,uint32_t offset,uint32_t value) {
    m->r->operation=PMU_PLL_OP_WRITE;m->r->last_offset=offset;m->r->last_value=value;
    if(!guard(m))return 0;
    ++m->r->writes_attempted;
    if(!result(m,m->io->write(m->io->context,offset,value,m->remaining)))return 0;
    ++m->r->writes_completed;return clock_check(m);
}
static int expect(model_t *m,uint32_t offset,uint32_t wanted) {
    uint32_t value;
    return read_register(m,offset,&value) && (value==wanted || fail(m,PMUR_READBACK));
}
static int domain(model_t *m) {
    return expect(m,CONTROL,0x012403bdu) && expect(m,CLOCK,0x50040u) &&
           expect(m,SR0,0u) && expect(m,SR1,0u);
}
static int post_domain(model_t *m,uint32_t expected,uint32_t polls) {
    if(!expect(m,CONTROL,0x012403bdu))return 0;
    for(uint32_t i=0u;i<polls;++i) {
        uint32_t value;
        if(!read_register(m,CLOCK,&value))return 0;
        if(!clock_state(value))return fail(m,PMUR_READBACK);
        if(value==expected)return expect(m,SR0,0u) && expect(m,SR1,0u);
    }
    return fail(m,PMUR_WAIT_FAILED);
}
static pmu_pll_callback_t settling_read(void *context,pmu_pll_register_t offset,uint32_t budget,uint32_t *out) {
    model_t *m=context;
    if(m->r->status!=PMUR_OK || (offset!=PMU_PLL_TIMER && offset!=PMU_PLL_PENDING))return PMU_PLL_CB_FAILED;
    m->r->operation=PMU_PLL_OP_READ;m->r->last_offset=(uint32_t)offset;++m->r->reads;
    pmu_pll_callback_t rc=m->io->read(m->io->context,(uint32_t)offset,budget,out);
    if(rc==PMU_PLL_CB_OK)m->r->last_value=*out;
    return rc;
}
static pmu_pll_callback_t settling_service(void *context,uint32_t budget) {
    model_t *m=context;
    if(m->r->status!=PMUR_OK)return PMU_PLL_CB_FAILED;
    ++m->r->services;return m->io->service(m->io->context,budget);
}
static pmu_pll_callback_t settling_now(void *context,uint32_t *out) {
    model_t *m=context;
    if(m->r->status!=PMUR_OK)return PMU_PLL_CB_FAILED;
    pmu_pll_callback_t rc=m->io->now(m->io->context,out);
    if(rc==PMU_PLL_CB_OK) {
        m->r->elapsed=*out-m->r->started;
        if(m->r->elapsed>=m->deadline) {
            fail(m,PMUR_DEADLINE);return PMU_PLL_CB_TIMEOUT;
        }
        m->remaining=m->deadline-m->r->elapsed;
    }
    return rc;
}
static pmur_status_t resource_run(const pmur_profile_t *p,const pmu_pll_limits_t *limits,
                                 const pmur_io_t *io,pmur_report_t *r,int prepare_only) {
    if(!r)return PMUR_INPUT;
    if(r->attempted) {
        model_t m={io,r,0u,0u};fail(&m,PMUR_REENTRY);return PMUR_REENTRY;
    }
    for(uint32_t i=0u;i<sizeof(*r);++i)((uint8_t *)r)[i]=0u;
    r->attempted=1u;r->status=prepare_only?profile_base(p):pmu_resource_profile_check(p);
    if(r->status!=PMUR_OK)return r->status;
    if(!limits || !limits->deadline_ticks || limits->deadline_ticks>=0x80000000u ||
       !limits->pending_observations || limits->pending_observations>PMU_PLL_PENDING_OBSERVATIONS_MAX ||
       !io || !io->read || !io->write || !io->service || !io->now || (!prepare_only && !io->wait_ticks)) {
        r->status=PMUR_INPUT;return r->status;
    }
    model_t m={io,r,limits->deadline_ticks,0u};
    r->operation=PMU_PLL_OP_CLOCK;
    if(!result(&m,io->now(io->context,&r->started)))return r->status;
    r->phase=PMUR_GUARD;
    if(!expect(&m,CAPABILITIES,p->capabilities) || !domain(&m) ||
       !read_register(&m,MINIMUM,&r->initial_min) || !read_register(&m,MAXIMUM,&r->initial_max))return r->status;
    if(r->initial_min!=0x0fcafd77u || r->initial_max!=0x0fcefd77u) {
        fail(&m,PMUR_READBACK);return r->status;
    }
    uint32_t hz;
    if(pmu_resource_alp_hz(0x012403bdu,&hz)!=PMU_PLAN_OK || hz!=37400000u) {
        fail(&m,PMUR_PROFILE);return r->status;
    }
    r->phase=PMUR_TIMINGS;
    for(uint32_t i=0u;i<PMU_TIMING_STEPS;++i) {
        pmu_timing_step_t step;
        r->index=i;
        if(pmu_resource_timing_for_clock(hz,i,&step)!=PMU_PLAN_OK) {
            fail(&m,PMUR_PROFILE);return r->status;
        }
        if(!write_register(&m,SELECTOR,step.resource) || !write_register(&m,TIMING,step.timing))return r->status;
        ++r->timing_records;
    }
    r->phase=PMUR_DEPENDENCIES;
    for(uint32_t i=PMU_RESOURCE_COUNT;i>0u;--i) {
        uint32_t value,index=i-1u;r->index=index;
        if(pmu_resource_dependency(index,&value)!=PMU_PLAN_OK) {
            fail(&m,PMUR_PROFILE);return r->status;
        }
        if(!write_register(&m,SELECTOR,index) || !write_register(&m,DEPENDENCY,value))return r->status;
        ++r->dependency_records;
    }
    r->phase=PMUR_EXPAND;r->minimum=BASE_MINIMUM;r->maximum=PMU_RESOURCE_MASK;
    for(uint32_t i=0u;i<PMU_RESOURCE_COUNT;++i)if(BASE_MINIMUM&(1u<<i)) {
        uint32_t expected,value;r->index=i;
        if(pmu_resource_dependency(i,&expected)!=PMU_PLAN_OK) {
            fail(&m,PMUR_PROFILE);return r->status;
        }
        if(!write_register(&m,SELECTOR,i) || !read_register(&m,DEPENDENCY,&value))return r->status;
        if(value!=expected) { fail(&m,PMUR_READBACK);return r->status; }
        r->minimum|=value;++r->dependency_reads;
    }
    if(r->minimum!=FINAL_MINIMUM) { fail(&m,PMUR_READBACK);return r->status; }
    r->phase=PMUR_PREMASK;
    if(!domain(&m) || !expect(&m,MINIMUM,r->initial_min))return r->status;
    r->phase=PMUR_ENABLE_MAX;
    uint32_t maximum;
    if(!read_register(&m,MAXIMUM,&maximum))return r->status;
    if(maximum!=r->initial_max) { fail(&m,PMUR_READBACK);return r->status; }
    if(!write_register(&m,MAXIMUM,maximum|r->maximum))return r->status;
    r->phase=PMUR_INSTALL_MIN;
    if(!write_register(&m,MINIMUM,r->minimum))return r->status;
    r->phase=PMUR_INSTALL_MAX;
    if(!write_register(&m,MAXIMUM,r->maximum))return r->status;
    r->phase=PMUR_SETTLE;
    if(!clock_check(&m))return r->status;
    const pmu_pll_limits_t settle_limits={m.remaining,limits->pending_observations};
    const pmu_pll_model_io_t settle_io={.context=&m,.read=settling_read,.service=settling_service,.now=settling_now};
    if(pmu_pll_settle_run(&settle_limits,&settle_io,&r->settling)!=PMU_PLL_OK) {
        r->callback_result=r->settling.callback_result;fail(&m,PMUR_SETTLE_FAILED);return r->status;
    }
    r->phase=PMUR_DELAY;
    if(prepare_only) {
        r->status=PMUR_PREPARED;r->external_recovery_required=1u;return r->status;
    }
    if(!post_domain(&m,p->delay.clock,limits->pending_observations) || !guard(&m))return r->status;
    r->operation=PMU_PLL_OP_DELAY;
    if(!result(&m,io->wait_ticks(io->context,p->delay.target_ticks,m.remaining,&r->wait_ticks)) ||
       !clock_check(&m))return r->status;
    if(r->wait_ticks<p->delay.target_ticks || r->wait_ticks>=0x80000000u) {
        fail(&m,PMUR_WAIT_FAILED);return r->status;
    }
    r->phase=PMUR_VERIFY;
    if(!post_domain(&m,p->delay.clock,1u) || !expect(&m,MINIMUM,r->minimum) || !expect(&m,MAXIMUM,r->maximum))return r->status;
    r->subsequence_applied=1u;r->phase=PMUR_DONE;r->operation=PMU_PLL_OP_NONE;
    return r->status;
}
pmur_status_t pmu_resource_model_run(const pmur_profile_t *p,const pmu_pll_limits_t *limits,
                                    const pmur_io_t *io,pmur_report_t *r) {
    return resource_run(p,limits,io,r,0);
}
pmur_status_t pmu_resource_prepare_run(const pmur_profile_t *p,const pmu_pll_limits_t *limits,
                                      const pmur_io_t *io,pmur_report_t *r) {
    return resource_run(p,limits,io,r,1);
}

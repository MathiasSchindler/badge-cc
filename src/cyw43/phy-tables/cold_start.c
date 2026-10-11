#include "cold_start.h"
#include "../../include/r0_hash.h"

static uint32_t packed_value(const uint8_t *p,uint32_t count) {
    uint32_t value=0u;
    for(uint32_t i=0u;i<count;++i)value|=(uint32_t)p[i]<<(8u*i);
    return value;
}
r3_rx_cold_error_t r3_rx_cold_validate(const r3_rx_board_t *b) {
    static const uint32_t offsets[]={0u,164u,468u,852u,878u};
    static const uint32_t sizes[]={164u,304u,384u,26u,1164u};
    static const uint32_t crcs[]={0xedaa1b5eu,0xb11830b4u,0x60ac5f7au,0x58e7a824u,0xd4145ee1u};
    static const uint32_t common_offsets[]={0x446u,0x546u,0x5c6u,0x6c6u,0x36eu};
    static const uint32_t counts[]={128u,64u,256u,154u,108u};
    static const uint16_t channel1[]={
        0x4b,0x176,0x13,0x3032,0x16,0x3,0x7e41,0x600,0xa00,0x3d9,0x27,0x25,0,0x991,0,0x96c,
        0,0,0x305,0xe75b,8,0x583f,0x1212,0x142f,0x1f1f,0x1f1f,0x5120,0x6127,0,0x6009,0x703f
    };
    if(!b)return R3_COLD_ARGUMENT;
    if(b->ready!=1u||b->core_flags!=0u||b->xtal_hz!=37400000u||
       b->channel.xtal_hz!=b->xtal_hz||b->channel.channel!=1u||b->channel.frequency_mhz!=2412u||
       b->channel.words!=b->channel_words||b->channel.capacity!=31u||
       b->gains.gainval!=b->gainval||b->gains.gain!=b->gain||b->gains.gainidx!=b->gainidx||
       b->gains.gainval_capacity!=26u||b->gains.gain_capacity!=96u||b->gains.gainidx_capacity!=76u||
       b->agc.alternate_gain_settings!=0u||b->agc.threshold_104!=0u||b->agc.aci_threshold_reset!=1u||
       b->agc.gain_787_high!=187u||b->agc.gain_432_high!=181u)return R3_COLD_BOARD;
    /* Captured alignment padding lies outside the five dataset CRCs. */
    if(b->packed[2042u]||b->packed[2043u])return R3_COLD_BOARD;
    uint32_t out=0u;
    for(uint32_t i=0u;i<5u;++i) {
        if(b->common.data[i]!=b->common_values+out||b->common.capacity[i]!=counts[i]||
           (r0_crc32_update(0xffffffffu,b->packed+offsets[i],sizes[i])^0xffffffffu)!=crcs[i])
            return R3_COLD_BOARD;
        uint32_t width=i==2u?1u:2u;
        for(uint32_t j=0u;j<counts[i];++j)
            if(b->common_values[out++]!=packed_value(b->packed+common_offsets[i]+j*width,width))
                return R3_COLD_BOARD;
    }
    for(uint32_t i=0u;i<56u;++i)
        if(b->tia16[i]!=packed_value(b->packed+2u*i,2u))return R3_COLD_BOARD;
    for(uint32_t i=0u;i<52u;++i)if(b->tia8[i]!=b->packed[112u+i])return R3_COLD_BOARD;
    for(uint32_t i=0u;i<76u;++i)
        if(b->gainidx[i]!=packed_value(b->packed+164u+4u*i,4u))return R3_COLD_BOARD;
    for(uint32_t i=0u;i<96u;++i)
        if(b->gain[i]!=packed_value(b->packed+468u+4u*i,4u))return R3_COLD_BOARD;
    for(uint32_t i=0u;i<26u;++i)if(b->gainval[i]!=b->packed[852u+i])return R3_COLD_BOARD;
    for(uint32_t i=0u;i<31u;++i)if(b->channel_words[i]!=channel1[i])return R3_COLD_BOARD;
    return R3_COLD_OK;
}

typedef struct {
    r3_phy_bus_t raw;
    r3_vco_time_t time;
    r3_rx_cold_limits_t limits;
    r3_rx_cold_report_t *report;
} cold_context_t;
static int fail(r3_rx_cold_report_t *r,r3_rx_cold_error_t error) {
    if(!r->error)r->error=error;
    return 0;
}
static int cancellation(cold_context_t *c) {
    r3_rx_cold_report_t *r=c->report;
    if(r->error)return 0;
    ++r->cancel_checks;
    int value=c->time.cancelled(c->time.context);
    if(value==1)return fail(r,R3_COLD_CANCELLED);
    return value==0?1:fail(r,R3_COLD_SERVICE);
}
static int gate(cold_context_t *c) {
    r3_rx_cold_report_t *r=c->report;
    uint32_t now;
    if(!cancellation(c))return 0;
    if(r->clock_samples==c->limits.max_clock_samples)return fail(r,R3_COLD_CLOCK_LIMIT);
    ++r->clock_samples;
    if(c->time.now(c->time.context,&now)!=1)return fail(r,R3_COLD_CLOCK_IO);
    if(r->clock_samples==1u)r->start=now;
    else {
        r->stalled_samples=now==r->last?r->stalled_samples+1u:0u;
        if(r->stalled_samples>=c->limits.max_stalled_samples)return fail(r,R3_COLD_CLOCK_STALLED);
    }
    r->last=now;r->elapsed=now-r->start;
    if(r->elapsed>=c->limits.budget_ticks)return fail(r,R3_COLD_TIMEOUT);
    return 1;
}
static int timed_read(void *context,uint32_t address,uint32_t width,uint32_t *value) {
    cold_context_t *c=context;r3_rx_cold_report_t *r=c->report;
    if(!gate(c))return 0;
    ++r->io_attempts;
    if(c->raw.read(c->raw.context,address,width,value)!=1)return fail(r,R3_COLD_IO);
    ++r->io_completed;++r->phase_io[r->phase];
    return gate(c);
}
static int timed_write(void *context,uint32_t address,uint32_t width,uint32_t value) {
    cold_context_t *c=context;r3_rx_cold_report_t *r=c->report;
    if(!gate(c))return 0;
    ++r->io_attempts;++r->write_attempts;
    if(c->raw.write(c->raw.context,address,width,value)!=1)return fail(r,R3_COLD_IO);
    ++r->io_completed;++r->writes_completed;++r->phase_io[r->phase];
    return gate(c);
}
static int timed_now(void *context,uint32_t *now) {
    cold_context_t *c=context;
    if(!gate(c))return 0;
    *now=c->report->last;return 1;
}
static int timed_delay(void *context,uint32_t argument) {
    cold_context_t *c=context;r3_rx_cold_report_t *r=c->report;
    if(!gate(c))return 0;
    ++r->delay_attempts;r->last_delay=argument;
    if(c->time.delay_argument(c->time.context,argument)!=1)return fail(r,R3_COLD_DELAY_IO);
    ++r->delays_completed;return gate(c);
}
static int timed_cancelled(void *context) {
    cold_context_t *c=context;
    return cancellation(c)?0:c->report->error==R3_COLD_CANCELLED?1:-1;
}
static int guard_check(r3_tx_guard_t *g,r3_rx_cold_report_t *r) {
    r->guard_error=r3_tx_guard_check(g);
    if(r->guard_error)return fail(r,R3_COLD_GUARD);
    ++r->guard_checks;return 1;
}
static uint32_t expected_index(uint32_t table,uint32_t index,uint32_t alias64) {
    return alias64&&table==2u?192u+(index&63u):index;
}
static r3_rx_cold_error_t recheck_tables(const r3_phy_bus_t *bus,const r3_rx_board_t *b,r3_rx_cold_verify_t *v,uint32_t alias64) {
    static const uint32_t ids[]={2u,1u,20u,23u,0u,17u,18u,13u};
    static const uint32_t widths[]={16u,16u,8u,16u,16u,8u,32u,32u};
    static const uint32_t counts[]={128u,64u,256u,154u,108u,26u,96u,76u};
    v->kind=R3_COLD_VERIFY_TABLE;
    for(uint32_t table=0u;table<8u;++table) {
        const uint32_t *data=table<5u?b->common.data[table]:
                             table==5u?b->gainval:table==6u?b->gain:b->gainidx;
        v->table_id=ids[table];v->width=widths[table];
        for(uint32_t at=0u;at<counts[table];at+=16u) {
            uint32_t count=counts[table]-at;
            if(count>16u)count=16u;
            v->index=at;v->expected=data[expected_index(table,at,alias64)];v->actual=0u;
            const r3_table_request_t request={ids[table],at,count,widths[table]};
            if(r3_phy_table_read(bus,&request,v->values,16u,&v->table)!=R3_TABLE_OK) {
                v->index=at+v->table.entries_completed;
                if(v->index<counts[table])v->expected=data[expected_index(table,v->index,alias64)];
                return R3_COLD_BLOCK;
            }
            for(uint32_t i=0u;i<count;++i) {
                v->index=at+i;v->expected=data[expected_index(table,at+i,alias64)];v->actual=v->values[i];
                if(v->actual!=v->expected)return R3_COLD_TABLE_MISMATCH;
                ++v->entries_verified;
            }
        }
        ++v->tables_verified;
    }
    return R3_COLD_OK;
}
static r3_rx_cold_error_t recheck_defaults(const r3_phy_bus_t *bus,r3_rx_cold_verify_t *v) {
    static const uint32_t indices[]={0x44cu,0x938u};
    static const uint32_t values[]={0x4200u,0x800u};
    v->kind=R3_COLD_VERIFY_PHY;v->table_id=0u;v->width=16u;
    for(uint32_t i=0u;i<2u;++i) {
        v->index=indices[i];v->expected=values[i];v->actual=0u;
        const r3_phy_op_t op={R3_PHY_READ,indices[i],0u,0u};
        if(r3_phy_execute(bus,&op,&v->phy)!=R3_PHY_OK)return R3_COLD_BLOCK;
        v->actual=v->phy.value;
        if(v->actual!=v->expected)return R3_COLD_DEFAULT_MISMATCH;
        ++v->defaults_verified;
    }
    return R3_COLD_OK;
}
r3_rx_cold_error_t r3_rx_cold_recheck(const r3_phy_bus_t *bus,const r3_rx_board_t *b,
                                    r3_rx_cold_verify_t *v,uint32_t alias64) {
    if(!bus||!bus->read||!bus->write||!b||!v||alias64>1u||v->entries_verified||v->defaults_verified)
        return R3_COLD_ARGUMENT;
    r3_rx_cold_error_t error=recheck_tables(bus,b,v,alias64);
    return error?error:recheck_defaults(bus,v);
}
static r3_rx_cold_error_t cold_start(r3_tx_guard_t *g,const r3_rx_board_t *b,
                                   const r3_vco_time_t *time,const r3_rx_cold_limits_t *limits,
                                   r3_rx_cold_report_t *r,uint32_t alias64) {
    if(!r)return R3_COLD_ARGUMENT;
    volatile uint8_t *bytes=(volatile uint8_t *)r;
    for(uint32_t i=0u;i<sizeof(*r);++i)bytes[i]=0u;
    if(!g||!time||!time->now||!time->delay_argument||!time->cancelled||!limits||
       !limits->budget_ticks||limits->budget_ticks>0x7fffffffu||
       !limits->max_clock_samples||limits->max_clock_samples>1000000u||
       !limits->max_stalled_samples||limits->max_stalled_samples>limits->max_clock_samples||
       !limits->calibration_samples||limits->calibration_samples>100u||
       !limits->dc_samples||limits->dc_samples>10u) {
        (void)fail(r,R3_COLD_ARGUMENT);return r->error;
    }
    if(g->phase!=R3_TG_ARMED||g->pa_phase!=R3_TG_PA_UNBOUND||g->error!=R3_TG_OK) {
        r->reset_required=g->phase==R3_TG_FAULT;r->guard_error=g->error;
        (void)fail(r,R3_COLD_GUARD);return r->error;
    }
    if(g->bus.context!=g||!g->bus.read||!g->bus.write||g->bus.base!=R3_PHY_REGISTER_BASE||
       !g->raw.read||!g->raw.write||g->raw.base!=R3_PHY_REGISTER_BASE||
       g->raw.read==timed_read||g->raw.write==timed_write) {
        (void)fail(r,R3_COLD_ARGUMENT);return r->error;
    }
    r->error=r3_rx_cold_validate(b);
    if(r->error)return r->error;
    cold_context_t context={g->raw,*time,*limits,r};
    const r3_vco_time_t bounded={&context,timed_now,timed_delay,timed_cancelled};
    const r3_vco_limits_t calibration={limits->budget_ticks,limits->calibration_samples};
    const r3_vco_limits_t dc={limits->budget_ticks,limits->dc_samples};
    r->reset_required=1u;
    g->raw=(r3_phy_bus_t){&context,timed_read,timed_write,R3_PHY_REGISTER_BASE};
    if(gate(&context))for(r->phase=R3_COLD_RADIO_RESET;r->phase<R3_COLD_CONFIGURED;++r->phase) {
        int ok=0;
        switch(r->phase) {
        case R3_COLD_RADIO_RESET:
            r->path_error=r3_rx_radio_reset(g,&r->detail.path);ok=r->path_error==R3_RX_PATH_OK;break;
        case R3_COLD_PA_ARM:
            r->guard_error=r3_tx_guard_arm_radio_pa(g);ok=r->guard_error==R3_TG_OK;break;
        case R3_COLD_BAND:
            r->path_error=r3_rx_band_apply(g,&r->detail.path);ok=r->path_error==R3_RX_PATH_OK;break;
        case R3_COLD_COMMON:
            r->config_error=r3_rx_common_write(&g->bus,&b->common,&r->detail.config);
            ok=r->config_error==R3_RX_CONFIG_OK;break;
        case R3_COLD_GAINS:
            r->config_error=r3_rx_gains_write(&g->bus,&b->gains,&r->detail.config);
            ok=r->config_error==R3_RX_CONFIG_OK;break;
        case R3_COLD_DEFAULTS:
            r->path_error=r3_rx_register_defaults_apply(g,&r->detail.path);
            ok=r->path_error==R3_RX_PATH_OK;break;
        case R3_COLD_TABLE_VERIFY: {
            r3_rx_cold_error_t error=recheck_tables(&g->bus,b,&r->verification,alias64);
            ok=error==R3_COLD_OK;if(error)(void)fail(r,error);break;
        }
        case R3_COLD_DEFAULT_VERIFY: {
            r3_rx_cold_error_t error=recheck_defaults(&g->bus,&r->verification);
            ok=error==R3_COLD_OK;if(error)(void)fail(r,error);break;
        }
        case R3_COLD_RADIO_INIT:
            r->radio_error=r3_radio_init_run(&g->bus,&bounded,&calibration,b->xtal_hz,b->tia8,b->tia16,&r->detail.radio);
            ok=r->radio_error==R3_VCO_OK;break;
        case R3_COLD_DEAF:
            r->agc_error=r3_rx_deaf_set(g,1u,&r->detail.agc);ok=r->agc_error==R3_AGC_OK;break;
        case R3_COLD_CHANNEL:
            r->radio_error=r3_radio_channel_run(&g->bus,&bounded,&calibration,&b->channel,&r->detail.channel);
            ok=r->radio_error==R3_VCO_OK;break;
        case R3_COLD_RX_PATH:
            r->path_error=r3_rx_path_configure(g,b->channel.channel,&r->detail.path);
            ok=r->path_error==R3_RX_PATH_OK;break;
        case R3_COLD_DC:
            r->radio_error=r3_dccal_run(&g->bus,&bounded,&dc,&r->detail.dc);ok=r->radio_error==R3_VCO_OK;break;
        case R3_COLD_AGC:
            r->agc_error=r3_rx_agc_configure(g,&b->agc,&r->detail.agc);ok=r->agc_error==R3_AGC_OK;break;
        case R3_COLD_UNDEAF:
            r->agc_error=r3_rx_deaf_set(g,0u,&r->detail.agc);ok=r->agc_error==R3_AGC_OK;break;
        case R3_COLD_FINALIZE:
            r->agc_error=r3_rx_finalize(g,&r->detail.agc);ok=r->agc_error==R3_AGC_OK;break;
        case R3_COLD_GUARD_CHECK:ok=1;break;
        default:break;
        }
        if(!ok){(void)fail(r,g->phase==R3_TG_FAULT?R3_COLD_GUARD:R3_COLD_BLOCK);break;}
        if(!guard_check(g,r)||!gate(&context))break;
        r->completed_mask|=1u<<r->phase;
    }
    g->raw=context.raw;
    if(!r->error&&!r->elapsed) {
        r->phase=R3_COLD_GUARD_CHECK;(void)fail(r,R3_COLD_CLOCK_STALLED);
    }
    if(r->error) {
        if(g->phase!=R3_TG_FAULT){g->phase=R3_TG_FAULT;g->error=R3_TG_IO;}
        r->guard_error=g->error;
    } else r->reset_required=0u;
    return r->error;
}
r3_rx_cold_error_t r3_rx_cold_start(r3_tx_guard_t *g,const r3_rx_board_t *b,
                                   const r3_vco_time_t *time,const r3_rx_cold_limits_t *limits,
                                   r3_rx_cold_report_t *r) {
    return cold_start(g,b,time,limits,r,0u);
}
r3_rx_cold_error_t r3_rx_cold_start_table20_alias64(r3_tx_guard_t *g,const r3_rx_board_t *b,
                                   const r3_vco_time_t *time,const r3_rx_cold_limits_t *limits,
                                   r3_rx_cold_report_t *r) {
    return cold_start(g,b,time,limits,r,1u);
}

#ifndef R3_RADIO_CALIBRATION_H
#define R3_RADIO_CALIBRATION_H
#include "radio_startup.h"

typedef enum { R3_RCCAL_VALIDATE,R3_RCCAL_LPF,R3_RCCAL_ADC,R3_RCCAL_RELEASE,
               R3_RCCAL_COMPLETE } r3_rccal_phase_t;
typedef struct {
    r3_vco_report_t transport;
    r3_rccal_phase_t phase;
    uint32_t samples[2],n0[2],n1[2],completed;
    uint16_t lpf,lpf_rc,adc;
    uint8_t dacbuf;
} r3_rccal_report_t;
typedef enum {
    R3_DCCAL_VALIDATE,R3_DCCAL_FORCE,R3_DCCAL_POLL,R3_DCCAL_RELEASE,R3_DCCAL_COMPLETE
} r3_dccal_phase_t;
typedef struct {
    r3_vco_report_t transport;
    r3_dccal_phase_t phase;
    uint32_t pulses,idac_status,dcoe_status,predicate_observed;
} r3_dccal_report_t;
typedef enum {
    R3_RADIO_INIT_VALIDATE,R3_RADIO_INIT_POWER,R3_RADIO_INIT_RC,R3_RADIO_INIT_ADC,
    R3_RADIO_INIT_DAC,R3_RADIO_INIT_TIA,R3_RADIO_INIT_PREFERRED,R3_RADIO_INIT_CONFIGURED
} r3_radio_init_phase_t;
typedef struct {
    r3_vco_report_t envelope,adc_io,tia_io;
    r3_rccal_report_t rc;
    r3_radio_init_phase_t phase;
    uint16_t adc[11];
} r3_radio_init_report_t;
typedef struct {
    uint32_t xtal_hz,channel,frequency_mhz;
    const uint16_t *words;
    uint32_t capacity;
} r3_radio_channel_t;
typedef struct {
    r3_vco_report_t programming,vco;
    uint32_t words_completed,band_operations,configured;
} r3_radio_channel_report_t;

/* Nominal delays require the same qualified, bounded callbacks as VCO.
   Outputs are published only after both calibrations and final release succeed.
   These routines are not a complete PHY initializer or a TX interlock. */
r3_vco_error_t r3_rccal_run(const r3_phy_bus_t *,const r3_vco_time_t *,
                           const r3_vco_limits_t *,uint32_t,r3_rccal_report_t *);
r3_vco_error_t r3_adc_coefficients(uint32_t,uint16_t [11]);
r3_vco_error_t r3_adc_apply(const r3_phy_bus_t *,const r3_vco_time_t *,
                           uint32_t,const uint16_t [11],r3_vco_report_t *);
r3_vco_error_t r3_tia_apply(const r3_phy_bus_t *,const r3_vco_time_t *,uint32_t,
                           const uint8_t [52],const uint16_t [56],r3_vco_report_t *);
/* Fixed mode=3, gain=0 DC force protocol only; never calls idle-TSSI/dummy TX.
   At most ten pulses with target delay argument 32. All-ones status is rejected.
   Success means the two completion fields were observed and release completed;
   it does not prove status freshness, analog calibration accuracy or RX readiness. */
r3_vco_error_t r3_dccal_run(const r3_phy_bus_t *,const r3_vco_time_t *,
                           const r3_vco_limits_t *,r3_dccal_report_t *);
/* Exact radio 0x871668 envelope, including TX-labelled supply/DAC/bias settings.
   NOT RX-only approval. Requires qualified state/timing/inhibition. The one
   overall deadline also constrains nested RC/ADC/TIA calls. TIA data must remain
   valid and immutable. Success means configuration completed, not channel lock. */
r3_vco_error_t r3_radio_init_run(const r3_phy_bus_t *,const r3_vco_time_t *,
                                const r3_vco_limits_t *,uint32_t,const uint8_t [52],
                                const uint16_t [56],r3_radio_init_report_t *);
/* Exact channel body 0x8714e4: 31 writes, three band updates, VCO protocol.
   Caller binds the immutable words to the declared oscillator/channel and
   supplies qualified power/access/timing/inhibition. The same overall deadline
   covers programming and VCO; configured is not proof of lock or reception. */
r3_vco_error_t r3_radio_channel_run(const r3_phy_bus_t *,const r3_vco_time_t *,
                                   const r3_vco_limits_t *,const r3_radio_channel_t *,
                                   r3_radio_channel_report_t *);
#endif

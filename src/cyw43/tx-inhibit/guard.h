#ifndef R3_TX_GUARD_H
#define R3_TX_GUARD_H
#include "../phy-access/access.h"

typedef enum { R3_TG_NEW,R3_TG_BOUND,R3_TG_ARMED,R3_TG_FAULT } r3_tx_guard_phase_t;
typedef enum { R3_TG_PA_UNBOUND,R3_TG_PA_ARMING,R3_TG_PA_ARMED } r3_tx_guard_pa_phase_t;
typedef enum { R3_TG_OK,R3_TG_ARGUMENT,R3_TG_STATE,R3_TG_IO,R3_TG_REGISTER,
               R3_TG_SELECTOR,R3_TG_DEFEATING_WRITE,R3_TG_INVARIANT } r3_tx_guard_error_t;
typedef struct {
    r3_phy_bus_t raw,bus;
    r3_tx_guard_phase_t phase;
    r3_tx_guard_error_t error;
    uint32_t phy_selector,radio_selector,have_phy,have_radio;
    uint32_t last_address,last_value,completed_io,verified_registers;
    r3_tx_guard_pa_phase_t pa_phase;
} r3_tx_guard_t;

/* Source-writer policy for candidate TX-chain/iPA overrides, NOT an RF interlock.
   Initialize a zeroed object only after independent cold recovery. All participating
   PHY/radio writers must use bus, never raw. Callback ownership, clocks, exclusivity
   and bounded execution remain caller requirements. Fault is sticky until recovery. */
r3_tx_guard_error_t r3_tx_guard_init(r3_tx_guard_t *,const r3_phy_bus_t *);
r3_tx_guard_error_t r3_tx_guard_arm(r3_tx_guard_t *);
/* Explicit candidate extension: radio[0x79] bit0 clear, radio[0xa4] bit2 set.
   Does not disable the TX-labelled LDO or claim electrical qualification. */
r3_tx_guard_error_t r3_tx_guard_arm_radio_pa(r3_tx_guard_t *);
r3_tx_guard_error_t r3_tx_guard_check(r3_tx_guard_t *);
const char *r3_tx_guard_error_name(r3_tx_guard_error_t);
#endif

#ifndef R3_PHY_ACCESS_H
#define R3_PHY_ACCESS_H
#include <stdint.h>
#define R3_PHY_REGISTER_BASE 0x18001000u
#define R3_PHY_MAX_WORDS 384u
#define R3_PHY_MAX_OPERATIONS 128u
typedef enum {
    R3_PHY_MOD,R3_PHY_AND,R3_PHY_WRITE,R3_PHY_OR,
    R3_RADIO_MOD,R3_RADIO_AND,R3_RADIO_WRITE,R3_RADIO_OR,R3_PHY_READ,R3_RADIO_READ
} r3_phy_kind_t;
typedef enum {
    R3_PHY_OK,R3_PHY_ARGUMENT,R3_PHY_TRUNCATED,R3_PHY_CAPACITY,
    R3_PHY_UNSUPPORTED,R3_PHY_READ_ERROR,R3_PHY_WRITE_ERROR,R3_PHY_VALUE_ERROR
} r3_phy_error_t;
typedef struct { r3_phy_kind_t kind;uint32_t index,mask,value; } r3_phy_op_t;
typedef int (*r3_phy_read_t)(void *,uint32_t,uint32_t,uint32_t *);
typedef int (*r3_phy_write_t)(void *,uint32_t,uint32_t,uint32_t);
typedef struct { void *context;r3_phy_read_t read;r3_phy_write_t write;uint32_t base; } r3_phy_bus_t;
typedef struct { r3_phy_error_t error;uint32_t io_completed,operations,address,value; } r3_phy_report_t;
/* Input/output extents must be valid and non-overlapping. */
r3_phy_error_t r3_phy_decode(const uint16_t *,uint32_t,r3_phy_op_t *,uint32_t,uint32_t *);
/* Callbacks must be bounded and serialized; caller must qualify clocks, ownership and RF safety.
   Table memory must remain immutable for the complete execution call. */
r3_phy_error_t r3_phy_execute(const r3_phy_bus_t *,const r3_phy_op_t *,r3_phy_report_t *);
r3_phy_error_t r3_phy_execute_table(const r3_phy_bus_t *,const uint16_t *,uint32_t,r3_phy_report_t *);
r3_phy_error_t r3_phy_pulse_044a(const r3_phy_bus_t *,r3_phy_report_t *);
/* Canonical enabled=0/1 only; this paired override is not a TX-inhibit API. */
r3_phy_error_t r3_phy_override_4b0_43a(const r3_phy_bus_t *,uint32_t,r3_phy_report_t *);
/* Explicit radio API; the PHY/table APIs still reject radio operations.
   Index 1 is rejected rather than using the vendor's fabricated read value. */
r3_phy_error_t r3_radio_execute(const r3_phy_bus_t *,const r3_phy_op_t *,r3_phy_report_t *);
#endif

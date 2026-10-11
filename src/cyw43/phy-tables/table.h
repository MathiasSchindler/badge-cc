#ifndef R3_PHY_TABLE_H
#define R3_PHY_TABLE_H
#include "../phy-access/access.h"

#define R3_PHY_TABLE_MAX_ENTRIES 256u
typedef enum {
    R3_TABLE_OK,R3_TABLE_ARGUMENT,R3_TABLE_RANGE,R3_TABLE_WIDTH,R3_TABLE_VALUE,R3_TABLE_IO
} r3_table_error_t;
typedef struct { uint32_t id,offset,count,width; } r3_table_request_t;
typedef struct {
    r3_table_error_t error;
    r3_phy_error_t phy_error;
    uint32_t entries_completed,io_completed,last_address,last_value;
    uint32_t saved_9a5,saved_574,saved_575,saved_mask;
    uint32_t restore_writes_completed,reset_required;
} r3_table_report_t;

/* All buffers/objects must be valid, non-overlapping, and stable for the call.
   Values use one uint32_t per entry, including widths 8 and 16.
   The caller must qualify the table, clocks, exclusive access and RF safety.
   Any I/O failure stops without guessed rollback; independent recovery is required. */
r3_table_error_t r3_phy_table_read(const r3_phy_bus_t *,const r3_table_request_t *,
                                  uint32_t *,uint32_t,r3_table_report_t *);
r3_table_error_t r3_phy_table_write(const r3_phy_bus_t *,const r3_table_request_t *,
                                   const uint32_t *,uint32_t,r3_table_report_t *);
#endif

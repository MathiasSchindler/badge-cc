#ifndef CLEAN_VENDOR_BURST_APP_H
#define CLEAN_VENDOR_BURST_APP_H
#include "protocol.h"
#include "../../src/usb/live_usb_hw.h"
#include "../../src/platform/badger/wifi_board.h"
extern live_usb_control_t ref_usb;
extern uint8_t ref_detail[REF_TRACE_BYTES];
extern uint8_t ref_ssid[33], ref_password[64];
extern uint32_t ref_generation;
extern uint32_t ref_clock_ticks, ref_clock_frames;
extern const char ref_role[];
uint32_t ref_now(void);
int ref_service(void *context);
void ref_run(const uint8_t nonce[16]);
void ref_idle(void);
void ref_stop(void);
static inline void ref_wipe(uint8_t *bytes, uint32_t size) {
    for (uint32_t i = 0u; i < size; ++i) ((volatile uint8_t *)bytes)[i] = 0u;
}
#endif

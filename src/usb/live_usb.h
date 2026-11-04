#ifndef UNIVERSE26_LIVE_USB_H
#define UNIVERSE26_LIVE_USB_H

#include "../shared/usb_report.h"
#include "../radio/wifi_trace.h"

/* Experimental lab identifiers, not a production USB VID/PID allocation. */
#define LIVE_USB_VID 0xcafeu
#ifdef LIVE_USB_LOGI_HOST
#define LIVE_USB_PID 0x4027u
#define LIVE_USB_LOGI_STATUS 0x72u
#define LIVE_USB_LOGI_PROBE 0x73u
#define LIVE_USB_LOGI_FS_STATUS 0x74u
#define LIVE_USB_LOGI_FS_TRACE 0x75u
#define LIVE_USB_LOGI_KEYPAD 0x76u
#define LIVE_USB_LOGI_KEYPAD_STATUS 0x77u
#define LIVE_USB_LOGI_KEYPAD_EVENT 0x78u
#define LIVE_USB_LOGI_LCD_TEST 0x79u
#define LIVE_USB_LOGI_LCD_TOGGLE 0x7au
#define LIVE_USB_LOGI_LCD_ANIMATE 0x7bu
#define LIVE_USB_LOGI_KEYPAD_SLOTS 32u
#else
#define LIVE_USB_PID 0x4026u
#endif
#define LIVE_USB_SUBMIT 0x50u
#define LIVE_USB_REPORT 0x51u
#define LIVE_USB_BOOTSEL 0x52u
#define LIVE_USB_INFO 0x53u
#define LIVE_USB_WIFI_SUBMIT 0x54u
#define LIVE_USB_WIFI_REPORT 0x55u
#define LIVE_USB_WIFI_TRACE 0x56u
#define LIVE_USB_WIFI_PIO_SUBMIT 0x57u
#define LIVE_USB_FW_SUBMIT 0x58u
#define LIVE_USB_FW_REPORT 0x59u
#define LIVE_USB_LINK_AP 0x5au
#define LIVE_USB_LINK_OPEN_AP 0x65u
#define LIVE_USB_LINK_OPEN_STATION 0x66u
#define LIVE_USB_CLEAN_SECRET 0x67u
#define LIVE_USB_LINK_WPA2_FIXTURE 0x68u
#define LIVE_USB_LINK_STATION 0x5bu
#define LIVE_USB_LINK_REPORT 0x5cu
#define LIVE_USB_UDP_START 0x5du
#define LIVE_USB_UDP_REPORT 0x5eu
#define LIVE_USB_UDP_PAGE 0x5fu
#define LIVE_USB_UDP_FREEZE 0x60u
#define LIVE_USB_PACKET 64u
#ifdef LIVE_USB_KEYPAD_TRACE
#include "../../logi-keypad/firmware/trace.h"
#endif
#ifdef LIVE_USB_NAMETAG_M2
#include "../shared/nametag_measure.h"
#define LIVE_USB_M2_SUBMIT 0x63u
#define LIVE_USB_M2_PAGE 0x64u
#endif
#ifdef LIVE_USB_COMPILER
#include "../tools/compiler_test/suite.h"
#define LIVE_USB_COMPILER_SUBMIT 0x61u
#define LIVE_USB_COMPILER_PAGE 0x62u
#endif

typedef enum {
    USB_ACTION_NONE,
    USB_ACTION_IN,
    USB_ACTION_OUT,
    USB_ACTION_ADDRESS,
    USB_ACTION_STALL
} live_usb_action_t;

typedef enum {
    USB_PHASE_IDLE,
    USB_PHASE_DATA_IN,
    USB_PHASE_DATA_OUT,
    USB_PHASE_STATUS_IN,
    USB_PHASE_STATUS_OUT,
    USB_PHASE_STALL
} live_usb_phase_t;

typedef struct {
    live_usb_phase_t phase;
    live_usb_action_t action;
    uint32_t build_id;
    uint32_t requests;
    uint32_t rejected;
    uint8_t configured;
    uint8_t address;
    uint8_t pending_address;
    uint8_t address_pending;
    uint8_t pending_configuration;
    uint8_t configuration_pending;
    uint8_t reboot_pending;
    uint8_t reboot_requested;
    uint8_t report_valid;
    uint8_t report_test;
    uint8_t incoming_test;
    uint8_t probe_pio;
    uint8_t probe_role;
    uint8_t probe_pending;
    uint8_t probe_ready;
    uint8_t probe_inflight;
    uint32_t probe_generation;
    uint8_t probe_nonce[16];
    uint8_t trace_valid;
    uint8_t trace[WIFI_TRACE_SIZE];
    uint16_t length;
    uint8_t data[LIVE_USB_PACKET];
    uint8_t report[USB_REPORT_WIRE_SIZE];
    char serial[17];
    uint8_t udp_out, udp_pending, udp_ready, udp_running;
    uint8_t udp_valid, udp_complete, udp_frozen;
    uint32_t udp_generation;
    uint8_t udp_report[64];
    uint8_t udp_detail[512];
#ifdef LIVE_USB_LOGI_HOST
    uint8_t logi_status[64], logi_status_valid;
    uint8_t logi_fs_status[64], logi_fs_valid, logi_fs_pending, logi_fs_ready;
    uint8_t logi_fs_trace[64];
    uint8_t logi_keypad_status[64], logi_keypad_events[LIVE_USB_LOGI_KEYPAD_SLOTS][64];
    uint8_t logi_keypad_pending, logi_keypad_seconds, logi_keypad_active, logi_keypad_valid;
    uint8_t logi_lcd_pending, logi_lcd_requested;
#endif
#ifdef LIVE_USB_KEYPAD_TRACE
    uint8_t keypad_out, keypad_pending, keypad_ready, keypad_status_valid;
    uint8_t keypad_record[64], keypad_status[64];
#endif
#ifdef LIVE_USB_CLEAN
    uint8_t clean_secret[64], clean_secret_ready, clean_secret_out;
#endif
#ifdef LIVE_USB_NAMETAG_M2
    uint8_t m2_out, m2_pending, m2_ready, m2_valid;
    uint32_t m2_generation;
    uint8_t m2_nonce[16];
    uint32_t m2_words[NAMETAG_M2_WORDS];
#endif
#ifdef LIVE_USB_COMPILER
    uint8_t compiler_out, compiler_pending, compiler_ready, compiler_valid;
    uint32_t compiler_generation;
    uint8_t compiler_nonce[16];
    uint8_t compiler_records[COMPILER_SUITE_BYTES];
#endif
} live_usb_control_t;

void live_usb_control_init(live_usb_control_t *control, uint32_t build_id,
                            const char serial[17]);
void live_usb_control_reset(live_usb_control_t *control);
/* Do not idle-throttle enumeration or an in-flight EP0 transfer. */
int live_usb_control_poll_immediate(const live_usb_control_t *control);
void live_usb_control_setup(live_usb_control_t *control, const uint8_t setup[8]);
void live_usb_control_complete(live_usb_control_t *control, uint8_t in,
                                const uint8_t *data, uint16_t length);
uint32_t live_usb_probe_take(live_usb_control_t *control, uint8_t nonce[16]);
void live_usb_probe_finish(live_usb_control_t *control, uint32_t generation,
                           const uint8_t report[64]);
void live_usb_trace_finish(live_usb_control_t *control, uint32_t generation,
                           const uint8_t trace[WIFI_TRACE_SIZE]);
void live_usb_link_publish(live_usb_control_t *control, uint32_t generation,
                            const uint8_t report[64]);
void live_usb_link_details(live_usb_control_t *control, uint32_t generation,
                            const uint8_t detail[WIFI_TRACE_SIZE]);
uint32_t live_usb_udp_take(live_usb_control_t *control);
void live_usb_udp_publish(live_usb_control_t *control, uint32_t link_generation,
                          uint32_t udp_generation, const uint8_t report[64],
                          const uint8_t detail[512], uint8_t complete);

#endif

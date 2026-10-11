#ifndef UNIVERSE26_WIFI_TRACE_H
#define UNIVERSE26_WIFI_TRACE_H

#include "../shared/usb_report.h"

#define WIFI_TRACE_SIZE 256u
#define WIFI_TRACE_CHUNK 24u
#define WIFI_TRACE_PAGES 11u
#define WIFI_TRACE_WORDS 64u
#define WIFI_TRACE_MAGIC 0x31525457u

typedef enum {
    WIFI_TRACE_IDLE,
    WIFI_TRACE_SELECTED,
    WIFI_TRACE_DRIVING,
    WIFI_TRACE_RELEASED,
    WIFI_TRACE_FINISHED
} wifi_trace_event_t;

void wifi_trace_page(uint8_t out[64], const uint8_t trace[WIFI_TRACE_SIZE],
                      const uint8_t report[64], uint8_t page);
usb_record_result_t wifi_trace_page_decode(uint8_t trace[WIFI_TRACE_SIZE],
                                            const uint8_t *page, uint32_t length,
                                            const uint8_t report[64], uint8_t expected_page);

#endif

#ifndef UNIVERSE26_WIFI_USB_REPORT_H
#define UNIVERSE26_WIFI_USB_REPORT_H

#include "wifi_probe.h"
#include "../shared/usb_report.h"

#define WIFI_USB_TEST_ID 2u

void wifi_usb_report_encode(uint8_t wire[64], uint32_t build_id,
                             const uint8_t nonce[16], const wifi_probe_report_t *report);
usb_record_result_t wifi_usb_report_decode(const uint8_t *wire, uint32_t length,
                                            const uint8_t request[64], uint32_t build_id,
                                            wifi_probe_report_t *report);

#endif

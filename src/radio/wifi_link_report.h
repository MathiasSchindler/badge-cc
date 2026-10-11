#ifndef UNIVERSE26_WIFI_LINK_REPORT_H
#define UNIVERSE26_WIFI_LINK_REPORT_H

#include "wifi_link.h"
#include "../shared/usb_report.h"

typedef struct {
    wifi_link_report_t committed;
    uint8_t in_progress;
} wifi_link_snapshot_t;

void wifi_link_snapshot_begin(wifi_link_snapshot_t *snapshot, const wifi_link_report_t *report);
void wifi_link_snapshot_end(wifi_link_snapshot_t *snapshot);
const wifi_link_report_t *wifi_link_snapshot_view(const wifi_link_snapshot_t *snapshot,
                                                  const wifi_link_report_t *current);

void wifi_link_report_encode(uint8_t bytes[64], uint32_t build,
                              const uint8_t nonce[16], const wifi_link_report_t *report);
usb_record_result_t wifi_link_report_decode(const uint8_t bytes[64], uint32_t build,
                                            const uint8_t nonce[16], uint8_t role,
                                            wifi_link_report_t *report);

#endif

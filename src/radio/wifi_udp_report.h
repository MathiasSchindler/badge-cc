#ifndef UNIVERSE26_WIFI_UDP_REPORT_H
#define UNIVERSE26_WIFI_UDP_REPORT_H

#include "wifi_udp.h"
#include "../shared/usb_report.h"

#define WIFI_UDP_DETAIL_SIZE 512u
#define WIFI_UDP_DETAIL_PAGES 22u
#define WIFI_UDP_SAMPLE_OFFSET 112u

typedef struct {
    uint32_t calibration_ticks;
    uint32_t calibration_frames;
    uint32_t elapsed_ticks;
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t radio_drops;
} wifi_udp_metrics_t;

typedef struct {
    uint8_t role, stage, error, arp_ready;
    uint32_t sent, received, lost, echoed, dropped, last_rtt;
    uint32_t calibration_ticks, elapsed_ticks;
} wifi_udp_summary_t;

void wifi_udp_report_encode(uint8_t report[64], uint32_t build, const uint8_t nonce[16],
                            const wifi_udp_t *net, const wifi_udp_metrics_t *metrics);
usb_record_result_t wifi_udp_report_decode(const uint8_t report[64], uint32_t build,
                                           const uint8_t nonce[16], uint8_t role,
                                           wifi_udp_summary_t *summary);
void wifi_udp_detail_encode(uint8_t detail[WIFI_UDP_DETAIL_SIZE],
                            const wifi_udp_t *net, const wifi_udp_metrics_t *metrics);
usb_record_result_t wifi_udp_detail_check(const uint8_t detail[WIFI_UDP_DETAIL_SIZE],
                                         const wifi_udp_summary_t *summary);
void wifi_udp_page(uint8_t out[64], const uint8_t detail[WIFI_UDP_DETAIL_SIZE],
                    const uint8_t report[64], uint8_t page);
usb_record_result_t wifi_udp_page_decode(uint8_t detail[WIFI_UDP_DETAIL_SIZE],
                                         const uint8_t page[64], const uint8_t report[64],
                                         uint8_t expected_page);

#endif

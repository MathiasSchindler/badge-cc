#ifndef UNIVERSE26_WIFI_FIRMWARE_H
#define UNIVERSE26_WIFI_FIRMWARE_H

#include "wifi_probe.h"
#include "wifi_trace.h"

#define WIFI_FW_TEST_ID 3u
#define WIFI_FW_PACKET_MAX 2040u
#define WIFI_FW_CHUNK 512u
#define WIFI_FW_BACKPLANE_BLOCK 64u

typedef enum {
    WIFI_FW_IDLE, WIFI_FW_PROBE, WIFI_FW_CORES, WIFI_FW_DOWNLOAD,
    WIFI_FW_VERIFY, WIFI_FW_NVRAM, WIFI_FW_START, WIFI_FW_HT,
    WIFI_FW_F2, WIFI_FW_VERSION, WIFI_FW_CLM, WIFI_FW_DONE
} wifi_fw_stage_t;

typedef enum {
    WIFI_FW_OK, WIFI_FW_BUS, WIFI_FW_CHIP, WIFI_FW_CORE_STATE,
    WIFI_FW_ASSET, WIFI_FW_READBACK, WIFI_FW_TIMEOUT, WIFI_FW_PROTOCOL,
    WIFI_FW_CONTROLLER_ERROR, WIFI_FW_CANCELLED
} wifi_fw_error_t;

typedef struct {
    const uint8_t *firmware;
    uint32_t firmware_size;
    uint32_t firmware_crc;
    const uint8_t *nvram;
    uint32_t nvram_size;
    uint32_t nvram_crc;
    const uint8_t *clm;
    uint32_t clm_size;
    uint32_t clm_crc;
} wifi_fw_assets_t;

typedef struct {
    wifi_fw_stage_t stage;
    wifi_fw_error_t error;
    uint32_t address;
    uint32_t expected;
    uint32_t actual;
    uint32_t verified_bytes;
    uint32_t elapsed_ticks;
    uint32_t controller_status;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t service_ticks;
    uint32_t transfer_ticks;
    uint8_t last_rx_prefix[28];
    char version[192];
} wifi_fw_report_t;

typedef int (*wifi_fw_service_t)(void *context, const wifi_fw_report_t *report);
typedef void (*wifi_fw_raw_receive_t)(void *context, const uint8_t *packet, uint32_t length);

void wifi_firmware_run(const wifi_wire_t *wire, const wifi_fw_assets_t *assets,
                        wifi_fw_report_t *report, wifi_fw_service_t service, void *context);
/* Single retained session; wire/report/service context must live until stop. */
int wifi_firmware_begin(const wifi_wire_t *wire, const wifi_fw_assets_t *assets,
                         wifi_fw_report_t *report, wifi_fw_service_t service, void *context);
int wifi_firmware_control(uint32_t command, uint8_t set, uint8_t interface,
                           const uint8_t *request, uint32_t request_size,
                           uint8_t *reply, uint32_t capacity, uint32_t *reply_size);
void wifi_firmware_stop(void);
/* Bounded access through the retained session's shared backplane window.
   Memory transfers require aligned addresses and lengths, up to 4096 bytes.
   These do not enable WLAN or transmit Ethernet frames. */
int wifi_firmware_backplane_read32(uint32_t address, uint32_t *value);
int wifi_firmware_backplane_write32(uint32_t address, uint32_t value);
int wifi_firmware_backplane_read(uint32_t address, uint8_t *data, uint32_t length);
int wifi_firmware_backplane_write(uint32_t address, const uint8_t *data, uint32_t length);
int wifi_firmware_data_enable(void);
/* Opt-in diagnostic alternative to Ethernet RX. Callback storage is borrowed.
 * Every non-control SDPCM packet is delivered, including during CDC commands. */
int wifi_firmware_raw_enable(wifi_fw_raw_receive_t receive, void *context);
/* 1 packet processed, 0 none, -1 radio/protocol error; never transmits data. */
int wifi_firmware_raw_poll(void);
int wifi_firmware_send_frame(uint8_t interface, const uint8_t *frame, uint32_t length);
/* Explicit bounded oversized-TX diagnostic; vendor acceptance requires OTA verification. */
int wifi_firmware_send_frame_diagnostic(uint8_t interface, const uint8_t *frame, uint32_t length);
/* 1 frame copied, 0 no frame, -1 radio/protocol error. */
int wifi_firmware_read_frame(uint8_t frame[1514], uint32_t *length, uint8_t *interface);
uint32_t wifi_firmware_rx_dropped(void);
void wifi_firmware_report(uint8_t wire[64], uint32_t build_id,
                           const uint8_t nonce[16], const wifi_fw_report_t *report);
void wifi_firmware_detail(uint8_t bytes[WIFI_TRACE_SIZE], const wifi_fw_assets_t *assets,
                           const wifi_fw_report_t *report);
usb_record_result_t wifi_firmware_decode(const uint8_t *wire, uint32_t length,
                                         const uint8_t request[64], uint32_t build_id,
                                         wifi_fw_report_t *report);
const char *wifi_fw_stage_name(wifi_fw_stage_t stage);
const char *wifi_fw_error_name(wifi_fw_error_t error);

#endif

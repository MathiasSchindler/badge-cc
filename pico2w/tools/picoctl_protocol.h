#ifndef PICOCTL_PROTOCOL_H
#define PICOCTL_PROTOCOL_H

#include "../src/usb/report.h"

typedef struct {
    uint32_t wifi_stage, dhcp_state, network_error, ip;
    int32_t wifi_error, lora_error;
    uint32_t received, dropped, transmitted, uptime_ms, connected, controller_status;
} picoctl_status_t;

#define PICOCTL_PACKET_SIZE 340u
#define PICOCTL_PACKET_CRC 336u

typedef struct {
    uint32_t present, newest, oldest, sequence, timestamp_ms, frequency_hz;
    uint32_t bandwidth_khz, sf, profile, packets, crc_errors, header_errors, length, flags;
    int32_t rssi_qdb, snr_qdb, signal_rssi_qdb;
    int32_t noise_qdb, noise_min_qdb, noise_max_qdb; /* GetRssiInst, quarter dBm */
    uint32_t noise_samples;                         /* 0: no noise sample yet */
    uint8_t payload[255];
} picoctl_packet_t;

int picoctl_hex(const char *text, uint8_t *output, uint32_t bytes);
int picoctl_serial(const char *text, char output[17]);
int picoctl_identity(const uint8_t record[64], uint32_t build,
                     const uint8_t nonce[16], const char *serial);
int picoctl_status(const uint8_t record[64], uint32_t build, picoctl_status_t *status);
int picoctl_wifi_diagnostic(const uint8_t record[64], uint32_t build);
/* USB request 0x59 record (P2WP); validates magic, size, build and CRC. */
int picoctl_packet(const uint8_t record[PICOCTL_PACKET_SIZE], uint32_t build,
                   picoctl_packet_t *packet);
#define PICOCTL_SURVEY_SIZE 32u

typedef struct {
    uint32_t state; /* 1 pending, 2 done, 3 failed */
    uint32_t khz, bandwidth_khz, samples;
    int32_t error, min_qdb, mean_qdb, max_qdb;
} picoctl_survey_t;

/* USB request 0x5a record (P2WV) for the requested frequency. */
int picoctl_survey(const uint8_t record[PICOCTL_SURVEY_SIZE], uint32_t build, uint32_t khz,
                   picoctl_survey_t *survey);
int picoctl_network_ready(const picoctl_status_t *status);
int picoctl_uf2_block(const uint8_t block[512], uint32_t index, uint32_t count);

#endif

#ifndef WIFI_HANDOVER_EXPERIMENT_H
#define WIFI_HANDOVER_EXPERIMENT_H

#include <stdbool.h>
#include <stdint.h>

#define EXP_MAX_PLAYERS 4u
#define EXP_MAX_HOSTS 4u
#define EXP_SSID_SIZE 24u
#define EXP_PASSWORD_SIZE 33u
#define EXP_BENCH_SAMPLES 1000u
#define EXP_POSITION_MAX 1000u

typedef struct {
    uint32_t session_id;
    uint64_t token;
    uint8_t slot;
    uint8_t channel;
    char ssid[EXP_SSID_SIZE];
    char password[EXP_PASSWORD_SIZE];
} handover_credentials_t;

typedef struct {
    uint32_t sent;
    uint32_t received;
    uint32_t lost;
    uint32_t local_errors;
    uint32_t min_us;
    uint32_t median_us;
    uint32_t p95_us;
    uint32_t p99_us;
    uint32_t max_us;
    uint32_t latest_us;
    uint8_t target;
    bool running;
} experiment_metrics_t;

typedef enum {
    WIFI_IDLE, WIFI_STARTING_AP, WIFI_JOINING, WIFI_HANDSHAKE,
    WIFI_READY, WIFI_ERROR
} experiment_wifi_phase_t;

typedef struct {
    experiment_wifi_phase_t phase;
    bool host;
    bool power_save;
    uint8_t slot;
    uint8_t online_mask;
    uint16_t positions[EXP_MAX_PLAYERS];
    uint32_t peer_age_ms[EXP_MAX_PLAYERS];
    uint32_t join_ms;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t rejected_packets;
    uint32_t send_errors;
    int error_code;
    char status[40];
    experiment_metrics_t metrics;
} experiment_wifi_view_t;

#endif

#ifndef PICO2W_LORA_RECEIVER_H
#define PICO2W_LORA_RECEIVER_H

#include "types.h"
#include "sx1262.h"

/*
 * Receive-only, continuously listening SX1262 LoRa receiver for the Waveshare
 * Pico-LoRa-SX1262-868M. It never issues SetTx, SetTxContinuousWave,
 * SetTxInfinitePreamble, SetCad or any LoRaWAN join/transmit operation.
 *
 * Scope: LoRaWAN *uplink* traffic (LoRaWAN profile) or Meshtastic frames
 * (Meshtastic profile). Standard (non-inverted) IQ is fixed,
 * so LoRaWAN downlinks (inverted IQ) are normally not received. Explicit header mode:
 * the radio checks payload CRC only when the received header announces one.
 * "packets" therefore means RxDone without CrcErr, not "CRC verified"; the
 * SX1262 does not report whether a CRC was present. Header fields are a
 * structural reading only: no MIC check, no decryption.
 *
 * The object is caller-allocated and is not re-entrant: the service callback
 * must never call back into the receiver. Every SPI wait calls the service;
 * a nonzero service result cancels the current operation.
 */

#define SX1262_RX_RING_SIZE 16u
#define SX1262_RX_MAX_PAYLOAD 255u

#define SX1262_RX_CONFIG_ERROR (-3010)
#define SX1262_RX_STATE_ERROR (-3011)
#define SX1262_RX_BOARD_ERROR (-3012) /* board not confirmed: no hardware touched */
#define SX1262_RX_BUSY_ERROR (-3013) /* activity survey owns the radio */

/* Only supported board: TCXO on DIO3 at 1.7 V/5 ms, PE4259 RF switch on DIO2. */
#define SX1262_RX_BOARD_WAVESHARE_PICO_LORA_SX1262_868M 0x57534831u

#define SX1262_RX_MIN_FREQUENCY_HZ 863000000u
#define SX1262_RX_MAX_FREQUENCY_HZ 870000000u

/* Sync word and preamble family. Both are receive-only. */
#define SX1262_RX_PROFILE_LORAWAN 0u    /* public sync word 0x34, preamble 8 */
#define SX1262_RX_PROFILE_MESHTASTIC 1u /* sync word 0x2b, preamble 16 */

/* LoRaWAN EU868 channel 0. */
#define SX1262_RX_LORAWAN_FREQUENCY_HZ 868100000u
#define SX1262_RX_LORAWAN_SF 7u
#define SX1262_RX_LORAWAN_BANDWIDTH_KHZ 125u
/* Meshtastic EU_868 LongFast: 869.4 MHz + 250 kHz / 2, one slot. */
#define SX1262_RX_MESHTASTIC_FREQUENCY_HZ 869525000u
#define SX1262_RX_MESHTASTIC_SF 11u
#define SX1262_RX_MESHTASTIC_BANDWIDTH_KHZ 250u

#define SX1262_RX_DEFAULT_PROFILE SX1262_RX_PROFILE_MESHTASTIC
#define SX1262_RX_DEFAULT_FREQUENCY_HZ SX1262_RX_MESHTASTIC_FREQUENCY_HZ
#define SX1262_RX_DEFAULT_SF SX1262_RX_MESHTASTIC_SF
#define SX1262_RX_DEFAULT_BANDWIDTH_KHZ SX1262_RX_MESHTASTIC_BANDWIDTH_KHZ

/* GetRssiInst noise-floor sampling interval while idle in RX. */
#define SX1262_RX_NOISE_INTERVAL_MS 250u

#define SX1262_RX_IDLE 0u
#define SX1262_RX_RUNNING 1u
#define SX1262_RX_FAULT 2u

/* Packet flags. */
#define SX1262_RX_HAS_MHDR 1u       /* length >= 1: mhdr/mtype/major are the raw first byte */
#define SX1262_RX_LORAWAN_DATA 2u   /* structurally plausible LoRaWAN data frame: dev_addr/fctrl/fcnt16 valid */
#define SX1262_RX_MESHTASTIC_HEADER 4u /* Meshtastic profile and length >= 16 */
/* Payload CRC failed: kept for its RSSI/SNR and raw bytes, never parsed, and
   counted in crc_errors instead of packets. */
#define SX1262_RX_CRC_ERROR 0x80u

typedef struct {
    uint32_t frequency_hz;   /* 863000000..870000000 */
    uint32_t spreading_factor; /* 7..12 */
    uint32_t bandwidth_khz;  /* 125, 250 or 500 */
    uint32_t profile;        /* SX1262_RX_PROFILE_* */
} sx1262_rx_config_t;

typedef struct {
    uint32_t sequence;       /* 1-based, increments per stored packet, wraps */
    uint32_t timestamp_ms;   /* caller-supplied uptime ms at poll that read it */
    uint32_t frequency_hz;   /* profile active when received */
    uint8_t spreading_factor;
    uint8_t flags;
    uint16_t bandwidth_khz;
    int32_t rssi_qdb;        /* packet RSSI, quarter dBm (RssiPkt * -2) */
    int32_t snr_qdb;         /* packet SNR, quarter dB (signed SnrPkt) */
    int32_t signal_rssi_qdb; /* despread signal RSSI, quarter dBm */
    uint8_t length;          /* payload bytes valid in payload[] */
    uint8_t profile;         /* SX1262_RX_PROFILE_* active when received */
    uint8_t mhdr;
    uint8_t mtype;           /* mhdr >> 5 */
    uint8_t major;           /* mhdr & 3 */
    uint8_t fctrl;
    uint8_t fopts_length;
    uint16_t fcnt16;
    uint32_t dev_addr;       /* little-endian over the air, host value here */
    uint8_t payload[SX1262_RX_MAX_PAYLOAD]; /* raw PHY payload; encrypted part not decoded */
} sx1262_rx_packet_t;

typedef struct {
    uint32_t packets;        /* RxDone without CrcErr, stored */
    uint32_t crc_errors;
    uint32_t header_errors;
    uint32_t headers_valid;
    uint32_t timeouts;
    uint32_t overwritten;    /* oldest ring entries replaced */
    uint32_t rearms;         /* radio found outside RX and reconfigured */
    uint32_t polls;
    uint32_t poll_errors;
    uint32_t configs;        /* successful configurations */
    uint32_t config_errors;  /* invalid or failed configuration attempts */
} sx1262_rx_counters_t;

typedef int32_t (*sx1262_service_t)(void);

typedef struct {
    sx1262_service_t service;
    uint32_t state;          /* SX1262_RX_IDLE/RUNNING/FAULT */
    int32_t last_error;      /* most recent error, sticky until next success */
    uint32_t device_errors;  /* last nonzero GetDeviceErrors value */
    uint8_t last_status;     /* last SX1262 status byte */
    uint8_t last_command;    /* most recently attempted opcode */
    uint32_t last_response;  /* first four reply bytes, little-endian, zero on transfer failure */
    uint8_t active_valid;    /* active profile reflects a successful config */
    uint8_t board_confirmed; /* set only by sx1262_receiver_confirm_board */
    uint16_t last_irq;
    sx1262_rx_config_t active;
    sx1262_rx_counters_t counters;
    /* Instantaneous RSSI (GetRssiInst, quarter dBm) sampled while idle in RX.
       Reset on every successful configuration; samples == 0 means none yet. */
    int32_t noise_qdb, noise_min_qdb, noise_max_qdb;
    uint32_t noise_samples, noise_at_ms;
    uint32_t next_sequence;
    uint32_t head;           /* next ring slot */
    uint32_t count;          /* valid ring entries, <= SX1262_RX_RING_SIZE */
    sx1262_rx_packet_t ring[SX1262_RX_RING_SIZE];
    uint8_t scratch[SX1262_RX_MAX_PAYLOAD];
} sx1262_receiver_t;

/* No hardware access. */
void sx1262_receiver_init(sx1262_receiver_t *rx, sx1262_service_t service);
/* Caller asserts the attached board; anything else returns SX1262_RX_BOARD_ERROR. */
int32_t sx1262_receiver_confirm_board(sx1262_receiver_t *rx, uint32_t board);
void sx1262_rx_default_config(sx1262_rx_config_t *config);
int32_t sx1262_rx_validate(const sx1262_rx_config_t *config);

/*
 * Requires sx1262_receiver_confirm_board first (else SX1262_RX_BOARD_ERROR).
 * Validate, then (re)configure and enter continuous RX. When idle/faulted this
 * initializes pins/SPI (and clocks only if not already initialized), resets
 * the radio and performs TCXO/RF-switch/calibration setup. When running it
 * reconfigures live without radio reset and without touching clocks/TIMER1.
 * Invalid config: returns SX1262_RX_CONFIG_ERROR, nothing changes.
 * Failure while running: previous profile is restored if possible (state
 * stays RUNNING, error returned); otherwise radio released, state FAULT.
 */
int32_t sx1262_receiver_configure(sx1262_receiver_t *rx, const sx1262_rx_config_t *config);

/*
 * Bounded poll: GetIrqStatus, read packets/errors, clear IRQs, rearm if not in RX.
 * Returns 0 on success whether or not a packet arrived (check count/sequence or
 * counters.packets for new data); any nonzero return is a negative error.
 * Service cancellation returns the service's own error and leaves state RUNNING;
 * -3001..-3006 set state FAULT with pins released; -3011 means not RUNNING.
 */
int32_t sx1262_receiver_poll(sx1262_receiver_t *rx, uint32_t now_ms);
/* Non-retuning instantaneous RSSI; hardware failures fault the receiver. */
int32_t sx1262_receiver_rssi(sx1262_receiver_t *rx, int32_t *qdb);

/* Receive-only RSSI survey of one frequency anywhere in the SX1262 tuning
   range, measured in the active profile's bandwidth. */
#define SX1262_RX_SURVEY_MIN_HZ 150000000u
#define SX1262_RX_SURVEY_MAX_HZ 960000000u
#define SX1262_RX_SURVEY_SAMPLES 16u

typedef struct {
    uint32_t frequency_hz;
    uint32_t bandwidth_khz;
    uint32_t samples;
    int32_t min_qdb, mean_qdb, max_qdb; /* GetRssiInst, quarter dBm */
} sx1262_rx_survey_t;

/*
 * Requires RUNNING. Calibrates the image band around frequency_hz, enters
 * continuous RX there, takes SX1262_RX_SURVEY_SAMPLES GetRssiInst readings,
 * then always restores the active profile (frames heard on the survey
 * frequency are discarded). Out-of-range frequency: SX1262_RX_CONFIG_ERROR,
 * no radio access. Radio faults release the radio as in poll.
 */
int32_t sx1262_receiver_survey(sx1262_receiver_t *rx, uint32_t frequency_hz,
                               sx1262_rx_survey_t *result);

/* Standby (best effort), release pins/SPI, state IDLE. */
void sx1262_receiver_stop(sx1262_receiver_t *rx);

/* index 0 is the newest packet; returns 0 when index >= count. */
const sx1262_rx_packet_t *sx1262_receiver_packet(const sx1262_receiver_t *rx, uint32_t index);

/* Header inspection only (no decryption/MIC check); exposed for tests.
   LoRaWAN fields are parsed only for the LoRaWAN profile. */
void sx1262_rx_parse(sx1262_rx_packet_t *packet);
/* Meshtastic LongFast preset for EU_868. */
void sx1262_rx_meshtastic_config(sx1262_rx_config_t *config);
void sx1262_rx_lorawan_config(sx1262_rx_config_t *config);

#endif

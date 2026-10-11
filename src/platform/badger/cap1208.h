#ifndef UNIVERSE26_CAP1208_H
#define UNIVERSE26_CAP1208_H

#include <stdint.h>

#define CAP1208_ADDRESS 0x28u
#define CAP1208_ALTERNATE_ADDRESS 0x29u
#define CAP1208_PRODUCT_ID 0x6bu
#define CAP1208_MANUFACTURER_ID 0x5du
#define CAP1208_CHANNELS 8u

#define CAP1208_STATUS_BASE_COUNT_OUT 0x40u
#define CAP1208_STATUS_CALIBRATION_FAILED 0x20u
#define CAP1208_STATUS_POWER 0x10u
#define CAP1208_STATUS_RESET 0x08u
#define CAP1208_STATUS_MULTIPLE_TOUCH 0x04u
#define CAP1208_STATUS_PATTERN 0x02u
#define CAP1208_STATUS_TOUCH 0x01u

typedef enum {
    CAP1208_OK = 0,
    CAP1208_I2C_ERROR = 1,
    CAP1208_ID_MISMATCH = 2,
    CAP1208_CONFIG_ERROR = 3,
    CAP1208_SENSOR_FAULT = 4,
    CAP1208_NOT_READY = 5,
    CAP1208_NOT_INITIALIZED = 6,
    CAP1208_INVALID_ARGUMENT = 7,
} cap1208_result_t;

typedef struct {
    uint8_t touched;
    uint8_t general_status;
    uint8_t noise;
    uint8_t calibrating;
    uint8_t base_count_out_of_limit;
    uint8_t main_control;
    int8_t delta[CAP1208_CHANNELS];
} cap1208_sample_t;

/*
 * Caller powers GPIO41 high and initializes the shared GPIO18/19 bus first.
 * Allow at least 15 ms after power-up for communications (DS00001570C p.9).
 * Probes only documented CAP1208-1/-2 addresses, checking product AND vendor
 * before writing. Revision is informational, not an identity requirement.
 * Outputs start at zero; a complete identity read remains available on error.
 * Enables CS1-8, disables repeat/multiple-touch blocking and power-button
 * qualification, and verifies configuration. Thresholds/sensitivity stay as-is.
 * OK means configured, not calibrated: first conversion can take 200 ms.
 * This singleton driver does not initialize GPIOs or change the selected bus.
 */
cap1208_result_t cap1208_init(uint8_t *product_id, uint8_t *revision);
/* Last identity-verified address, or zero if neither address matched. */
uint8_t cap1208_get_address(void);

/*
 * Bits 0-7 and delta[0-7] mean CS1-CS8; board positions are in docs/spec.md.
 * Reads status on both sides of the delta burst, retrying changes three times.
 * Hardware scans channels sequentially; no simultaneous-sample latch exists.
 * INT is cleared by a fresh main-control RMW only after a complete stable read.
 * Status is captured before clearing, so a released touch may remain for one
 * poll (DS00001570C pp.19,24-27). Noise flags mark invalid channel deltas.
 *
 * Output starts at zero. A complete sample is also returned for SENSOR_FAULT
 * (calibration/base-count failure), CONFIG_ERROR (reset/non-active mode), and
 * NOT_READY while calibrating. NOT_READY from changing status leaves it zero.
 * Retry NOT_READY and transient I2C errors; CONFIG_ERROR requires re-init.
 * Calibration bits alone are not proof of failure (datasheet p.35).
 */
cap1208_result_t cap1208_read(cap1208_sample_t *sample);
const char *cap1208_result_name(cap1208_result_t result);

#endif

#include "cap1208.h"
#include "i2c_bitbang.h"

/* Microchip DS00001570C, pp.10,24-30,35-40,47-49. */
#define REG_MAIN_CONTROL 0x00u
#define REG_GENERAL_STATUS 0x02u
#define REG_NOISE_STATUS 0x0au
#define REG_DELTA 0x10u
#define REG_CONFIGURATION 0x20u
#define REG_SENSOR_ENABLE 0x21u
#define REG_CALIBRATION 0x26u
#define REG_INTERRUPT_ENABLE 0x27u
#define REG_REPEAT_ENABLE 0x28u
#define REG_MULTIPLE_TOUCH 0x2au
#define REG_MULTIPLE_PATTERN 0x2bu
#define REG_BASE_COUNT_OUT 0x2eu
#define REG_CONFIGURATION_2 0x44u
#define REG_POWER_BUTTON 0x61u
#define REG_PRODUCT_ID 0xfdu
#define MAIN_POWER_MASK 0x30u
#define MAIN_INT 0x01u
#define STATUS_FAULT_MASK \
    (CAP1208_STATUS_BASE_COUNT_OUT | CAP1208_STATUS_CALIBRATION_FAILED)

static uint8_t device_address;
static uint8_t initialized;

static void clear_sample(cap1208_sample_t *sample) {
    sample->touched = 0u;
    sample->general_status = 0u;
    sample->noise = 0u;
    sample->calibrating = 0u;
    sample->base_count_out_of_limit = 0u;
    sample->main_control = 0u;
    for (uint32_t i = 0u; i < CAP1208_CHANNELS; ++i) sample->delta[i] = 0;
}

static int read_register(uint8_t reg, uint8_t *value) {
    return badger_i2c_read_registers(device_address, reg, value, 1u);
}

static cap1208_result_t configure(uint8_t reg, uint8_t mask, uint8_t bits) {
    uint8_t value;
    uint8_t verified;
    if (!read_register(reg, &value)) return CAP1208_I2C_ERROR;
    value = (uint8_t)((value & (uint8_t)~mask) | bits);
    if (!badger_i2c_write_register(device_address, reg, value) ||
        !read_register(reg, &verified)) {
        return CAP1208_I2C_ERROR;
    }
    if ((verified & mask) != bits) return CAP1208_CONFIG_ERROR;
    return CAP1208_OK;
}

static int read_status(cap1208_sample_t *sample) {
    uint8_t status[2];
    if (!badger_i2c_read_registers(device_address, REG_GENERAL_STATUS,
                                  status, 2u) ||
        !read_register(REG_NOISE_STATUS, &sample->noise) ||
        !read_register(REG_CALIBRATION, &sample->calibrating) ||
        !read_register(REG_BASE_COUNT_OUT, &sample->base_count_out_of_limit)) {
        return 0;
    }
    sample->general_status = status[0];
    sample->touched = status[1];
    return 1;
}

static int same_status(const cap1208_sample_t *a, const cap1208_sample_t *b) {
    return a->general_status == b->general_status &&
           a->touched == b->touched &&
           a->noise == b->noise &&
           a->calibrating == b->calibrating &&
           a->base_count_out_of_limit == b->base_count_out_of_limit;
}

static int clear_interrupt(uint8_t *main_control) {
    if (!read_register(REG_MAIN_CONTROL, main_control)) return 0;
    /* INT can be reasserted immediately, so do not verify it stays cleared. */
    return badger_i2c_write_register(device_address, REG_MAIN_CONTROL,
                                    (uint8_t)(*main_control & ~MAIN_INT));
}

static int sensor_fault(const cap1208_sample_t *sample) {
    return (sample->general_status & STATUS_FAULT_MASK) != 0u ||
           sample->base_count_out_of_limit != 0u;
}

cap1208_result_t cap1208_init(uint8_t *product_id, uint8_t *revision) {
    static const uint8_t addresses[] = {
        CAP1208_ADDRESS, CAP1208_ALTERNATE_ADDRESS
    };
    static const struct {
        uint8_t reg;
        uint8_t mask;
        uint8_t bits;
    } settings[] = {
        {REG_MAIN_CONTROL, MAIN_POWER_MASK, 0x00u},
        {REG_SENSOR_ENABLE, 0xffu, 0xffu},
        {REG_INTERRUPT_ENABLE, 0xffu, 0xffu},
        {REG_REPEAT_ENABLE, 0xffu, 0x00u},
        {REG_MULTIPLE_TOUCH, 0x80u, 0x00u},
        {REG_MULTIPLE_PATTERN, 0x80u, 0x00u},
        /* Keep analog filtering; a held touch must not trigger recalibration. */
        {REG_CONFIGURATION, 0x18u, 0x00u},
        /* Report both noise sources, filter RF, interrupt on faults/release. */
        {REG_CONFIGURATION_2, 0x1fu, 0x12u},
        {REG_POWER_BUTTON, 0x44u, 0x00u},
    };
    uint8_t identity[3];
    uint8_t found = 0u;
    cap1208_result_t result = CAP1208_I2C_ERROR;
    cap1208_sample_t status;

    initialized = 0u;
    device_address = 0u;
    if (product_id != 0) *product_id = 0u;
    if (revision != 0) *revision = 0u;
    if (product_id == 0 || revision == 0) return CAP1208_INVALID_ARGUMENT;

    for (uint32_t i = 0u; i < sizeof(addresses); ++i) {
        if (!badger_i2c_read_registers(addresses[i], REG_PRODUCT_ID,
                                      identity, sizeof(identity))) {
            continue;
        }
        *product_id = identity[0];
        *revision = identity[2];
        if (identity[0] == CAP1208_PRODUCT_ID &&
            identity[1] == CAP1208_MANUFACTURER_ID) {
            device_address = addresses[i];
            found = 1u;
            break;
        }
        result = CAP1208_ID_MISMATCH;
    }
    if (!found) return result;

    for (uint32_t i = 0u; i < sizeof(settings) / sizeof(settings[0]); ++i) {
        result = configure(settings[i].reg, settings[i].mask, settings[i].bits);
        if (result != CAP1208_OK) return result;
    }
    if (!read_status(&status) || !clear_interrupt(&status.main_control)) {
        return CAP1208_I2C_ERROR;
    }
    initialized = 1u;
    return sensor_fault(&status) ? CAP1208_SENSOR_FAULT : CAP1208_OK;
}

uint8_t cap1208_get_address(void) {
    return device_address;
}

cap1208_result_t cap1208_read(cap1208_sample_t *sample) {
    cap1208_sample_t before;
    cap1208_sample_t after;
    uint8_t delta[CAP1208_CHANNELS];
    if (sample == 0) return CAP1208_INVALID_ARGUMENT;
    clear_sample(sample);
    if (!initialized) return CAP1208_NOT_INITIALIZED;

    for (uint32_t attempt = 0u; attempt < 3u; ++attempt) {
        if (!read_status(&before) ||
            !badger_i2c_read_registers(device_address, REG_DELTA, delta,
                                      sizeof(delta)) ||
            !read_status(&after)) {
            return CAP1208_I2C_ERROR;
        }
        if (!same_status(&before, &after)) continue;
        if (!clear_interrupt(&before.main_control)) return CAP1208_I2C_ERROR;
        for (uint32_t i = 0u; i < CAP1208_CHANNELS; ++i) {
            int16_t value = delta[i];
            if ((delta[i] & 0x80u) != 0u) value -= 256;
            before.delta[i] = (int8_t)value;
        }
        sample->touched = before.touched;
        sample->general_status = before.general_status;
        sample->noise = before.noise;
        sample->calibrating = before.calibrating;
        sample->base_count_out_of_limit = before.base_count_out_of_limit;
        sample->main_control = before.main_control;
        for (uint32_t i = 0u; i < CAP1208_CHANNELS; ++i) {
            sample->delta[i] = before.delta[i];
        }
        if ((before.general_status & CAP1208_STATUS_RESET) != 0u ||
            (before.main_control & MAIN_POWER_MASK) != 0u) {
            initialized = 0u;
            return CAP1208_CONFIG_ERROR;
        }
        if (sensor_fault(&before)) return CAP1208_SENSOR_FAULT;
        if (before.calibrating != 0u) return CAP1208_NOT_READY;
        return CAP1208_OK;
    }
    return CAP1208_NOT_READY;
}

const char *cap1208_result_name(cap1208_result_t result) {
    switch (result) {
        case CAP1208_OK: return "OK";
        case CAP1208_I2C_ERROR: return "I2C ERROR";
        case CAP1208_ID_MISMATCH: return "ID MISMATCH";
        case CAP1208_CONFIG_ERROR: return "CONFIG ERROR";
        case CAP1208_SENSOR_FAULT: return "SENSOR FAULT";
        case CAP1208_NOT_READY: return "NOT READY";
        case CAP1208_NOT_INITIALIZED: return "NOT INITIALIZED";
        case CAP1208_INVALID_ARGUMENT: return "INVALID ARGUMENT";
        default: return "UNKNOWN";
    }
}

#include "lsm6ds3.h"
#include "i2c_bitbang.h"

#define LSM6DS3_WHO_AM_I 0x0fu
#define LSM6DS3_CTRL1_XL 0x10u
#define LSM6DS3_CTRL2_G 0x11u
#define LSM6DS3_CTRL3_C 0x12u
#define LSM6DS3_OUTX_L_G 0x22u

#define LSM6DS3_104HZ 0x40u
#define LSM6DS3_BDU_IF_INC 0x44u

static int16_t decode_i16(const uint8_t *data) {
    uint16_t raw = (uint16_t)data[0] | (uint16_t)data[1] << 8u;
    int32_t value = (int32_t)raw;
    if ((raw & 0x8000u) != 0u) value -= 65536;
    return (int16_t)value;
}

lsm6ds3_result_t lsm6ds3_init(uint8_t *identity) {
    if (!badger_i2c_read_registers(LSM6DS3_ADDRESS, LSM6DS3_WHO_AM_I,
                                   identity, 1u)) {
        return LSM6DS3_I2C_ERROR;
    }
    if (*identity != LSM6DS3_WHO_AM_I_VALUE) {
        return LSM6DS3_ID_MISMATCH;
    }
    if (!badger_i2c_write_register(LSM6DS3_ADDRESS, LSM6DS3_CTRL3_C,
                                   LSM6DS3_BDU_IF_INC) ||
        !badger_i2c_write_register(LSM6DS3_ADDRESS, LSM6DS3_CTRL1_XL,
                                   LSM6DS3_104HZ) ||
        !badger_i2c_write_register(LSM6DS3_ADDRESS, LSM6DS3_CTRL2_G,
                                   LSM6DS3_104HZ)) {
        return LSM6DS3_I2C_ERROR;
    }
    return LSM6DS3_OK;
}

int lsm6ds3_read(lsm6ds3_sample_t *sample) {
    uint8_t data[12];
    if (!badger_i2c_read_registers(LSM6DS3_ADDRESS, LSM6DS3_OUTX_L_G,
                                   data, sizeof(data))) {
        return 0;
    }

    sample->gyro_x = decode_i16(data);
    sample->gyro_y = decode_i16(data + 2u);
    sample->gyro_z = decode_i16(data + 4u);
    sample->accel_x = decode_i16(data + 6u);
    sample->accel_y = decode_i16(data + 8u);
    sample->accel_z = decode_i16(data + 10u);
    return 1;
}

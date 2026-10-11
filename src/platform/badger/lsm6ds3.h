#ifndef UNIVERSE26_LSM6DS3_H
#define UNIVERSE26_LSM6DS3_H

#include <stdint.h>

#define LSM6DS3_ADDRESS 0x6au
#define LSM6DS3_WHO_AM_I_VALUE 0x6au

typedef enum {
    LSM6DS3_OK = 0,
    LSM6DS3_I2C_ERROR = 1,
    LSM6DS3_ID_MISMATCH = 2,
} lsm6ds3_result_t;

typedef struct {
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
} lsm6ds3_sample_t;

lsm6ds3_result_t lsm6ds3_init(uint8_t *identity);
int lsm6ds3_read(lsm6ds3_sample_t *sample);

#endif

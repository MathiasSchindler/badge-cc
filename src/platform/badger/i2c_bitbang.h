#ifndef UNIVERSE26_I2C_BITBANG_H
#define UNIVERSE26_I2C_BITBANG_H

#include <stdint.h>

typedef enum {
    BADGER_I2C_OK = 0,
    BADGER_I2C_LINES_NOT_HIGH = 1,
    BADGER_I2C_CLOCK_STRETCH_TIMEOUT = 2,
    BADGER_I2C_INVALID_PINS = 3,
} badger_i2c_result_t;

badger_i2c_result_t badger_i2c_init(void);
badger_i2c_result_t badger_i2c_init_pins(uint32_t sda, uint32_t scl);
int badger_i2c_probe_read(uint8_t address, uint8_t *value);
int badger_i2c_write_register(uint8_t address, uint8_t reg, uint8_t value);
int badger_i2c_read_registers(uint8_t address, uint8_t reg,
                              uint8_t *data, uint32_t length);

#endif

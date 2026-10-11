#include "i2c_bitbang.h"

#define IO_BANK0_BASE 0x40028000u
#define PADS_BANK0_BASE 0x40038000u
#define SIO_BASE 0xd0000000u
#define TIMER0_TIMELR (*(volatile uint32_t *)(0x400b0000u + 0x0cu))

#define GPIO_CTRL(gpio) \
    (*(volatile uint32_t *)(IO_BANK0_BASE + 0x04u + (gpio) * 8u))
#define GPIO_STATUS(gpio) \
    (*(volatile uint32_t *)(IO_BANK0_BASE + (gpio) * 8u))
#define GPIO_PAD(gpio) \
    (*(volatile uint32_t *)(PADS_BANK0_BASE + 0x04u + (gpio) * 4u))
#define SIO_GPIO_OUT_CLR (*(volatile uint32_t *)(SIO_BASE + 0x20u))
#define SIO_GPIO_HI_OUT_CLR (*(volatile uint32_t *)(SIO_BASE + 0x24u))
#define SIO_GPIO_OE_SET (*(volatile uint32_t *)(SIO_BASE + 0x38u))
#define SIO_GPIO_HI_OE_SET (*(volatile uint32_t *)(SIO_BASE + 0x3cu))
#define SIO_GPIO_OE_CLR (*(volatile uint32_t *)(SIO_BASE + 0x40u))
#define SIO_GPIO_HI_OE_CLR (*(volatile uint32_t *)(SIO_BASE + 0x44u))

#define GPIO_FUNC_SIO 5u
#define GPIO_STATUS_INFROMPAD (1u << 17u)
#define GPIO_PAD_ISO (1u << 8u)
#define GPIO_PAD_OD (1u << 7u)
#define GPIO_PAD_IE (1u << 6u)
#define GPIO_PAD_PUE (1u << 3u)
#define GPIO_PAD_PDE (1u << 2u)

#define I2C_HALF_PERIOD_US 5u
#define I2C_STRETCH_TIMEOUT_US 1000u

static int bus_failed;
static uint32_t i2c_sda = 4u;
static uint32_t i2c_scl = 5u;

static uint32_t time_us(void) {
    return TIMER0_TIMELR;
}

static void delay_us(uint32_t duration) {
    uint32_t start = time_us();
    while (time_us() - start < duration) {}
}

static int gpio_level(uint32_t gpio) {
    return (GPIO_STATUS(gpio) & GPIO_STATUS_INFROMPAD) != 0u;
}

static void drive_low(uint32_t gpio) {
    if (gpio < 32u) {
        SIO_GPIO_OE_SET = 1u << gpio;
    } else {
        SIO_GPIO_HI_OE_SET = 1u << (gpio - 32u);
    }
}

static void release_line(uint32_t gpio) {
    if (gpio < 32u) {
        SIO_GPIO_OE_CLR = 1u << gpio;
    } else {
        SIO_GPIO_HI_OE_CLR = 1u << (gpio - 32u);
    }
}

static int release_clock(void) {
    uint32_t start;
    release_line(i2c_scl);
    start = time_us();
    while (!gpio_level(i2c_scl)) {
        if (time_us() - start >= I2C_STRETCH_TIMEOUT_US) {
            bus_failed = 1;
            return 0;
        }
    }
    return 1;
}

static int i2c_start(void) {
    release_line(i2c_sda);
    if (!release_clock()) return 0;
    delay_us(I2C_HALF_PERIOD_US);
    drive_low(i2c_sda);
    delay_us(I2C_HALF_PERIOD_US);
    drive_low(i2c_scl);
    return 1;
}

static void i2c_stop(void) {
    drive_low(i2c_sda);
    delay_us(I2C_HALF_PERIOD_US);
    int clock_released = release_clock();
    if (clock_released) delay_us(I2C_HALF_PERIOD_US);
    release_line(i2c_sda);
    release_line(i2c_scl);
    delay_us(I2C_HALF_PERIOD_US);
}

static int i2c_write_byte(uint8_t value) {
    for (uint32_t bit = 0; bit < 8u; ++bit) {
        if ((value & 0x80u) != 0u) release_line(i2c_sda);
        else drive_low(i2c_sda);
        delay_us(I2C_HALF_PERIOD_US);
        if (!release_clock()) {
            release_line(i2c_sda);
            release_line(i2c_scl);
            return 0;
        }
        delay_us(I2C_HALF_PERIOD_US);
        drive_low(i2c_scl);
        value <<= 1u;
    }

    release_line(i2c_sda);
    delay_us(I2C_HALF_PERIOD_US);
    if (!release_clock()) return 0;
    int acknowledged = !gpio_level(i2c_sda);
    delay_us(I2C_HALF_PERIOD_US);
    drive_low(i2c_scl);
    return acknowledged;
}

static uint8_t i2c_read_byte(int acknowledge) {
    uint8_t value = 0u;
    release_line(i2c_sda);
    for (uint32_t bit = 0; bit < 8u; ++bit) {
        delay_us(I2C_HALF_PERIOD_US);
        if (!release_clock()) return 0xffu;
        value = (uint8_t)((value << 1u) | gpio_level(i2c_sda));
        delay_us(I2C_HALF_PERIOD_US);
        drive_low(i2c_scl);
    }

    if (acknowledge) drive_low(i2c_sda);
    else release_line(i2c_sda);
    delay_us(I2C_HALF_PERIOD_US);
    if (release_clock()) {
        delay_us(I2C_HALF_PERIOD_US);
        drive_low(i2c_scl);
    }
    release_line(i2c_sda);
    return value;
}

badger_i2c_result_t badger_i2c_init(void) {
    return badger_i2c_init_pins(4u, 5u);
}

badger_i2c_result_t badger_i2c_init_pins(uint32_t sda, uint32_t scl) {
    uint32_t pad_mask = GPIO_PAD_ISO | GPIO_PAD_OD |
                        GPIO_PAD_PUE | GPIO_PAD_PDE;

    if (sda > 47u || scl > 47u || sda == scl) {
        return BADGER_I2C_INVALID_PINS;
    }
    i2c_sda = sda;
    i2c_scl = scl;

    GPIO_PAD(i2c_sda) =
        (GPIO_PAD(i2c_sda) & ~pad_mask) | GPIO_PAD_OD | GPIO_PAD_IE;
    GPIO_PAD(i2c_scl) =
        (GPIO_PAD(i2c_scl) & ~pad_mask) | GPIO_PAD_OD | GPIO_PAD_IE;
    if (!gpio_level(i2c_sda) || !gpio_level(i2c_scl)) {
        return BADGER_I2C_LINES_NOT_HIGH;
    }

    if (i2c_sda < 32u) SIO_GPIO_OUT_CLR = 1u << i2c_sda;
    else SIO_GPIO_HI_OUT_CLR = 1u << (i2c_sda - 32u);
    if (i2c_scl < 32u) SIO_GPIO_OUT_CLR = 1u << i2c_scl;
    else SIO_GPIO_HI_OUT_CLR = 1u << (i2c_scl - 32u);
    release_line(i2c_sda);
    release_line(i2c_scl);
    GPIO_CTRL(i2c_sda) = GPIO_FUNC_SIO;
    GPIO_CTRL(i2c_scl) = GPIO_FUNC_SIO;
    GPIO_PAD(i2c_sda) =
        (GPIO_PAD(i2c_sda) & ~pad_mask) | GPIO_PAD_IE;
    GPIO_PAD(i2c_scl) =
        (GPIO_PAD(i2c_scl) & ~pad_mask) | GPIO_PAD_IE;
    bus_failed = 0;

    if (!gpio_level(i2c_sda)) {
        return BADGER_I2C_LINES_NOT_HIGH;
    }
    if (!release_clock()) return BADGER_I2C_CLOCK_STRETCH_TIMEOUT;
    return BADGER_I2C_OK;
}

int badger_i2c_write_register(uint8_t address, uint8_t reg, uint8_t value) {
    bus_failed = 0;
    if (!i2c_start()) return 0;
    if (!i2c_write_byte((uint8_t)(address << 1u)) ||
        !i2c_write_byte(reg) ||
        !i2c_write_byte(value)) {
        i2c_stop();
        return 0;
    }
    i2c_stop();
    return !bus_failed;
}

int badger_i2c_probe_read(uint8_t address, uint8_t *value) {
    bus_failed = 0;
    if (!i2c_start()) return 0;
    if (!i2c_write_byte((uint8_t)((address << 1u) | 1u))) {
        i2c_stop();
        return 0;
    }
    *value = i2c_read_byte(0);
    i2c_stop();
    return !bus_failed;
}

int badger_i2c_read_registers(uint8_t address, uint8_t reg,
                              uint8_t *data, uint32_t length) {
    if (length == 0u) return 0;
    bus_failed = 0;
    if (!i2c_start()) return 0;
    if (!i2c_write_byte((uint8_t)(address << 1u)) ||
        !i2c_write_byte(reg) ||
        !i2c_start() ||
        !i2c_write_byte((uint8_t)((address << 1u) | 1u))) {
        i2c_stop();
        return 0;
    }
    for (uint32_t index = 0; index < length; ++index) {
        data[index] = i2c_read_byte(index + 1u < length);
        if (bus_failed) {
            i2c_stop();
            return 0;
        }
    }
    i2c_stop();
    return !bus_failed;
}

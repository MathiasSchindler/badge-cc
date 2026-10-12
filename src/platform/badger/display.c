#include "display.h"

#ifndef BADGER_DISPLAY_TIME_US
#define BADGER_DISPLAY_TIME_US() (*(volatile uint32_t *)0x400b000cu)
#endif

#ifdef BADGER_DISPLAY_SDK_TIMING
#include "pico/time.h"
#endif

#define BADGER_IO_BANK0_BASE 0x40028000u
#define BADGER_PADS_BANK0_BASE 0x40038000u
#define BADGER_SIO_BASE 0xd0000000u

#define GPIO_FUNC_SIO 5u
#define GPIO_CTRL(gpio) \
    (*(volatile uint32_t *)(uintptr_t)(BADGER_IO_BANK0_BASE + 0x04u + (gpio) * 8u))
#define GPIO_PAD(gpio) \
    (*(volatile uint32_t *)(uintptr_t)(BADGER_PADS_BANK0_BASE + 0x04u + (gpio) * 4u))
#define GPIO_PAD_8MA_FAST 0x21u

#define SIO_GPIO_OUT_SET \
    (*(volatile uint32_t *)(BADGER_SIO_BASE + 0x18u))
#define SIO_GPIO_HI_OUT_SET \
    (*(volatile uint32_t *)(BADGER_SIO_BASE + 0x1cu))
#define SIO_GPIO_OUT_CLR \
    (*(volatile uint32_t *)(BADGER_SIO_BASE + 0x20u))
#define SIO_GPIO_HI_OUT_CLR \
    (*(volatile uint32_t *)(BADGER_SIO_BASE + 0x24u))
#define SIO_GPIO_OE_SET \
    (*(volatile uint32_t *)(BADGER_SIO_BASE + 0x38u))
#define SIO_GPIO_HI_OE_SET \
    (*(volatile uint32_t *)(BADGER_SIO_BASE + 0x3cu))

#define LCD_BACKLIGHT 26u
#define LCD_CS 27u
#define LCD_DC 28u
#define LCD_WR 30u
#define LCD_RD 31u
#define LCD_DATA0 32u
#define LCD_DATA_MASK 0xffu
#define BOARD_POWER_EN 41u

#define ST7789_SWRESET 0x01u
#define ST7789_SLPOUT 0x11u
#define ST7789_SLPIN 0x10u
#define ST7789_DISPOFF 0x28u
#define ST7789_INVON 0x21u
#define ST7789_DISPON 0x29u
#define ST7789_CASET 0x2au
#define ST7789_RASET 0x2bu
#define ST7789_RAMWR 0x2cu
#define ST7789_MADCTL 0x36u
#define ST7789_COLMOD 0x3au
#define ST7789_PORCTRL 0xb2u
#define ST7789_RAMCTRL 0xb0u
#define ST7789_GCTRL 0xb7u
#define ST7789_VCOMS 0xbbu
#define ST7789_LCMCTRL 0xc0u
#define ST7789_VDVVRHEN 0xc2u
#define ST7789_VRHS 0xc3u
#define ST7789_VDVS 0xc4u
#define ST7789_FRCTRL2 0xc6u
#define ST7789_PWCTRL1 0xd0u
#define ST7789_GMCTRP1 0xe0u
#define ST7789_GMCTRN1 0xe1u

#if defined(__arm__) && defined(BADGER_FAST_BYTES) && !defined(BADGER_DISPLAY_SDK_TIMING)
extern void badger_display_write_pixels_hw(const uint16_t *pixels, uint32_t count,
                                            volatile uint32_t *sio);
#endif

#ifdef BADGER_DISPLAY_SDK_TIMING
static void delay_cycles(uint32_t cycles) {
    while (cycles-- != 0u) {
        __asm__ volatile ("nop");
    }
}
#endif

static void gpio_put(uint32_t gpio, uint32_t value) {
#ifdef BADGER_DISPLAY_GPIO_PUT
    BADGER_DISPLAY_GPIO_PUT(gpio, value);
#else
    if (gpio < 32u) {
        *(value ? &SIO_GPIO_OUT_SET : &SIO_GPIO_OUT_CLR) = 1u << gpio;
    } else {
        *(value ? &SIO_GPIO_HI_OUT_SET : &SIO_GPIO_HI_OUT_CLR) =
            1u << (gpio - 32u);
    }
#endif
}

static void gpio_output(uint32_t gpio, uint32_t value) {
    gpio_put(gpio, value);
    GPIO_PAD(gpio) = GPIO_PAD_8MA_FAST;
    GPIO_CTRL(gpio) = GPIO_FUNC_SIO;
    if (gpio < 32u) {
        SIO_GPIO_OE_SET = 1u << gpio;
    } else {
        SIO_GPIO_HI_OE_SET = 1u << (gpio - 32u);
    }
}

static void lcd_write_byte(uint8_t value) {
#ifdef BADGER_DISPLAY_WRITE_BYTE
    BADGER_DISPLAY_WRITE_BYTE(value);
#else
    SIO_GPIO_HI_OUT_CLR = LCD_DATA_MASK;
    SIO_GPIO_HI_OUT_SET = value;
#ifdef BADGER_DISPLAY_SDK_TIMING
    delay_cycles(4u);
    SIO_GPIO_OUT_CLR = 1u << LCD_WR;
    delay_cycles(8u);
    SIO_GPIO_OUT_SET = 1u << LCD_WR;
    delay_cycles(4u);
#else
    SIO_GPIO_OUT_CLR = 1u << LCD_WR;
    __asm__ volatile ("nop\nnop\nnop");
    SIO_GPIO_OUT_SET = 1u << LCD_WR;
    __asm__ volatile ("nop\nnop");
#endif
#endif
}

static void lcd_command(uint8_t command, const uint8_t *data,
                        uint32_t length) {
    gpio_put(LCD_DC, 0u);
    gpio_put(LCD_CS, 0u);
    lcd_write_byte(command);
    if (data != 0) {
        gpio_put(LCD_DC, 1u);
        for (uint32_t index = 0; index < length; ++index) {
            lcd_write_byte(data[index]);
        }
    }
    gpio_put(LCD_CS, 1u);
}

static void lcd_command0(uint8_t command) {
    lcd_command(command, 0, 0u);
}

static int init_delay(int (*service)(void *), void *context) {
#ifdef BADGER_DISPLAY_SDK_TIMING
    for (uint32_t ms = 0u; ms < 150u; ++ms) {
        if (service != 0 && !service(context)) return 0;
        sleep_ms(1u);
    }
#else
    uint32_t started = BADGER_DISPLAY_TIME_US();
    while (BADGER_DISPLAY_TIME_US() - started < 150000u) {
        if (service != 0 && !service(context)) return 0;
    }
#endif
    return 1;
}

int badger_display_init_serviced(int (*service)(void *), void *context) {
    static const uint8_t colmod[] = {0x05u};
    static const uint8_t porctrl[] =
        {0x0cu, 0x0cu, 0x00u, 0x33u, 0x33u};
    static const uint8_t lcmctrl[] = {0x2cu};
    static const uint8_t vdvvrhen[] = {0x01u};
    static const uint8_t vrhs[] = {0x0fu};
    static const uint8_t vdvs[] = {0x20u};
    static const uint8_t pwctrl1[] = {0xa4u, 0xa1u};
    static const uint8_t frctrl2[] = {0x0fu};
    static const uint8_t ramctrl[] = {0x00u, 0xc0u};
    static const uint8_t gctrl[] = {0x35u};
    static const uint8_t vcoms[] = {0x1bu};
    static const uint8_t gamma_pos[] = {
        0xf0u, 0x00u, 0x06u, 0x04u, 0x05u, 0x05u, 0x31u,
        0x44u, 0x48u, 0x36u, 0x12u, 0x12u, 0x2bu, 0x34u,
    };
    static const uint8_t gamma_neg[] = {
        0xf0u, 0x0bu, 0x0fu, 0x0fu, 0x0du, 0x26u, 0x31u,
        0x43u, 0x47u, 0x38u, 0x14u, 0x14u, 0x2cu, 0x32u,
    };
    static const uint8_t columns[] = {0x00u, 0x00u, 0x00u, 0xefu};
    static const uint8_t rows[] = {0x00u, 0x00u, 0x01u, 0x3fu};
    static const uint8_t madctl[] = {0x90u};

    gpio_output(BOARD_POWER_EN, 1u);
    gpio_output(LCD_BACKLIGHT, 0u);
    gpio_output(LCD_CS, 1u);
    gpio_output(LCD_DC, 1u);
    gpio_output(LCD_WR, 1u);
    gpio_output(LCD_RD, 1u);
    for (uint32_t pin = LCD_DATA0; pin < LCD_DATA0 + 8u; ++pin) {
        gpio_output(pin, 0u);
    }

    lcd_command0(ST7789_SWRESET);
    if (!init_delay(service, context)) return 0;
    lcd_command(ST7789_COLMOD, colmod, sizeof(colmod));
    lcd_command(ST7789_PORCTRL, porctrl, sizeof(porctrl));
    lcd_command(ST7789_LCMCTRL, lcmctrl, sizeof(lcmctrl));
    lcd_command(ST7789_VDVVRHEN, vdvvrhen, sizeof(vdvvrhen));
    lcd_command(ST7789_VRHS, vrhs, sizeof(vrhs));
    lcd_command(ST7789_VDVS, vdvs, sizeof(vdvs));
    lcd_command(ST7789_PWCTRL1, pwctrl1, sizeof(pwctrl1));
    lcd_command(ST7789_FRCTRL2, frctrl2, sizeof(frctrl2));
    lcd_command(ST7789_RAMCTRL, ramctrl, sizeof(ramctrl));
    lcd_command(ST7789_GCTRL, gctrl, sizeof(gctrl));
    lcd_command(ST7789_VCOMS, vcoms, sizeof(vcoms));
    lcd_command(ST7789_GMCTRP1, gamma_pos, sizeof(gamma_pos));
    lcd_command(ST7789_GMCTRN1, gamma_neg, sizeof(gamma_neg));
    lcd_command0(ST7789_INVON);
    lcd_command0(ST7789_SLPOUT);
    if (!init_delay(service, context)) return 0;
    lcd_command(ST7789_CASET, columns, sizeof(columns));
    lcd_command(ST7789_RASET, rows, sizeof(rows));
    lcd_command(ST7789_MADCTL, madctl, sizeof(madctl));
    return 1;
}

void badger_display_write_pixels(const uint16_t *pixels, uint32_t count) {
#if defined(__arm__) && defined(BADGER_FAST_BYTES) && !defined(BADGER_DISPLAY_SDK_TIMING)
    badger_display_write_pixels_hw(pixels, count, (volatile uint32_t *)BADGER_SIO_BASE);
#else
    for (uint32_t i = 0u; i < count; ++i) badger_display_write(pixels[i]);
#endif
}

void badger_display_init(void) {
    (void)badger_display_init_serviced(0, 0);
}

void badger_display_begin(void) {
    badger_display_begin_region(0u, 0u, BADGER_DISPLAY_WIDTH,
                                BADGER_DISPLAY_HEIGHT);
}

void badger_display_begin_region(uint32_t x, uint32_t y,
                                 uint32_t width, uint32_t height) {
    uint32_t x_end = x + width - 1u;
    uint32_t y_end = y + height - 1u;
    uint8_t columns[] = {
        (uint8_t)(y >> 8), (uint8_t)y,
        (uint8_t)(y_end >> 8), (uint8_t)y_end,
    };
    uint8_t rows[] = {
        (uint8_t)(x >> 8), (uint8_t)x,
        (uint8_t)(x_end >> 8), (uint8_t)x_end,
    };

    lcd_command(ST7789_CASET, columns, sizeof(columns));
    lcd_command(ST7789_RASET, rows, sizeof(rows));
    gpio_put(LCD_DC, 0u);
    gpio_put(LCD_CS, 0u);
    lcd_write_byte(ST7789_RAMWR);
    gpio_put(LCD_DC, 1u);
}

void badger_display_write(uint16_t pixel) {
    lcd_write_byte((uint8_t)(pixel >> 8));
    lcd_write_byte((uint8_t)pixel);
}

void badger_display_end_update(void) {
    gpio_put(LCD_CS, 1u);
}

void badger_display_end(void) {
    badger_display_end_update();
    lcd_command0(ST7789_DISPON);
    gpio_put(LCD_BACKLIGHT, 1u);
}

void badger_display_set_tearing(int enabled) {
    static const uint8_t vertical_only[] = {0u};
    if (enabled) lcd_command(0x35u, vertical_only, sizeof(vertical_only));
    else lcd_command0(0x34u);
}

void badger_display_sleep(void) {
    gpio_put(LCD_BACKLIGHT, 0u);
    lcd_command0(ST7789_DISPOFF);
    lcd_command0(ST7789_SLPIN);
    init_delay(0, 0);
}

void badger_display_wake(void) {
    lcd_command0(ST7789_SLPOUT);
    init_delay(0, 0);
}

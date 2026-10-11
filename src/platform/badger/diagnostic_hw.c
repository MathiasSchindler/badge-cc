#include "diagnostic_hw.h"

#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define PAD(pin) REG(0x40038004u + (pin) * 4u)
#define CTRL(pin) REG(0x40028004u + (pin) * 8u)
#define INTR(pin) REG(0x40028230u + ((pin) / 8u) * 4u)
#define SIO_SET REG(0xd0000018u)
#define SIO_CLR REG(0xd0000020u)
#define SIO_OE_SET REG(0xd0000038u)
#define SIO_OE_CLR REG(0xd0000040u)
#define RESET_SET REG(0x40022000u)
#define RESET_CLR REG(0x40023000u)
#define RESET_DONE REG(0x40020008u)
#define ADC_CS REG(0x400a0000u)
#define ADC_RESULT REG(0x400a0004u)
#define ADC_READY (1u << 8u)
#define PWM_CSR REG(0x400a8000u)
#define PWM_DIV REG(0x400a8004u)
#define PWM_CTR REG(0x400a8008u)
#define PWM_CC REG(0x400a800cu)
#define PWM_TOP REG(0x400a8010u)

static uint8_t ir_ready;

uint32_t diag_time_us(void) {
    return REG(0x400b000cu);
}

void diag_wait_us(uint32_t duration) {
    uint32_t start = diag_time_us();
    while (diag_time_us() - start < duration) {}
}

static int wait_bits(volatile uint32_t *reg, uint32_t mask) {
    uint32_t start = diag_time_us();
    while ((*reg & mask) != mask) {
        if (diag_time_us() - start >= 5000u) return 0;
    }
    return 1;
}

static void input(uint32_t pin, uint8_t pull_up) {
    SIO_OE_CLR = 1u << pin;
    PAD(pin) = (1u << 7u) | (1u << 6u) | (1u << 1u) |
               (pull_up ? 1u << 3u : 0u);
    CTRL(pin) = 5u;
}

static void output(uint32_t pin) {
    SIO_CLR = 1u << pin;
    CTRL(pin) = 5u;
    PAD(pin) = 0x12u;
    SIO_OE_SET = 1u << pin;
}

void diag_gpio_init(void) {
    const uint8_t buttons[] = {7u, 9u, 10u, 11u, 6u, 22u};
    const uint8_t inputs[] = {12u, 14u, 17u, 20u, 21u};
    for (uint32_t i = 0; i < sizeof(buttons); ++i) input(buttons[i], 1u);
    for (uint32_t i = 0; i < sizeof(inputs); ++i) input(inputs[i], 0u);
    input(13u, 1u);
    input(15u, 1u);
    for (uint32_t pin = 0; pin < 4u; ++pin) output(pin);
    output(16u);
}

uint8_t diag_gpio_level(uint32_t pin) {
    return (uint8_t)((REG(0x40028000u + pin * 8u) >> 17u) & 1u);
}

uint8_t diag_gpio_take_edges(uint32_t pin) {
    uint32_t shift = (pin % 8u) * 4u;
    uint32_t flags = INTR(pin) & (0xcu << shift);
    INTR(pin) = flags;
    return (uint8_t)(flags >> (shift + 2u));
}

uint8_t diag_buttons_raw(void) {
    const uint8_t pins[] = {7u, 9u, 10u, 11u, 6u, 22u};
    uint8_t pressed = 0u;
    for (uint32_t i = 0; i < sizeof(pins); ++i) {
        if (!diag_gpio_level(pins[i])) pressed |= (uint8_t)(1u << i);
    }
    return pressed;
}

void diag_leds(uint8_t mask) {
    SIO_CLR = 0x0fu;
    SIO_SET = mask & 0x0fu;
}

void diag_backlight(uint8_t on) {
    if (on) SIO_SET = 1u << 26u;
    else SIO_CLR = 1u << 26u;
}

diag_hw_result_t diag_adc_init(void) {
    if ((REG(0x40048004u) & (1u << 12u)) == 0u) {
        REG(0x4004800cu) = 196u;
        REG(0x40048000u) = (0xfabu << 12u) | 0xaa0u;
    }
    if (!wait_bits(&REG(0x40048004u), 1u << 31u)) return DIAG_HW_CLOCK_ERROR;
    RESET_SET = 1u;
    REG(0x4001006cu) = 0u;
    diag_wait_us(10u);
    REG(0x40010070u) = 1u << 16u;
    REG(0x4001006cu) = (3u << 5u) | (1u << 11u);
    RESET_CLR = 1u;
    if (!wait_bits(&RESET_DONE, 1u)) return DIAG_HW_TIMEOUT;

    const uint8_t pins[] = {40u, 42u, 43u};
    for (uint32_t i = 0; i < sizeof(pins); ++i) {
        PAD(pins[i]) = 1u << 7u;
        CTRL(pins[i]) = 31u;
    }
    ADC_CS = 1u;
    if (!wait_bits(&ADC_CS, ADC_READY)) return DIAG_HW_TIMEOUT;
    return DIAG_HW_OK;
}

diag_hw_result_t diag_adc_read(uint8_t channel, uint16_t *value) {
    *value = 0u;
    if (channel != 0u && channel != 2u && channel != 3u) {
        return DIAG_HW_INVALID_CHANNEL;
    }
    ADC_CS = 1u | (1u << 10u) | ((uint32_t)channel << 12u);
    if (!wait_bits(&ADC_CS, ADC_READY)) return DIAG_HW_TIMEOUT;
    /* Discard the first conversion after switching the analog mux. */
    for (uint32_t i = 0; i < 2u; ++i) {
        ADC_CS = 1u | (1u << 2u) | ((uint32_t)channel << 12u);
        if (!wait_bits(&ADC_CS, ADC_READY)) return DIAG_HW_TIMEOUT;
        if ((ADC_CS & (1u << 9u)) != 0u) return DIAG_HW_ADC_ERROR;
        *value = (uint16_t)(ADC_RESULT & 0xfffu);
    }
    return DIAG_HW_OK;
}

diag_hw_result_t diag_ir_init(uint32_t *carrier_hz) {
    ir_ready = 0u;
    *carrier_hz = 0u;
    CTRL(16u) = 5u;
    SIO_CLR = 1u << 16u;
    RESET_CLR = 1u << 16u;
    if (!wait_bits(&RESET_DONE, 1u << 16u)) return DIAG_HW_TIMEOUT;
    PWM_CSR = 0u;
    PWM_DIV = 16u << 4u;
    PWM_TOP = 65535u;
    PWM_CC = 0u;
    PWM_CTR = 0u;
    PWM_CSR = 1u;
    uint32_t start = diag_time_us();
    uint32_t before = PWM_CTR;
    diag_wait_us(1000u);
    uint32_t after = PWM_CTR;
    uint32_t elapsed = diag_time_us() - start;
    PWM_CSR = 0u;
    uint32_t ticks = (after - before) & 0xffffu;
    if (ticks < 100u || ticks > 60000u || elapsed > 2000u || elapsed == 0u) {
        return DIAG_HW_CLOCK_ERROR;
    }
    /* Measure PWM's clock against the existing microsecond timer, not a
       presumed boot-time clk_sys frequency. GPIO16 stays low throughout. */
    uint32_t period = (ticks * 1000u + 19u * elapsed) / (38u * elapsed);
    if (period < 8u || period > 65535u) return DIAG_HW_CLOCK_ERROR;
    PWM_TOP = period - 1u;
    PWM_CC = period / 3u;
    PWM_CTR = 0u;
    *carrier_hz = (ticks * 1000u / elapsed) * 1000u / period;
    ir_ready = 1u;
    return DIAG_HW_OK;
}

diag_hw_result_t diag_ir_send(void) {
    if (!ir_ready) return DIAG_HW_CLOCK_ERROR;
    for (uint32_t burst = 0; burst < 12u; ++burst) {
        PWM_CTR = 0u;
        PWM_CSR = 1u;
        CTRL(16u) = 4u;
        diag_wait_us(600u);
        CTRL(16u) = 5u;
        SIO_CLR = 1u << 16u;
        PWM_CSR = 0u;
        diag_wait_us(5400u);
    }
    return DIAG_HW_OK;
}

void diag_ir_capture(diag_ir_capture_t *capture) {
    capture->edges = 0u;
    capture->low_pulses = 0u;
    capture->shortest_us = 0u;
    capture->longest_us = 0u;
    uint32_t start = diag_time_us();
    uint32_t low_start = 0u;
    uint8_t have_low_start = 0u;
    uint8_t previous = diag_gpio_level(17u);
    while (diag_time_us() - start < 250000u) {
        uint32_t now = diag_time_us();
        uint8_t level = diag_gpio_level(17u);
        if (level == previous) continue;
        previous = level;
        ++capture->edges;
        if (!level) {
            low_start = now;
            have_low_start = 1u;
        } else if (have_low_start) {
            uint32_t width = now - low_start;
            if (capture->low_pulses == 0u || width < capture->shortest_us) {
                capture->shortest_us = width;
            }
            if (width > capture->longest_us) capture->longest_us = width;
            ++capture->low_pulses;
            have_low_start = 0u;
        }
    }
}

const char *diag_hw_result_name(diag_hw_result_t result) {
    switch (result) {
        case DIAG_HW_OK: return "OK";
        case DIAG_HW_CLOCK_ERROR: return "CLOCK ERROR";
        case DIAG_HW_TIMEOUT: return "HARDWARE TIMEOUT";
        case DIAG_HW_ADC_ERROR: return "ADC CONVERSION ERROR";
        case DIAG_HW_INVALID_CHANNEL: return "INVALID ADC CHANNEL";
    }
    return "UNKNOWN HARDWARE ERROR";
}

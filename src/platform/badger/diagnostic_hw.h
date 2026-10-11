#ifndef UNIVERSE26_DIAGNOSTIC_HW_H
#define UNIVERSE26_DIAGNOSTIC_HW_H

#include <stdint.h>

typedef enum {
    DIAG_HW_OK,
    DIAG_HW_CLOCK_ERROR,
    DIAG_HW_TIMEOUT,
    DIAG_HW_ADC_ERROR,
    DIAG_HW_INVALID_CHANNEL
} diag_hw_result_t;

typedef struct {
    uint32_t edges;
    uint32_t low_pulses;
    uint32_t shortest_us;
    uint32_t longest_us;
} diag_ir_capture_t;

uint32_t diag_time_us(void);
void diag_wait_us(uint32_t duration);
void diag_gpio_init(void);
uint8_t diag_gpio_level(uint32_t pin);
uint8_t diag_gpio_take_edges(uint32_t pin);
uint8_t diag_buttons_raw(void);
void diag_leds(uint8_t mask);
void diag_backlight(uint8_t on);
diag_hw_result_t diag_adc_init(void);
diag_hw_result_t diag_adc_read(uint8_t channel, uint16_t *value);
diag_hw_result_t diag_ir_init(uint32_t *carrier_hz);
diag_hw_result_t diag_ir_send(void);
void diag_ir_capture(diag_ir_capture_t *capture);
const char *diag_hw_result_name(diag_hw_result_t result);

#endif

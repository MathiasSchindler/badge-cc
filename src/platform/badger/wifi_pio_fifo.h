#ifndef UNIVERSE26_WIFI_PIO_FIFO_H
#define UNIVERSE26_WIFI_PIO_FIFO_H

#include <stdint.h>

typedef struct {
    volatile uint32_t *status;
    volatile uint32_t *tx;
    volatile uint32_t *rx;
    volatile uint32_t *timer;
} wifi_pio_fifo_t;

/* Whole words, network byte order; 100000 elapsed ticks and bounded FIFO waits. */
int wifi_pio_write_words(const wifi_pio_fifo_t *fifo, const uint8_t *bytes,
                         uint32_t words, uint32_t started);
int wifi_pio_read_words(const wifi_pio_fifo_t *fifo, uint8_t *bytes,
                        uint32_t words, uint32_t started);
void wifi_pio_prepare_hw(volatile uint32_t *pio, volatile uint32_t *gpio,
                         uint32_t tx_length, uint32_t rx_length);
void wifi_pio_finish_hw(volatile uint32_t *pio, volatile uint32_t *sio,
                        volatile uint32_t *gpio);

#endif

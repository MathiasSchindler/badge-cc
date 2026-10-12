#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t ticks, reads, services;
static uint32_t pins[48], command_count;
static uint8_t commands[16];
static void test_gpio_put(uint32_t gpio, uint32_t value) {
    assert(gpio < 48u);
    pins[gpio] = value;
}
static void test_write_byte(uint8_t value) {
    if (pins[28] == 0u) {
        assert(command_count < 16u);
        commands[command_count++] = value;
    }
}
static uint32_t test_time_us(void) {
    reads++;
    return ticks++;
}
#define BADGER_DISPLAY_TIME_US() test_time_us()
#define BADGER_DISPLAY_GPIO_PUT(gpio, value) test_gpio_put(gpio, value)
#define BADGER_DISPLAY_WRITE_BYTE(value) test_write_byte(value)
#include "../../platform/badger/display.c"

static int service(void *context) {
    services++;
    return context == 0 || services < *(uint32_t *)context;
}
int main(void) {
    for (uint32_t wrapped = 0; wrapped < 2; wrapped++) {
        ticks = wrapped ? 0xfffffff0u : 0u;
        reads = services = 0u;
        assert(init_delay(0, 0));
        assert(reads == 150001u && services == 0u);
        ticks = wrapped ? 0xfffffff0u : 0u;
        reads = services = 0u;
        assert(init_delay(service, 0));
        assert(reads == 150001u && services == 149999u);
    }
    uint32_t stop = 7u;
    reads = services = ticks = 0u;
    assert(!init_delay(service, &stop));
    assert(services == 7u && reads == 8u);
    for (uint32_t wrapped = 0u; wrapped < 2u; ++wrapped) {
        ticks = wrapped ? 0xfffffff0u : 0u;
        reads = command_count = 0u;
        pins[26] = 1u;
        badger_display_sleep();
        assert(pins[26] == 0u && command_count == 2u);
        assert(commands[0] == 0x28u && commands[1] == 0x10u);
        assert(reads == 150001u);
        reads = 0u;
        badger_display_wake();
        assert(command_count == 3u && commands[2] == 0x11u);
        assert(pins[26] == 0u && reads == 150001u);
        badger_display_end();
        assert(command_count == 4u && commands[3] == 0x29u && pins[26] == 1u);
    }
    puts("display: initialization/sleep/wake timing, wraparound, cancellation and explicit backlight enable PASS");
}

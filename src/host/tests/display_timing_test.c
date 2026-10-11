#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t ticks, reads, services;
static uint32_t test_time_us(void) {
    reads++;
    return ticks++;
}
#define BADGER_DISPLAY_TIME_US() test_time_us()
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
    puts("display initialization: timer minimum, wraparound and service cancellation PASS");
}

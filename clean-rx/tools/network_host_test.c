#include <assert.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
static uint64_t clock_us=1000000u;
static int fake_clock(clockid_t id,struct timespec *value) {
    (void)id;value->tv_sec=(time_t)(clock_us/1000000u);
    value->tv_nsec=(long)(clock_us%1000000u)*1000;return 0;
}
static int fake_sleep(useconds_t us) {clock_us+=us;return 0;}
#define clock_gettime fake_clock
#define usleep fake_sleep
#include "network_host.h"
#undef clock_gettime
#undef usleep
int main(void) {
    clean_network_host_t h;
    assert(!setenv("CLEAN_RX_NETWORK_HOST_PAUSES","0",1));
    assert(clean_network_host_init(&h) && !h.enabled);
    assert(clean_network_host_pause(&h) && !h.count && clock_us==1000000u);
    assert(!setenv("CLEAN_RX_NETWORK_HOST_PAUSES","1",1));
    assert(clean_network_host_init(&h) && h.enabled);
    assert(clean_network_host_pause(&h) && h.count==1);
    assert(h.pauses[0].before==1000000u && h.pauses[0].after==3000000u);
    assert(!setenv("CLEAN_RX_NETWORK_HOST_PAUSES","invalid",1));
    assert(!clean_network_host_init(&h));
    assert(!unsetenv("CLEAN_RX_NETWORK_HOST_PAUSES"));
    assert(clean_network_host_init(&h) && h.enabled);
    h.count=1024;
    assert(!clean_network_host_pause(&h));
    puts("Host pauses: on/off, measured duration, invalid setting and capacity failure pass; no real sleep");
}

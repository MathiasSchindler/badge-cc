#ifndef CLEAN_NETWORK_HOST_H
#define CLEAN_NETWORK_HOST_H
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    uint64_t before, after;
} clean_network_pause_t;
typedef struct {
    uint32_t enabled, count;
    clean_network_pause_t pauses[1024];
} clean_network_host_t;

static inline int clean_network_host_init(clean_network_host_t *h) {
    memset(h,0,sizeof(*h));
    const char *value=getenv("CLEAN_RX_NETWORK_HOST_PAUSES");
    if (value && strcmp(value,"0") && strcmp(value,"1")) {
        fputs("CLEAN_RX_NETWORK_HOST_PAUSES must be 0 or 1\n",stderr); return 0;
    }
    h->enabled=!value || !strcmp(value,"1");
    return 1;
}
static inline int clean_network_host_now(uint64_t *value) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC,&now)) { perror("Network monotonic clock"); return 0; }
    *value=(uint64_t)now.tv_sec*1000000u+(uint64_t)now.tv_nsec/1000u;
    return 1;
}
static inline int clean_network_host_pause(clean_network_host_t *h) {
    if (!h->enabled) return 1;
    if (h->count==sizeof(h->pauses)/sizeof(h->pauses[0])) {
        fputs("Network pause evidence capacity exhausted\n",stderr); return 0;
    }
    clean_network_pause_t *p=&h->pauses[h->count];
    if (!clean_network_host_now(&p->before)) return 0;
    if (usleep(2000000u) && errno!=EINTR) { perror("Network host pause"); return 0; }
    if (!clean_network_host_now(&p->after)) return 0;
    ++h->count;
    return 1;
}
static inline void clean_network_host_print(const clean_network_host_t *h) {
    printf(",\"host_observation\":{\"pauses_enabled\":%s,\"pause_target_us\":2000000,\"pauses\":[",
           h->enabled?"true":"false");
    for (uint32_t i=0u;i<h->count;++i)
        printf("%s{\"begin_us\":%llu,\"end_us\":%llu}",i?",":"",
               (unsigned long long)h->pauses[i].before,(unsigned long long)h->pauses[i].after);
    fputs("]}",stdout);
}
#endif

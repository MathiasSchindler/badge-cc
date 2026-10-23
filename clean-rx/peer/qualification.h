#ifndef CLEAN_PEER_QUALIFICATION_H
#define CLEAN_PEER_QUALIFICATION_H
#include "../../src/radio/wifi_link.h"
#include "../../src/shared/bytes.h"
#include "../src/wpa2_crypto.h"

#define CLEAN_QUALIFICATION_AP_MODE 5u

typedef struct {
    wifi_link_ops_t backend;
    uint32_t channel;
    const uint8_t *pmk;
} clean_qualification_t;

static inline int clean_qualification_channel(uint32_t channel) {
    return channel == 1u || channel == 6u || channel == 11u;
}
static inline uint32_t clean_qualification_nmode(uint32_t channel) {
    return channel == 11u ? 1u : 0u;
}
static inline int clean_qualification_control(void *context, uint32_t command,
        uint8_t set, uint8_t interface, const uint8_t *request, uint32_t size,
        uint8_t *reply, uint32_t capacity, uint32_t *received) {
    clean_qualification_t *q = context;
    if (!q || !q->backend.control || !clean_qualification_channel(q->channel)) return 0;
    uint8_t adjusted[68];
    if (command == 268u && set && interface == 1u && q->pmk) {
        if (size != 68u) return 0;
        badger_bytes_zero(adjusted,sizeof(adjusted));
        adjusted[0] = 32u;
        badger_bytes_copy(adjusted + 4u,q->pmk,32u);
        int ok = q->backend.control(q->backend.context,command,set,interface,
            adjusted,sizeof(adjusted),reply,capacity,received);
        wpa_wipe(adjusted,sizeof(adjusted));
        return ok;
    }
    if (command == 2u && set && !interface) {
        static const uint8_t name[] = {'n','m','o','d','e',0u};
        badger_bytes_copy(adjusted,name,sizeof(name));
        badger_bytes_store_u32(adjusted + 6u, clean_qualification_nmode(q->channel));
        if (!q->backend.control(q->backend.context,263u,1u,0u,adjusted,10u,
                reply,capacity,received)) return 0;
    }
    if (command == 30u && set && !interface) {
        if (size != 4u) return 0;
        badger_bytes_store_u32(adjusted,q->channel);
        request = adjusted;
    }
    return q->backend.control(q->backend.context,command,set,interface,
        request,size,reply,capacity,received);
}
static inline int clean_qualification_verify(clean_qualification_t *q, uint8_t evidence[16]) {
    uint8_t request[32], reply[32];
    badger_bytes_zero(request,sizeof(request));
    badger_bytes_zero(reply,sizeof(reply));
    uint32_t size = 0u;
    if (!q->backend.control(q->backend.context,29u,0u,0u,request,12u,reply,32u,&size) ||
        size < 12u) return 0;
    badger_bytes_copy(evidence,reply,8u);
    static const char *const names[] = {"nmode","wsec"};
    for (uint32_t i = 0u; i < 2u; ++i) {
        badger_bytes_zero(request,sizeof(request));
        for (uint32_t j = 0u; names[i][j]; ++j) request[j] = (uint8_t)names[i][j];
        if (!q->backend.control(q->backend.context,262u,0u,(uint8_t)i,request,
                sizeof(request),reply,sizeof(reply),&size) || size < 4u) return 0;
        badger_bytes_copy(evidence + 8u + 4u*i,reply,4u);
    }
    return badger_bytes_load_u32(evidence) == q->channel &&
        badger_bytes_load_u32(evidence + 4u) == q->channel &&
        badger_bytes_load_u32(evidence + 8u) == clean_qualification_nmode(q->channel) &&
        badger_bytes_load_u32(evidence + 12u) == (q->pmk ? 4u : 0u);
}
#endif

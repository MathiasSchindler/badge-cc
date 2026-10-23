#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "qualification.h"

typedef struct {
    uint8_t mac[6];
    uint32_t channel, nmode, up, fail, bad, short_reply, calls, wsec;
} fake_t;
static int control(void *context, uint32_t cmd, uint8_t set, uint8_t iface,
        const uint8_t *request, uint32_t size, uint8_t *reply, uint32_t capacity,
        uint32_t *received) {
    fake_t *f = context;
    ++f->calls;
    assert(capacity>=12u);
    memset(reply,0,capacity);
    if (received) *received=0u;
    if (cmd==f->fail) return 0;
    if (cmd==3u && set) f->up=0u;
    if (cmd==2u && set) { assert(f->calls>1u); f->up=1u; }
    if (cmd==30u && set) { assert(size==4u); f->channel=badger_bytes_load_u32(request); }
    if (cmd==263u && !strcmp((const char *)request,"nmode")) {
        assert(!f->up && size==10u && !iface);
        if (f->fail==1u) return 0;
        f->nmode=badger_bytes_load_u32(request+6u);
    }
    if (cmd==263u && !strcmp((const char *)request,"cur_etheraddr"))
        memcpy(f->mac,request+14u,6u);
    if (cmd==29u) {
        badger_bytes_store_u32(reply,f->channel+f->bad);
        badger_bytes_store_u32(reply+4u,f->channel);
        if (received) *received=f->short_reply?4u:12u;
    }
    if (cmd==262u) {
        if (!strcmp((const char *)request,"cur_etheraddr")) {
            memcpy(reply,f->mac,6u); if (received) *received=6u;
        } else {
            badger_bytes_store_u32(reply,!strcmp((const char *)request,"nmode")?f->nmode:f->wsec);
            if (received) *received=4u;
        }
    }
    return 1;
}
int main(void) {
    uint8_t nonce[16]={1u},mac[6]={2u,1u,2u,3u,4u,5u},evidence[16];
    const uint32_t channels[]={1u,6u,11u};
    for (uint32_t i=0u;i<3u;++i) {
        fake_t fake={0};
        clean_qualification_t q={.backend={.context=&fake,.control=control},.channel=channels[i]};
        wifi_link_ops_t ops={.context=&q,.control=clean_qualification_control};
        wifi_link_t link;
        wifi_link_init(&link,&ops,WIFI_LINK_OPEN_AP,nonce,mac);
        assert(wifi_link_configure(&link));
        assert(fake.channel==channels[i] && fake.nmode==(i==2u));
        assert(clean_qualification_verify(&q,evidence));
        fake.wsec=4u; assert(!clean_qualification_verify(&q,evidence)); fake.wsec=0u;
        fake.nmode^=1u; assert(!clean_qualification_verify(&q,evidence)); fake.nmode^=1u;
        fake.bad=1u; assert(!clean_qualification_verify(&q,evidence));
        fake.bad=0u; fake.short_reply=1u; assert(!clean_qualification_verify(&q,evidence));
        fake.short_reply=0u; fake.fail=262u; assert(!clean_qualification_verify(&q,evidence));
        fake.fail=1u; fake.up=0u;
        wifi_link_init(&link,&ops,WIFI_LINK_OPEN_AP,nonce,mac);
        assert(!wifi_link_configure(&link) && !fake.up);
    }
    assert(!clean_qualification_channel(0u) && !clean_qualification_channel(12u));
    puts("B3 AP profiles: channel override, pre-UP nmode, readback/mismatch/short/failure guards pass");
}

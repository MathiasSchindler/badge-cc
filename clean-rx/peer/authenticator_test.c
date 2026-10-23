#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "authenticator.h"
#include "../badge/wpa2.h"

static const uint8_t rsn[22]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,12,0};
typedef struct {
    clean_auth_t auth;
    clean_wpa_t station;
    uint8_t pending[512], tk[16], gtk[4][16], pn[6], gtk_pn[4][6], last_response[512];
    uint32_t length, installs, authorizations, now, last_length;
    uint8_t installed, fail_send, fail_install, fail_authorize, expect_failure;
} fixture_t;
static fixture_t fixture;

static int send(void *context, const uint8_t *frame, uint32_t n) {
    fixture_t *f=context;
    if (f->fail_send) return 0;
    memcpy(f->pending,frame,n); f->length=n; return 1;
}
static int install(void *context, const uint8_t *key, uint8_t id, const uint8_t *peer) {
    fixture_t *f=context;
    if (f->fail_install) return 0;
    ++f->installs;
    if (peer) {
        assert(!id && !memcmp(peer,f->station.mac,6u));
        if (f->installed) assert(memcmp(key,f->tk,16u));
        memcpy(f->tk,key,16u); memset(f->pn,0,6u); f->installed=1u;
    } else {
        assert(id==1u || id==2u);
        memcpy(f->gtk[id],key,16u); memset(f->gtk_pn[id],0,6u);
    }
    return 1;
}
static int authorize(void *context, const uint8_t *peer) {
    fixture_t *f=context;
    assert(!memcmp(peer,f->station.mac,6u)); ++f->authorizations;
    return !f->fail_authorize;
}
static void init(void) {
    fixture_t *f=&fixture; memset(f,0,sizeof(*f));
    uint8_t secret[64], seed[32], ap[6]={2,1,2,3,4,5}, sta[6]={2,6,7,8,9,10};
    for (unsigned i=0u;i<64u;++i) secret[i]=(uint8_t)(i+1u);
    memset(seed,0xa5,sizeof(seed));
    clean_auth_ops_t ops={.context=f,.send=send,.install=install,.authorize=authorize};
    clean_auth_init(&f->auth,&ops,secret,seed,ap,rsn,sizeof(rsn));
    clean_wpa_init(&f->station,secret,sta,ap,rsn,sizeof(rsn));
    assert(!f->auth.error && !f->station.error);
}
static void increment(uint8_t pn[6]) {
    for (unsigned i=0u;i<6u;++i) if (++pn[i]) return;
    assert(0);
}
static uint32_t mpdu(uint8_t *m, const uint8_t *ethernet, uint32_t n, int group) {
    fixture_t *f=&fixture;
    const uint8_t snap[6]={170,170,3,0,0,0};
    uint8_t plain[512]; assert(n>=14u && n<490u);
    memset(m,0,1024u); m[0]=8u; m[1]=2u;
    memcpy(m+4u,ethernet,6u); memcpy(m+10u,ethernet+6u,6u);
    memcpy(m+16u,f->auth.mac,6u);
    memcpy(plain,snap,6u); memcpy(plain+6u,ethernet+12u,n-12u);
    uint32_t length=n-6u+24u;
    if (!f->installed) memcpy(m+24u,plain,n-6u);
    else {
        uint8_t id=group?f->auth.gtk_id:0u;
        uint8_t *pn=group?f->gtk_pn[id]:f->pn;
        increment(pn); m[1]|=64u;
        m[24]=pn[0]; m[25]=pn[1]; m[27]=(uint8_t)(32u|(id<<6u));
        memcpy(m+28u,pn+2u,4u);
        assert(wpa_ccmp(group?f->gtk[id]:f->tk,m,24u,pn,plain,n-6u,
            m+32u,m+32u+n-6u,0));
        length+=16u;
    }
    badger_bytes_store_u32(m+length,diag_crc32(m,length)); return length+4u;
}
static void exchange(int drop) {
    fixture_t *f=&fixture;
    uint8_t m[1024], tx[512], ethernet[512], plain[512];
    uint32_t n=mpdu(m,f->pending,f->length,0), tn=0u;
    assert(clean_wpa_poll(&f->station,f->now,tx,&tn)>=0);
    assert(clean_wpa_receive(&f->station,m,n,tx,&tn)==1 && tn);
    clean_wpa_tx_complete(&f->station);
    if (drop) return;
    const uint8_t *r=tx+6u; uint32_t h=r[0]==0x88u?26u:24u, size=tn-6u-h-4u;
    const uint8_t *body=r+h;
    if (r[1]&64u) {
        uint8_t pn[6]={body[0],body[1],body[4],body[5],body[6],body[7]}, tag[8];
        size-=16u;
        memcpy(tag,body+8u+size,8u);
        assert(wpa_ccmp(f->tk,r,h,pn,body+8u,size,plain,tag,1));
        assert(!memcmp(tag,body+8u+size,8u)); body=plain;
    }
    memcpy(ethernet,r+4u,6u); memcpy(ethernet+6u,r+10u,6u);
    memcpy(ethernet+12u,body+6u,size-6u);
    f->last_length=size+6u; memcpy(f->last_response,ethernet,f->last_length);
    assert(clean_auth_receive(&f->auth,ethernet,size+6u,f->now)==!f->expect_failure);
}
static void proof(int group, int multicast) {
    fixture_t *f=&fixture; uint8_t e[38]={0}, m[1024], response[512]; uint32_t n=0u;
    const uint8_t broadcast[6]={255,255,255,255,255,255};
    const uint8_t multi[6]={1,0,94,0,0,1};
    memcpy(e,group?(multicast?multi:broadcast):f->station.mac,6u);
    memcpy(e+6u,f->auth.mac,6u); e[12]=136u; e[13]=181u;
    memcpy(e+14u,"U26WPA2",8u); e[21]=1u; memcpy(e+22u,f->station.probe_token,16u);
    uint32_t size=mpdu(m,e,sizeof(e),group);
    assert(clean_wpa_receive(&f->station,m,size,response,&n)==0 && !n);
    assert(!f->station.error && f->station.stage==WPA_PROTECTED);
}
int main(void) {
    fixture_t *f=&fixture;
    init();
    assert(clean_auth_start(&f->auth,f->station.mac,0,f->now));
    exchange(0); exchange(0); proof(0,0); proof(1,0);
    assert(f->installs==2u && f->authorizations==1u);
    for (unsigned i=0u;i<3u;++i) {
        f->now+=4000000u;
        assert(clean_auth_start(&f->auth,f->station.mac,1,f->now));
        exchange(1);
        uint8_t pn[6]; memcpy(pn,f->station.group[f->auth.next_id].pn[16],6u);
        f->now+=CLEAN_AUTH_RETRY_US;
        assert(clean_auth_poll(&f->auth,f->now)); exchange(0);
        assert(!memcmp(pn,f->station.group[f->auth.gtk_id].pn[16],6u));
        proof(1,0); proof(1,1);
        f->now+=4000000u;
        assert(clean_auth_start(&f->auth,f->station.mac,0,f->now));
        exchange(0); exchange(1);
        f->now+=CLEAN_AUTH_RETRY_US;
        assert(clean_auth_poll(&f->auth,f->now)); exchange(0);
        proof(0,0); proof(1,0);
        assert(f->auth.group_rekeys==i+1u && f->auth.pairwise_rekeys==i+1u);
        assert(f->station.group_verified==i+1u && f->station.pairwise_verified==i+1u);
    }
    assert(f->auth.m2==4u && f->auth.m4==4u && f->auth.g2==3u);
    assert(f->authorizations==1u && f->installs==8u);
    init(); assert(clean_auth_start(&f->auth,f->station.mac,0,0u));
    clean_auth_t awaiting=f->auth;
    exchange(0);
    f->auth=awaiting; f->last_response[14u+81u]^=1u;
    assert(!clean_auth_receive(&f->auth,f->last_response,f->last_length,0u));
    assert(f->auth.error==CLEAN_AUTH_MIC && !f->auth.installed);
    for (unsigned i=0u;i<32u;++i) assert(!f->auth.pmk[i] && !f->auth.seed[i]);
    init(); assert(clean_auth_start(&f->auth,f->station.mac,0,0u));
    exchange(0);
    f->fail_install=f->expect_failure=1u; exchange(0);
    assert(f->auth.error==CLEAN_AUTH_BACKEND && !f->auth.installed);
    init(); assert(clean_auth_start(&f->auth,f->station.mac,0,0u));
    exchange(0);
    f->fail_authorize=f->expect_failure=1u; exchange(0);
    assert(f->auth.error==CLEAN_AUTH_BACKEND && !f->auth.installed);
    init(); f->fail_send=1u;
    assert(!clean_auth_start(&f->auth,f->station.mac,0,0u));
    assert(f->auth.error==CLEAN_AUTH_BACKEND && !f->auth.installed);
    init(); assert(clean_auth_start(&f->auth,f->station.mac,0,0xffff0000u));
    assert(!clean_auth_poll(&f->auth,0xffff0000u+CLEAN_AUTH_TIMEOUT_US));
    assert(f->auth.error==CLEAN_AUTH_TIMEOUT);
    clean_auth_clear(&f->auth);
    for (unsigned i=0u;i<sizeof(f->auth);++i) assert(!((uint8_t *)&f->auth)[i]);
    puts("Host authenticator: initial + three group/pairwise exchanges, encrypted rekeys, lost G2/M4 retries, per-epoch proof, install-once and bounded failure wiping pass (NOT RF)");
}

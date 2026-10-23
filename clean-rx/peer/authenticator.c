#include "authenticator.h"

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0]*256u+p[1]; }
static void put16(uint8_t *p, uint32_t n) { p[0]=(uint8_t)(n>>8u); p[1]=(uint8_t)n; }

void clean_auth_clear(clean_auth_t *a) { wpa_wipe(a,sizeof(*a)); }
static int fail(clean_auth_t *a, uint8_t error) {
    /* Preserve public progress and the error, never private key material. */
    wpa_wipe(a->pmk,sizeof(a->pmk)); wpa_wipe(a->seed,sizeof(a->seed));
    wpa_wipe(a->ptk,sizeof(a->ptk)); wpa_wipe(a->next_ptk,sizeof(a->next_ptk));
    wpa_wipe(a->gtk,sizeof(a->gtk)); wpa_wipe(a->next_gtk,sizeof(a->next_gtk));
    wpa_wipe(a->snonce,sizeof(a->snonce)); wpa_wipe(a->anonce,sizeof(a->anonce));
    wpa_wipe(a->message,sizeof(a->message)); a->message_length=0u;
    a->error=error; a->installed=0u; return 0;
}

void clean_auth_init(clean_auth_t *a, const clean_auth_ops_t *ops,
        const uint8_t pmk[32], const uint8_t seed[32], const uint8_t mac[6],
        const uint8_t *rsn, uint8_t length) {
    clean_auth_clear(a); a->ops=*ops;
    if (!ops->send || !ops->install || !ops->authorize ||
        length<22u || length>sizeof(a->rsn) || rsn[0]!=48u || rsn[1]!=length-2u) {
        (void)fail(a,CLEAN_AUTH_FORMAT); return;
    }
    badger_bytes_copy(a->pmk,pmk,32u); badger_bytes_copy(a->seed,seed,32u);
    badger_bytes_copy(a->mac,mac,6u);
    badger_bytes_copy(a->rsn,rsn,length); a->rsn_length=length;
}

static void fresh(clean_auth_t *a, uint8_t purpose, uint8_t *out, uint32_t n) {
    uint8_t input[6], digest[20];
    badger_bytes_store_u32(input,a->sequence); input[4]=purpose;
    for (uint32_t at=0u;at<n;at+=20u) {
        input[5]=(uint8_t)(at/20u);
        wpa_hmac(a->seed,32u,input,sizeof(input),digest);
        uint32_t count=n-at<20u?n-at:20u;
        badger_bytes_copy(out+at,digest,count);
    }
    wpa_wipe(digest,sizeof(digest));
}

static uint32_t wrap(const uint8_t key[16], const uint8_t *plain,
        uint32_t n, uint8_t *out) {
    wpa_aes_t aes; uint8_t block[16];
    wpa_aes_init(&aes,key);
    for (uint32_t i=0u;i<8u;++i) out[i]=0xa6u;
    badger_bytes_copy(out+8u,plain,n);
    uint32_t count=n/8u;
    for (uint32_t j=0u;j<6u;++j) for (uint32_t i=1u;i<=count;++i) {
        badger_bytes_copy(block,out,8u); badger_bytes_copy(block+8u,out+8u*i,8u);
        wpa_aes_encrypt(&aes,block,block);
        uint32_t t=count*j+i;
        for (uint32_t b=0u;b<4u;++b) block[7u-b]^=(uint8_t)(t>>(8u*b));
        badger_bytes_copy(out,block,8u); badger_bytes_copy(out+8u*i,block+8u,8u);
    }
    wpa_wipe(&aes,sizeof(aes)); wpa_wipe(block,sizeof(block)); return n+8u;
}

static int message(clean_auth_t *a, uint32_t info, uint32_t now) {
    uint8_t *e=a->message+14u, plain[160], mic[20];
    wpa_wipe(a->message,sizeof(a->message));
    badger_bytes_copy(a->message,a->peer,6u); badger_bytes_copy(a->message+6u,a->mac,6u);
    a->message[12]=136u; a->message[13]=142u;
    e[0]=2u; e[1]=3u; e[4]=2u; put16(e+5u,info); put16(e+7u,16u);
    for (uint32_t i=8u;i;--i) if (++a->replay[i-1u]) break;
    badger_bytes_copy(e+9u,a->replay,8u);
    badger_bytes_copy(e+17u,a->anonce,32u);
    uint32_t en=99u;
    if (info!=0x8au) {
        wpa_wipe(plain,sizeof(plain));
        uint32_t at=info==0x13cau?a->rsn_length:0u;
        if (at) badger_bytes_copy(plain,a->rsn,at);
        const uint8_t kde[8]={221u,22u,0u,15u,172u,1u,
            info==0x13cau?a->gtk_id:a->next_id,0u};
        badger_bytes_copy(plain+at,kde,8u);
        badger_bytes_copy(plain+at+8u,info==0x13cau?a->gtk:a->next_gtk,16u);
        at+=24u;
        if (at&7u) { plain[at]=221u; at=(at+7u)&~7u; }
        uint32_t n=wrap((info==0x13cau?a->next_ptk:a->ptk)+16u,plain,at,e+99u);
        put16(e+97u,n); en+=n;
    }
    put16(e+2u,en-4u);
    if (info!=0x8au) {
        wpa_hmac(info==0x13cau?a->next_ptk:a->ptk,16u,e,en,mic);
        badger_bytes_copy(e+81u,mic,16u);
    }
    wpa_wipe(plain,sizeof(plain)); wpa_wipe(mic,sizeof(mic));
    a->message_length=en+14u;
    if (info!=0x13cau) a->started=now;
    a->retry_at=now; a->retries=0u;
    if (!a->ops.send(a->ops.context,a->message,a->message_length))
        return fail(a,CLEAN_AUTH_BACKEND);
    return 1;
}

int clean_auth_start(clean_auth_t *a, const uint8_t peer[6], int group, uint32_t now) {
    if (a->error) return 0;
    if ((a->state!=CLEAN_AUTH_IDLE && a->state!=CLEAN_AUTH_READY) ||
        (group && !a->installed)) return fail(a,CLEAN_AUTH_FORMAT);
    if (a->installed && !wpa_equal(peer,a->peer,6u)) return fail(a,CLEAN_AUTH_FORMAT);
    badger_bytes_copy(a->peer,peer,6u);
    if (++a->sequence==0u) return fail(a,CLEAN_AUTH_NONCE);
    fresh(a,1u,a->anonce,32u);
    if (group) {
        a->next_id=(uint8_t)(a->gtk_id==1u?2u:1u);
        fresh(a,2u,a->next_gtk,16u); a->state=CLEAN_AUTH_G2;
        return message(a,0x1382u,now);
    }
    if (!a->installed) { a->gtk_id=1u; fresh(a,2u,a->gtk,16u); }
    a->state=CLEAN_AUTH_M2; return message(a,0x8au,now);
}

int clean_auth_receive(clean_auth_t *a, const uint8_t *frame, uint32_t n, uint32_t now) {
    if (a->error) return 0;
    if (n<14u || frame[12]!=136u || frame[13]!=142u) return 1;
    if (n<113u || !wpa_equal(frame,a->mac,6u) ||
        !wpa_equal(frame+6u,a->peer,6u)) return fail(a,CLEAN_AUTH_FORMAT);
    const uint8_t *e=frame+14u;
    uint32_t en=be16(e+2u)+4u, info=be16(e+5u);
    if (en<99u || en>363u || en!=n-14u || be16(e+97u)!=en-99u ||
        (e[0]!=1u && e[0]!=2u) || e[1]!=3u || e[4]!=2u)
        return fail(a,CLEAN_AUTH_FORMAT);
    /* A late duplicate response belongs to an already completed exchange. */
    if (!wpa_equal(e+9u,a->replay,8u)) return 1;
    int m2=a->state==CLEAN_AUTH_M2 && info==0x10au;
    int m4=a->state==CLEAN_AUTH_M4 && info==0x30au;
    int g2=a->state==CLEAN_AUTH_G2 && info==0x302u;
    if (!m2 && !m4 && !g2) return 1;
    uint8_t copy[363], mic[20], candidate[48];
    if (m2) {
        uint8_t bits=0u; for (uint32_t i=0u;i<32u;++i) bits|=e[17u+i];
        if (!bits || (a->installed && wpa_equal(e+17u,a->snonce,32u)))
            return fail(a,CLEAN_AUTH_NONCE);
        if (en!=121u || !wpa_equal(e+99u,a->rsn,20u))
            return fail(a,CLEAN_AUTH_FORMAT);
        wpa_ptk(a->pmk,a->mac,a->peer,a->anonce,e+17u,candidate);
    } else if (en!=99u) return fail(a,CLEAN_AUTH_FORMAT);
    badger_bytes_copy(copy,e,en); wpa_wipe(copy+81u,16u);
    wpa_hmac(m2?candidate:m4?a->next_ptk:a->ptk,16u,copy,en,mic);
    int valid=wpa_equal(mic,e+81u,16u);
    wpa_wipe(copy,sizeof(copy)); wpa_wipe(mic,sizeof(mic));
    if (!valid) { wpa_wipe(candidate,sizeof(candidate)); return fail(a,CLEAN_AUTH_MIC); }
    if (m2) {
        badger_bytes_copy(a->snonce,e+17u,32u);
        badger_bytes_copy(a->next_ptk,candidate,48u); wpa_wipe(candidate,sizeof(candidate));
        if (a->installed && wpa_equal(a->ptk+32u,a->next_ptk+32u,16u))
            return fail(a,CLEAN_AUTH_NONCE);
        ++a->m2; a->state=CLEAN_AUTH_M4; return message(a,0x13cau,now);
    }
    if (m4) {
        if ((!a->installed && !a->ops.install(a->ops.context,a->gtk,a->gtk_id,0)) ||
            !a->ops.install(a->ops.context,a->next_ptk+32u,0u,a->peer) ||
            (!a->installed && !a->ops.authorize(a->ops.context,a->peer)))
            return fail(a,CLEAN_AUTH_BACKEND);
        badger_bytes_copy(a->ptk,a->next_ptk,48u); wpa_wipe(a->next_ptk,48u);
        if (a->installed) ++a->pairwise_rekeys;
        a->installed=1u; ++a->m4;
    } else {
        if (!a->ops.install(a->ops.context,a->next_gtk,a->next_id,0))
            return fail(a,CLEAN_AUTH_BACKEND);
        badger_bytes_copy(a->gtk,a->next_gtk,16u); wpa_wipe(a->next_gtk,16u);
        a->gtk_id=a->next_id; ++a->g2; ++a->group_rekeys;
    }
    a->state=CLEAN_AUTH_READY; wpa_wipe(a->message,sizeof(a->message));
    a->message_length=0u; return 1;
}

int clean_auth_poll(clean_auth_t *a, uint32_t now) {
    if (a->error) return 0;
    if (a->state==CLEAN_AUTH_IDLE || a->state==CLEAN_AUTH_READY) return 1;
    if (now-a->started>=CLEAN_AUTH_TIMEOUT_US) return fail(a,CLEAN_AUTH_TIMEOUT);
    if (now-a->retry_at<CLEAN_AUTH_RETRY_US) return 1;
    if (a->retries++>=4u) return fail(a,CLEAN_AUTH_TIMEOUT);
    a->retry_at=now;
    if (!a->ops.send(a->ops.context,a->message,a->message_length))
        return fail(a,CLEAN_AUTH_BACKEND);
    return 1;
}

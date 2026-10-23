#include "wpa2.h"

static const uint8_t snap[8]={170u,170u,3u,0u,0u,0u,136u,142u};
static const uint8_t station_rsn[22]={
    48u,20u,1u,0u,0u,15u,172u,4u,1u,0u,0u,15u,172u,4u,1u,0u,0u,15u,172u,2u,0u,0u
};

static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0]<<8u)|p[1]; }
static int greater(const uint8_t *a, const uint8_t *b, uint32_t n) {
    for (uint32_t i=0u;i<n;++i) if (a[i]!=b[i]) return a[i]>b[i];
    return 0;
}

static int failure(clean_wpa_t *s, uint8_t error) {
    s->error=error;
    wpa_wipe(s->pmk,32u); wpa_wipe(s->ptk,48u); wpa_wipe(s->gtk,16u);
    wpa_wipe(s->rx_mac.frame,sizeof(s->rx_mac.frame)); s->rx_mac.active=0u;
    for (uint32_t i=0u;i<16u;++i) {
        wpa_wipe(s->rx_qos.tid[i].frame,sizeof(s->rx_qos.tid[i].frame));
        s->rx_qos.tid[i].active=0u;
    }
    wpa_wipe(s->rx_mpdu,sizeof(s->rx_mpdu)); wpa_wipe(s->fragment_pn,sizeof(s->fragment_pn));
    wpa_wipe(s->qos_fragment_pn,sizeof(s->qos_fragment_pn));
    wpa_wipe(s->snonce,sizeof(s->snonce)); wpa_wipe(s->anonce,sizeof(s->anonce));
    wpa_wipe(s->group,sizeof(s->group)); wpa_wipe(s->old_ptk,sizeof(s->old_ptk));
    wpa_wipe(s->old_rx_pn,sizeof(s->old_rx_pn)); wpa_wipe(s->old_tx_pn,sizeof(s->old_tx_pn));
    s->old_valid=s->reply_old=0u;
    wpa_wipe(s->nonce_seed,sizeof(s->nonce_seed)); wpa_wipe(s->m3_gtk,sizeof(s->m3_gtk));
    wpa_wipe(s->probe_token,sizeof(s->probe_token)); s->rekey=0u;
    s->installed=0u;
    return -1;
}

int clean_wpa_abort(clean_wpa_t *s, uint8_t error) {
    return failure(s,error?error:WPA_TRANSPORT);
}

static int nonzero(const uint8_t *p, uint32_t n) {
    uint8_t bits=0u;
    for (uint32_t i=0u;i<n;++i) bits|=p[i];
    return bits!=0u;
}

void clean_wpa_init(clean_wpa_t *s, const uint8_t secret[64], const uint8_t mac[6],
                     const uint8_t ap[6], const uint8_t *rsn, uint8_t rn) {
    wpa_wipe(s,sizeof(*s));
    badger_bytes_copy(s->pmk,secret,32u); badger_bytes_copy(s->snonce,secret+32u,32u);
    badger_bytes_copy(s->mac,mac,6u); badger_bytes_copy(s->bssid,ap,6u);
    if (rn>sizeof(s->rsn) || rn<20u || rsn[0]!=48u || rsn[1]!=rn-2u ||
        clean_station_rsn(rsn+2u,rn-2u)!=CLEAN_AP_PSK_CCMP) {
        (void)failure(s,WPA_RSN); return;
    }
    if (!nonzero(s->snonce,32u)) { (void)failure(s,WPA_NONCE); return; }
    badger_bytes_copy(s->rsn,rsn,rn); s->rsn_length=rn;
    badger_bytes_copy(s->probe_token,s->snonce,16u);
    wpa_hmac(s->pmk,32u,s->snonce,32u,s->nonce_seed);
}

static const uint8_t *data_ptk(const clean_wpa_t *s) {
    return s->rekey?s->old_ptk:s->ptk;
}

static int fresh_nonce(clean_wpa_t *s, const uint8_t anonce[32]) {
    if (++s->nonce_counter==0u) return failure(s,WPA_EXHAUSTED);
    uint8_t input[37], digest[20];
    badger_bytes_copy(input,anonce,32u);
    badger_bytes_store_u32(input+32u,s->nonce_counter);
    input[36]=0u;
    wpa_hmac(s->nonce_seed,20u,input,sizeof(input),digest);
    badger_bytes_copy(s->snonce,digest,20u);
    input[36]=1u;
    wpa_hmac(s->nonce_seed,20u,input,sizeof(input),digest);
    badger_bytes_copy(s->snonce+20u,digest,12u);
    wpa_hmac(s->nonce_seed,20u,s->snonce,32u,digest);
    badger_bytes_copy(s->nonce_seed,digest,20u);
    wpa_wipe(input,sizeof(input)); wpa_wipe(digest,sizeof(digest));
    return 1;
}

static int next_pn(uint8_t pn[6]);

static int pn_greater(const uint8_t a[6], const uint8_t b[6]) {
    for (uint32_t i=6u;i;--i) if (a[i-1u]!=b[i-1u]) return a[i-1u]>b[i-1u];
    return 0;
}

static uint32_t response(clean_wpa_t *s, uint8_t version, const uint8_t replay[8],
                          int m4, uint8_t frame[512]) {
    uint8_t *m=frame+6u, *e=m+32u;
    badger_bytes_zero(frame,512u);
    clean_station_header(m,8u,s->sequence++,s->mac,s->bssid);
    m[1]=1u; badger_bytes_copy(m+24u,snap,8u);
    e[0]=version; e[1]=3u;
    uint32_t n=m4?99u:121u;
    e[3]=(uint8_t)(n-4u); e[4]=2u; e[5]=m4?3u:1u; e[6]=10u;
    badger_bytes_copy(e+9u,replay,8u);
    if (!m4) {
        badger_bytes_copy(e+17u,s->snonce,32u); e[98]=22u;
        badger_bytes_copy(e+99u,station_rsn,22u);
    }
    uint8_t mic[20];
    wpa_hmac(s->ptk,16u,e,n,mic); badger_bytes_copy(e+81u,mic,16u); wpa_wipe(mic,sizeof(mic));
    s->reply_kind=m4?WPA_REPLY_M4:WPA_REPLY_M2;
    if (m4==2) {
        e[6]=2u; badger_bytes_zero(e+81u,16u);
        wpa_hmac(s->ptk,16u,e,n,mic); badger_bytes_copy(e+81u,mic,16u);
        wpa_wipe(mic,sizeof(mic)); s->reply_kind=WPA_REPLY_GROUP;
    }
    if (s->reply_protected) {
        uint8_t plain[144];
        uint8_t *pn=s->reply_old?s->old_tx_pn:s->tx_pn;
        const uint8_t *key=s->reply_old?s->old_ptk:data_ptk(s);
        badger_bytes_copy(plain,m+24u,n+8u);
        if (!next_pn(pn)) { (void)failure(s,WPA_EXHAUSTED); return 0u; }
        m[1]|=64u;
        m[24]=pn[0]; m[25]=pn[1]; m[26]=0u; m[27]=32u;
        badger_bytes_copy(m+28u,pn+2u,4u);
        int ok=wpa_ccmp(key+32u,m,24u,pn,plain,n+8u,m+32u,m+40u+n,0);
        wpa_wipe(plain,sizeof(plain));
        if (!ok) { (void)failure(s,WPA_FORMAT); return 0u; }
        badger_bytes_store_u32(m+48u+n,diag_crc32(m,48u+n));
        (void)clean_ofdm_plcp_rate(frame,52u+n,0u);
        return 58u+n;
    }
    badger_bytes_store_u32(m+32u+n,diag_crc32(m,32u+n));
    (void)clean_ofdm_plcp_rate(frame,36u+n,0u);
    return 42u+n;
}

void clean_wpa_tx_complete(clean_wpa_t *s) {
    if (s->error) return;
    if (s->reply_kind==WPA_REPLY_GROUP) ++s->group_responses;
    else if (s->reply_kind==WPA_REPLY_M2) ++s->m2;
    else {
        ++s->m4;
        if (s->stage==WPA_KEYS) s->stage=WPA_M4_SENT;
    }
}

static void group_expire(clean_wpa_t *s, uint32_t now) {
    for (uint32_t i=0u;i<4u;++i)
        if (s->group[i].valid && i!=s->gtk_id &&
            now-s->group[i].retired_at>=CLEAN_WPA_GTK_OVERLAP_US)
            wpa_wipe(&s->group[i],sizeof(s->group[i]));
}

static int install_group(clean_wpa_t *s, const uint8_t key[16], uint8_t id,
                          const uint8_t rsc[8]) {
    clean_wpa_gtk_t *g=&s->group[id];
    for (uint32_t i=0u;i<4u;++i)
        if (i!=id && s->group[i].valid && wpa_equal(s->group[i].key,key,16u))
            return failure(s,WPA_REPLAY);
    int same=g->valid && wpa_equal(g->key,key,16u);
    if (!same) {
        wpa_wipe(g,sizeof(*g)); g->valid=1u;
        badger_bytes_copy(g->key,key,16u);
        for (uint32_t i=0u;i<17u;++i) badger_bytes_copy(g->pn[i],rsc,6u);
    } else for (uint32_t i=0u;i<17u;++i)
        if (pn_greater(rsc,g->pn[i])) badger_bytes_copy(g->pn[i],rsc,6u);
    if (s->group[s->gtk_id].valid && s->gtk_id!=id)
        s->group[s->gtk_id].retired_at=s->clock;
    badger_bytes_copy(s->gtk,key,16u); s->gtk_id=id;
    return same?0:1;
}

int clean_wpa_poll(clean_wpa_t *s, uint32_t now, uint8_t frame[512], uint32_t *length) {
    *length=0u;
    if (s->error) return -1;
    s->clock=now;
    group_expire(s,now);
    if (!s->timer_started) {
        s->timer_started=1u; s->started=s->retry_at=now;
    }
    if (s->stage==WPA_PROTECTED) return 0;
    if (now-s->started>=CLEAN_WPA_DEADLINE_US) return failure(s,WPA_TIMEOUT);
    if (s->stage==WPA_WAIT_M1 || now-s->retry_at<CLEAN_WPA_RETRY_US) return 0;
    if (s->retries>=CLEAN_WPA_MAX_RETRIES) return failure(s,WPA_TIMEOUT);
    ++s->retries; ++s->retransmissions; s->retry_at=now;
    *length=response(s,s->eapol_version,s->replay,s->stage!=WPA_WAIT_M3,frame);
    return s->error?-1:1;
}

static int receive_key(clean_wpa_t *s, const uint8_t *m, uint32_t n,
                       uint8_t frame[512], uint32_t *length, int encrypted) {
    *length=0u;
    if (s->error) return -1;
    if (n<36u || !clean_frame_valid(m,n) || (m[0]!=8u && m[0]!=0x88u) ||
        (m[1]&0xc7u)!=2u || !wpa_equal(m+4u,s->mac,6u) ||
        !wpa_equal(m+10u,s->bssid,6u) || !wpa_equal(m+16u,s->bssid,6u)) return 0;
    uint32_t h=m[0]==0x88u?26u:24u;
    if (n<h+12u || !wpa_equal(m+h,snap,8u)) return 0;
    if ((m[22]&15u) || (h==26u && (m[24]&128u))) return failure(s,WPA_FORMAT);
    const uint8_t *e=m+h+8u;
    uint32_t available=n-h-12u;
    if (available<99u) return failure(s,WPA_FORMAT);
    uint32_t en=be16(e+2u)+4u;
    if (en<99u || en>363u || en>available || be16(e+97u)!=en-99u ||
        (e[0]!=1u && e[0]!=2u) || e[1]!=3u || e[4]!=2u || be16(e+7u)!=16u)
        return failure(s,WPA_FORMAT);
    uint32_t info=be16(e+5u);
    s->reply_protected=(uint8_t)(encrypted || s->rekey);
    s->reply_old=encrypted==2;
    if (info==0x8au) {
        if (!clean_station_eapol_m1(m,n,s->mac,s->bssid)) return failure(s,WPA_FORMAT);
        if (!nonzero(e+17u,32u)) return failure(s,WPA_NONCE);
        if (s->stage==WPA_PROTECTED) {
            if (!greater(e+9u,s->replay,8u)) return failure(s,WPA_REPLAY);
            if (wpa_equal(e+17u,s->anonce,32u)) return failure(s,WPA_NONCE);
            if (fresh_nonce(s,e+17u)<0) return -1;
            badger_bytes_copy(s->old_ptk,s->ptk,48u); s->rekey=1u;
            s->stage=WPA_WAIT_M1; s->started=s->retry_at=s->clock;
            s->timer_started=1u;
            s->reply_protected=1u;
        } else if (s->stage!=WPA_WAIT_M1) {
            if (s->stage!=WPA_WAIT_M3 ||
                (!wpa_equal(e+9u,s->replay,8u) && !greater(e+9u,s->replay,8u)))
                return failure(s,WPA_REPLAY);
            if (!wpa_equal(e+17u,s->anonce,32u)) return failure(s,WPA_NONCE);
        }
        if (s->stage==WPA_WAIT_M1) {
            badger_bytes_copy(s->replay,e+9u,8u); badger_bytes_copy(s->anonce,e+17u,32u);
            wpa_ptk(s->pmk,s->bssid,s->mac,s->anonce,s->snonce,s->ptk);
            if (s->rekey && wpa_equal(s->ptk+32u,s->old_ptk+32u,16u))
                return failure(s,WPA_NONCE);
            s->stage=WPA_WAIT_M3;
            s->retries=0u; s->retry_at=s->clock;
        }
        badger_bytes_copy(s->replay,e+9u,8u); s->eapol_version=e[0];
        ++s->m1; *length=response(s,e[0],e+9u,0,frame); return s->error?-1:1;
    }
    int group=info==0x1382u;
    if ((!group && info!=0x13cau) || s->stage==WPA_WAIT_M1 ||
        (group && (!s->installed || s->rekey))) return failure(s,WPA_FORMAT);
    if (!group && s->last_group && wpa_equal(e+9u,s->replay,8u))
        return failure(s,WPA_REPLAY);
    if (!greater(e+9u,s->replay,8u) &&
        !(s->installed && wpa_equal(e+9u,s->replay,8u))) return failure(s,WPA_REPLAY);
    if (!group && !wpa_equal(e+17u,s->anonce,32u)) return failure(s,WPA_NONCE);
    uint8_t copy[363], mic[20], plain[256];
    badger_bytes_copy(copy,e,en); badger_bytes_zero(copy+81u,16u);
    wpa_hmac(s->ptk,16u,copy,en,mic);
    wpa_wipe(copy,sizeof(copy));
    int valid=wpa_equal(mic,e+81u,16u); wpa_wipe(mic,sizeof(mic));
    if (!valid) return failure(s,WPA_MIC);
    uint32_t wrapped=en-99u;
    if (e[71] || e[72]) return failure(s,WPA_FORMAT);
    if (!wpa_unwrap(s->ptk+16u,e+99u,wrapped,plain)) return failure(s,WPA_UNWRAP);
    uint32_t size=wrapped-8u, rsn=0u, gtk=0u;
    uint8_t candidate[16], gtk_id=0u, error=WPA_OK;
    for (uint32_t at=0u;at<size;) {
        if (plain[at]==221u && (at+1u==size || plain[at+1u]==0u)) {
            for (uint32_t i=at+1u;i<size;++i) if (plain[i]) { error=WPA_FORMAT; break; }
            break;
        }
        if (size-at<2u || plain[at+1u]>size-at-2u) { error=WPA_FORMAT; break; }
        uint32_t len=plain[at+1u]+2u;
        if (plain[at]==48u) {
            if (rsn++ || len!=s->rsn_length || !wpa_equal(plain+at,s->rsn,len)) {
                error=WPA_RSN; break;
            }
        } else if (plain[at]==221u && len>=6u && plain[at+2u]==0u &&
                   plain[at+3u]==15u && plain[at+4u]==172u && plain[at+5u]==1u) {
            s->gtk_info=len | (len>=8u?((uint32_t)plain[at+6u]<<8u)|((uint32_t)plain[at+7u]<<16u):0u);
            if (gtk++ || len!=24u) {
                error=WPA_GTK; break;
            }
            gtk_id=plain[at+6u]&3u; badger_bytes_copy(candidate,plain+at+8u,16u);
        }
        at+=len;
    }
    wpa_wipe(plain,sizeof(plain));
    if (error || (!group && !rsn) || !gtk) {
        wpa_wipe(candidate,sizeof(candidate));
        return failure(s,error?error:!rsn?WPA_RSN:WPA_GTK);
    }
    if (group) {
        int equal=wpa_equal(s->replay,e+9u,8u);
        int same=s->gtk_id==gtk_id && wpa_equal(s->gtk,candidate,16u);
        if (equal && (!s->last_group || !same ||
            !wpa_equal(s->group_nonce,e+17u,32u) || !wpa_equal(s->group_rsc,e+65u,8u))) {
            wpa_wipe(candidate,sizeof(candidate)); return failure(s,WPA_REPLAY);
        }
        if (same && s->last_group) {
            if (s->clock-s->group_started>=CLEAN_WPA_DEADLINE_US ||
                ++s->group_duplicates>CLEAN_WPA_MAX_RETRIES) {
                wpa_wipe(candidate,sizeof(candidate)); return failure(s,WPA_TIMEOUT);
            }
        } else { s->group_started=s->clock; s->group_duplicates=0u; }
        int changed=install_group(s,candidate,gtk_id,e+65u);
        wpa_wipe(candidate,sizeof(candidate));
        if (changed<0) return -1;
        if (changed) {
            ++s->group_rekeys; s->group[gtk_id].needs_proof=1u;
        }
        badger_bytes_copy(s->group_nonce,e+17u,32u);
        badger_bytes_copy(s->group_rsc,e+65u,8u);
        badger_bytes_copy(s->replay,e+9u,8u); s->last_group=1u;
        s->eapol_version=e[0]; s->reply_protected=1u;
        ++s->group_messages; *length=response(s,e[0],e+9u,2,frame);
        return s->error?-1:1;
    }
    if (s->installed && !s->rekey) {
        int unchanged=s->m3_gtk_id==gtk_id && wpa_equal(s->m3_gtk,candidate,16u);
        wpa_wipe(candidate,sizeof(candidate));
        if (!unchanged) return failure(s,WPA_REPLAY);
        badger_bytes_copy(s->replay,e+9u,8u); s->eapol_version=e[0];
        s->last_group=0u;
        ++s->m3; *length=response(s,e[0],e+9u,1,frame); return s->error?-1:1;
    }
    *length=response(s,e[0],e+9u,1,frame);
    if (s->error) { wpa_wipe(candidate,sizeof(candidate)); return -1; }
    if (install_group(s,candidate,gtk_id,e+65u)<0) {
        wpa_wipe(candidate,sizeof(candidate)); *length=0u; return -1;
    }
    badger_bytes_copy(s->m3_gtk,candidate,16u); s->m3_gtk_id=gtk_id;
    wpa_wipe(candidate,sizeof(candidate));
    if (s->rekey) {
        ++s->pairwise_rekeys;
        s->pairwise_needs_proof=1u;
        badger_bytes_copy(s->old_tx_pn,s->tx_pn,sizeof(s->tx_pn));
        for (uint32_t i=0u;i<17u;++i) badger_bytes_copy(s->old_rx_pn[i],s->rx_pn[i],6u);
        s->old_valid=1u; s->reply_old=1u;
        wpa_wipe(s->tx_pn,sizeof(s->tx_pn)); wpa_wipe(s->rx_pn,sizeof(s->rx_pn));
        wpa_wipe(&s->rx_mac,sizeof(s->rx_mac)); wpa_wipe(&s->rx_qos,sizeof(s->rx_qos));
        wpa_wipe(s->fragment_pn,sizeof(s->fragment_pn));
        wpa_wipe(s->qos_fragment_pn,sizeof(s->qos_fragment_pn));
        s->rekey=0u;
    }
    badger_bytes_copy(s->replay,e+9u,8u);
    s->installed=1u; s->stage=WPA_KEYS; ++s->m3;
    s->retries=0u; s->retry_at=s->clock; s->eapol_version=e[0];
    s->last_group=0u;
    return 1;
}

static int next_pn(uint8_t pn[6]) {
    for (uint32_t i=0u;i<6u;++i) if (++pn[i]) return 1;
    return 0;
}

uint32_t clean_wpa_probe(clean_wpa_t *s, uint8_t frame[512]) {
    if (!s->installed || s->error) return 0u;
    if (!next_pn(s->tx_pn)) { (void)failure(s,WPA_EXHAUSTED); return 0u; }
    badger_bytes_zero(frame,512u);
    static const uint8_t prefix[16]={170u,170u,3u,0u,0u,0u,136u,181u,'U','2','6','W','P','A','2',0u};
    uint8_t *m=frame+6u, payload[32];
    uint32_t h=s->tx_qos?26u:24u;
    badger_bytes_copy(payload,prefix,16u);
    clean_station_header(m,s->tx_qos?0x88u:8u,s->sequence++,s->mac,s->bssid); m[1]=0x41u;
    m[h]=s->tx_pn[0]; m[h+1u]=s->tx_pn[1]; m[h+3u]=32u;
    badger_bytes_copy(m+h+4u,s->tx_pn+2u,4u);
    badger_bytes_copy(payload+16u,s->probe_token,16u);
    if (!wpa_ccmp(data_ptk(s)+32u,m,h,s->tx_pn,payload,32u,m+h+8u,m+h+40u,0)) return 0u;
    badger_bytes_store_u32(m+h+48u,diag_crc32(m,h+48u));
    (void)clean_ofdm_plcp_rate(frame,h+52u,0u);
    return h+58u;
}

void clean_wpa_rx_expire(clean_wpa_t *s, uint32_t now) {
    clean_mac_rx_expire(&s->rx_mac,now);
    if (!s->rx_mac.active) wpa_wipe(s->fragment_pn,sizeof(s->fragment_pn));
    clean_qos_rx_expire(&s->rx_qos,now);
    for (uint32_t i=0u;i<16u;++i)
        if (!s->rx_qos.tid[i].active)
            wpa_wipe(s->qos_fragment_pn[i],sizeof(s->qos_fragment_pn[i]));
}

static int protected_reply(const clean_wpa_t *s, const volatile uint8_t *plain,
                            uint32_t payload) {
    static const uint8_t reply[16]={170u,170u,3u,0u,0u,0u,136u,181u,'U','2','6','W','P','A','2',1u};
    if (payload<32u) return 0;
    uint8_t different=0u;
    for (uint32_t i=0u;i<32u;++i)
        different|=plain[i]^(i<16u?reply[i]:s->probe_token[i-16u]);
    return !different;
}

static int protected_receive(clean_wpa_t *s, const uint8_t *m, uint32_t n,
                              uint32_t now, int block_ack, uint8_t *response_frame,
                              uint32_t *response_length) {
    clean_wpa_rx_expire(s,now);
    group_expire(s,now);
    if (!s->installed || s->error || n<44u || !clean_frame_valid(m,n) ||
        (m[0]!=8u && m[0]!=0x88u) || (m[1]&0xc3u)!=0x42u ||
        (!(m[4]&1u) && !wpa_equal(m+4u,s->mac,6u)) ||
        !wpa_equal(m+10u,s->bssid,6u)) return 0;
    uint32_t h=m[0]==0x88u?26u:24u;
    int group=(m[4]&1u)!=0u;
    int aggregate=h==26u && (m[24]&128u);
    if (n<h+20u || (h==26u && (m[24]&0x60u)>=0x40u &&
                   !(block_ack && (m[24]&0x60u)==0x60u)) ||
        (aggregate && (!s->rx_amsdu || (m[1]&4u) || (m[22]&15u))))
        return failure(s,WPA_FORMAT);
    const uint8_t *c=m+h;
    if (c[2] || (c[3]&63u)!=32u || (!group && c[3]!=32u))
        return failure(s,WPA_FORMAT);
    uint8_t id=c[3]>>6u;
    if (group && (!s->group[id].valid || block_ack || aggregate ||
                  (m[1]&4u) || (m[22]&15u))) {
        ++s->replay_drops; return 0;
    }
    uint8_t pn[6]={c[0],c[1],c[4],c[5],c[6],c[7]}, big[6], old[6], tag[8];
    uint32_t tid=h==26u?(m[24]&15u):16u;
    uint8_t *last_pn=group?s->group[id].pn[tid]:s->rx_pn[tid];
    clean_mac_rx_t *assembly=tid==16u?&s->rx_mac:&s->rx_qos.tid[tid];
    uint8_t *fragment_pn=tid==16u?s->fragment_pn:s->qos_fragment_pn[tid];
    for (uint32_t i=0u;i<6u;++i) { big[i]=pn[5u-i]; old[i]=last_pn[5u-i]; }
    if (!greater(big,old,6u)) { ++s->replay_drops; return 0; }
    uint32_t payload=n-h-20u;
    if (payload>(aggregate?CLEAN_AMSDU_BYTES:CLEAN_MSDU_BYTES))
        return failure(s,WPA_FORMAT);
    uint8_t *plain=s->rx_mpdu+h;
    badger_bytes_copy(s->rx_mpdu,m,h);
    badger_bytes_copy(tag,m+n-12u,8u);
    const uint8_t *key=group?s->group[id].key:data_ptk(s)+32u;
    int encrypted=1;
    if (!wpa_ccmp(key,m,h,pn,c+8u,payload,plain,tag,1)) {
        if (group || !s->old_valid || !response_frame || aggregate ||
            (m[1]&4u) || (m[22]&15u)) return failure(s,WPA_MIC);
        if (!pn_greater(pn,s->old_rx_pn[tid])) { ++s->replay_drops; return 0; }
        badger_bytes_copy(tag,m+n-12u,8u);
        if (!wpa_ccmp(s->old_ptk+32u,m,h,pn,c+8u,payload,plain,tag,1))
            return failure(s,WPA_MIC);
        if (payload<107u || !wpa_equal(plain,snap,8u) || be16(plain+13u)!=0x13cau) {
            wpa_wipe(s->rx_mpdu,sizeof(s->rx_mpdu)); return 0;
        }
        encrypted=2; last_pn=s->old_rx_pn[tid];
    }
    if (group) {
        badger_bytes_copy(last_pn,pn,6u);
        ++s->group_rx;
        static const uint8_t broadcast[6]={255u,255u,255u,255u,255u,255u};
        if (wpa_equal(m+4u,broadcast,6u)) ++s->broadcast_rx;
        else ++s->multicast_rx;
        int matched=protected_reply(s,plain,payload);
        if (matched && s->group[id].needs_proof) {
            s->group[id].needs_proof=0u; ++s->group_verified;
        }
        wpa_wipe(s->rx_mpdu,sizeof(s->rx_mpdu));
        return matched;
    }
    if (!aggregate && !(m[1]&4u) && !(m[22]&15u) && payload>=8u &&
        wpa_equal(plain,snap,8u) && response_frame) {
        badger_bytes_copy(last_pn,pn,6u);
        s->rx_mpdu[1]&=(uint8_t)~64u;
        badger_bytes_store_u32(s->rx_mpdu+h+payload,diag_crc32(s->rx_mpdu,h+payload));
        int result=receive_key(s,s->rx_mpdu,h+payload+4u,response_frame,response_length,encrypted);
        wpa_wipe(s->rx_mpdu,sizeof(s->rx_mpdu));
        return result;
    }
    uint32_t subframes=0u;
    int matched=0;
    if (aggregate) {
        clean_amsdu_t body;
        clean_amsdu_subframe_t subframe;
        if (!clean_amsdu_open(&body,plain,payload)) return failure(s,WPA_FORMAT);
        while (clean_amsdu_next(&body,&subframe)) {
            for (uint32_t i=0u;i<6u;++i)
                if (subframe.destination[i]!=s->mac[i]) return failure(s,WPA_FORMAT);
            ++subframes;
            matched|=protected_reply(s,subframe.payload,subframe.length);
        }
    }
    badger_bytes_copy(s->rx_pn[tid],pn,6u);
    if (m[22]&15u) {
        uint8_t expected[6];
        badger_bytes_copy(expected,fragment_pn,6u);
        if (!next_pn(expected) || !wpa_equal(expected,pn,6u))
            return failure(s,WPA_FRAGMENT);
    }
    badger_bytes_store_u32(s->rx_mpdu+h+payload,diag_crc32(s->rx_mpdu,h+payload));
    int result=block_ack?
        clean_qos_rx_ba_authenticated(&s->rx_qos,s->rx_mpdu,h+payload+4u,now):
        tid==16u?
        clean_mac_rx_authenticated(&s->rx_mac,s->rx_mpdu,h+payload+4u,now):
        clean_qos_rx_authenticated(&s->rx_qos,s->rx_mpdu,h+payload+4u,now);
    if (result<0) return failure(s,WPA_FRAGMENT);
    if (result==CLEAN_MAC_FRAGMENT) badger_bytes_copy(fragment_pn,pn,6u);
    if (result!=CLEAN_MAC_DELIVER) {
        wpa_wipe(s->rx_mpdu,sizeof(s->rx_mpdu));
        return 0;
    }
    if (m[22]&15u) {
        plain=assembly->frame+h;
        payload=assembly->length-h-4u;
    }
    if (aggregate) {
        ++s->amsdu_rx;
        s->amsdu_subframes+=subframes;
    } else matched=protected_reply(s,plain,payload);
    wpa_wipe(s->rx_mpdu,sizeof(s->rx_mpdu));
    if (!assembly->active) {
        wpa_wipe(assembly->frame,sizeof(assembly->frame));
        wpa_wipe(fragment_pn,6u);
    }
    if (matched) {
        ++s->protected_rx;
        if (!s->rekey) {
            s->stage=WPA_PROTECTED;
            wpa_wipe(s->old_ptk,sizeof(s->old_ptk));
            wpa_wipe(s->old_rx_pn,sizeof(s->old_rx_pn));
            wpa_wipe(s->old_tx_pn,sizeof(s->old_tx_pn));
            s->old_valid=s->reply_old=0u;
            if (s->pairwise_needs_proof) {
                s->pairwise_needs_proof=0u; ++s->pairwise_verified;
            }
        }
    }
    return matched;
}

int clean_wpa_protected(clean_wpa_t *s, const uint8_t *m, uint32_t n, uint32_t now) {
    return protected_receive(s,m,n,now,0,0,0);
}

int clean_wpa_receive(clean_wpa_t *s, const uint8_t *m, uint32_t n,
                       uint8_t frame[512], uint32_t *length) {
    *length=0u;
    if (s->error) return -1;
    if (n>=2u && (m[1]&64u))
        return protected_receive(s,m,n,s->clock,0,frame,length)<0?-1:(*length?1:0);
    int result=receive_key(s,m,n,frame,length,0);
    if (!result && s->installed && n>=36u && clean_frame_valid(m,n) &&
        (m[0]==8u || m[0]==0x88u) && (m[1]&3u)==2u &&
        wpa_equal(m+10u,s->bssid,6u)) ++s->plaintext_drops;
    return result;
}

static int ba_protected_sink(void *context, const uint8_t *mpdu, uint32_t length,
                              uint32_t now) {
    clean_wpa_t *s = context;
    if (!s->installed || s->error) return -1;
    int result = protected_receive(s,mpdu,length,now,1,0,0);
    return result < 0 ? -1 : 1;
}

static int ba_identity(const clean_wpa_t *s, const clean_ba_rx_t *ba) {
    return s->installed && !s->error && wpa_equal(s->mac,ba->own,6u) &&
           wpa_equal(s->bssid,ba->peer,6u);
}

int clean_wpa_ba_receive(clean_wpa_t *s, clean_ba_rx_t *ba, const uint8_t *mpdu,
                         uint32_t length, uint32_t now) {
    if (!ba_identity(s,ba) || !mpdu || length < 46u || !(mpdu[1]&64u))
        return CLEAN_BA_INVALID;
    return clean_ba_receive(ba,mpdu,length,now,ba_protected_sink,s);
}

int clean_wpa_ba_poll(clean_wpa_t *s, clean_ba_rx_t *ba, uint32_t now) {
    if (!ba_identity(s,ba)) return CLEAN_BA_INVALID;
    return clean_ba_poll(ba,now,ba_protected_sink,s);
}

int clean_wpa_ba_bar(clean_wpa_t *s, clean_ba_rx_t *ba, const uint8_t *mpdu,
                     uint32_t length, uint32_t now) {
    if (!ba_identity(s,ba)) return CLEAN_BA_INVALID;
    return clean_ba_bar(ba,mpdu,length,now,ba_protected_sink,s);
}

typedef struct {
    clean_wpa_t *wpa;
    clean_ba_rx_t *ba;
} wpa_ampdu_context_t;

static int ampdu_protected_sink(void *context, const uint8_t *mpdu,
                                uint32_t length, uint32_t now) {
    wpa_ampdu_context_t *c = context;
    int result = clean_wpa_ba_receive(c->wpa,c->ba,mpdu,length,now);
    if (result == CLEAN_BA_BUSY) return 0;
    return result == CLEAN_BA_ACCEPTED || result == CLEAN_BA_DUPLICATE ||
           result == CLEAN_BA_OLD ? 1 : -1;
}

int clean_wpa_ampdu_feed(clean_wpa_t *s, clean_ba_rx_t *ba, clean_ampdu_stream_t *stream,
                         const uint8_t *bytes, uint32_t length, uint32_t *used,
                         uint32_t now) {
    if (!used) return CLEAN_BA_INVALID;
    *used = 0u;
    if (!ba_identity(s,ba)) return CLEAN_BA_INVALID;
    wpa_ampdu_context_t context = {.wpa=s,.ba=ba};
    return clean_ampdu_feed(stream,bytes,length,used,ampdu_protected_sink,&context,now);
}

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
    badger_bytes_store_u32(m+32u+n,diag_crc32(m,32u+n));
    (void)clean_ofdm_plcp_rate(frame,36u+n,0u);
    return 42u+n;
}

int clean_wpa_poll(clean_wpa_t *s, uint32_t now, uint8_t frame[512], uint32_t *length) {
    *length=0u;
    if (s->error) return -1;
    s->clock=now;
    if (!s->timer_started) {
        s->timer_started=1u; s->started=s->retry_at=now;
    }
    if (s->stage==WPA_PROTECTED) return 0;
    if (now-s->started>=CLEAN_WPA_DEADLINE_US) return failure(s,WPA_TIMEOUT);
    if (s->stage==WPA_WAIT_M1 || now-s->retry_at<CLEAN_WPA_RETRY_US) return 0;
    if (s->retries>=CLEAN_WPA_MAX_RETRIES) return failure(s,WPA_TIMEOUT);
    ++s->retries; ++s->retransmissions; s->retry_at=now;
    *length=response(s,s->eapol_version,s->replay,s->installed,frame);
    return 1;
}

int clean_wpa_receive(clean_wpa_t *s, const uint8_t *m, uint32_t n,
                       uint8_t frame[512], uint32_t *length) {
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
    if (info==0x8au) {
        if (!clean_station_eapol_m1(m,n,s->mac,s->bssid)) return failure(s,WPA_FORMAT);
        if (!nonzero(e+17u,32u)) return failure(s,WPA_NONCE);
        if (s->stage!=WPA_WAIT_M1) {
            if (s->stage!=WPA_WAIT_M3 ||
                (!wpa_equal(e+9u,s->replay,8u) && !greater(e+9u,s->replay,8u)))
                return failure(s,WPA_REPLAY);
            if (!wpa_equal(e+17u,s->anonce,32u)) return failure(s,WPA_NONCE);
        }
        if (s->stage==WPA_WAIT_M1) {
            badger_bytes_copy(s->replay,e+9u,8u); badger_bytes_copy(s->anonce,e+17u,32u);
            wpa_ptk(s->pmk,s->bssid,s->mac,s->anonce,s->snonce,s->ptk);
            s->stage=WPA_WAIT_M3;
            s->retries=0u; s->retry_at=s->clock;
        }
        badger_bytes_copy(s->replay,e+9u,8u); s->eapol_version=e[0];
        ++s->m1; *length=response(s,e[0],e+9u,0,frame); return 1;
    }
    if (info!=0x13cau || s->stage==WPA_WAIT_M1) return failure(s,WPA_FORMAT);
    if (!greater(e+9u,s->replay,8u) &&
        !(s->installed && wpa_equal(e+9u,s->replay,8u))) return failure(s,WPA_REPLAY);
    if (!wpa_equal(e+17u,s->anonce,32u)) return failure(s,WPA_NONCE);
    uint8_t copy[363], mic[20], plain[256];
    badger_bytes_copy(copy,e,en); badger_bytes_zero(copy+81u,16u);
    wpa_hmac(s->ptk,16u,copy,en,mic);
    wpa_wipe(copy,sizeof(copy));
    int valid=wpa_equal(mic,e+81u,16u); wpa_wipe(mic,sizeof(mic));
    if (!valid) return failure(s,WPA_MIC);
    uint32_t wrapped=en-99u;
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
    if (error || !rsn || !gtk) {
        wpa_wipe(candidate,sizeof(candidate));
        return failure(s,error?error:!rsn?WPA_RSN:WPA_GTK);
    }
    if (s->installed) {
        int unchanged=s->gtk_id==gtk_id && wpa_equal(s->gtk,candidate,16u);
        wpa_wipe(candidate,sizeof(candidate));
        if (!unchanged) return failure(s,WPA_REPLAY);
        badger_bytes_copy(s->replay,e+9u,8u); s->eapol_version=e[0];
        ++s->m3; *length=response(s,e[0],e+9u,1,frame); return 1;
    }
    badger_bytes_copy(s->gtk,candidate,16u); wpa_wipe(candidate,sizeof(candidate)); s->gtk_id=gtk_id;
    badger_bytes_copy(s->replay,e+9u,8u);
    s->installed=1u; s->stage=WPA_KEYS; ++s->m3;
    s->retries=0u; s->retry_at=s->clock; s->eapol_version=e[0];
    wpa_wipe(s->pmk,sizeof(s->pmk));
    *length=response(s,e[0],e+9u,1,frame);
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
    badger_bytes_copy(payload+16u,s->snonce,16u);
    if (!wpa_ccmp(s->ptk+32u,m,h,s->tx_pn,payload,32u,m+h+8u,m+h+40u,0)) return 0u;
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
        different|=plain[i]^(i<16u?reply[i]:s->snonce[i-16u]);
    return !different;
}

static int protected_receive(clean_wpa_t *s, const uint8_t *m, uint32_t n,
                              uint32_t now, int block_ack) {
    clean_wpa_rx_expire(s,now);
    if (!s->installed || s->error || n<44u || !clean_frame_valid(m,n) ||
        (m[0]!=8u && m[0]!=0x88u) || (m[1]&0xc3u)!=0x42u ||
        !wpa_equal(m+4u,s->mac,6u) || !wpa_equal(m+10u,s->bssid,6u)) return 0;
    uint32_t h=m[0]==0x88u?26u:24u;
    int aggregate=h==26u && (m[24]&128u);
    if (n<h+20u || (h==26u && (m[24]&0x60u)>=0x40u &&
                   !(block_ack && (m[24]&0x60u)==0x60u)) ||
        (aggregate && (!s->rx_amsdu || (m[1]&4u) || (m[22]&15u))))
        return failure(s,WPA_FORMAT);
    const uint8_t *c=m+h;
    if (c[2] || c[3]!=32u) return failure(s,WPA_FORMAT);
    uint8_t pn[6]={c[0],c[1],c[4],c[5],c[6],c[7]}, big[6], old[6], tag[8];
    uint32_t tid=h==26u?(m[24]&15u):16u;
    clean_mac_rx_t *assembly=tid==16u?&s->rx_mac:&s->rx_qos.tid[tid];
    uint8_t *fragment_pn=tid==16u?s->fragment_pn:s->qos_fragment_pn[tid];
    for (uint32_t i=0u;i<6u;++i) { big[i]=pn[5u-i]; old[i]=s->rx_pn[tid][5u-i]; }
    if (!greater(big,old,6u)) return 0; /* A valid MAC retry must not reset replay state. */
    uint32_t payload=n-h-20u;
    if (payload>(aggregate?CLEAN_AMSDU_BYTES:CLEAN_MSDU_BYTES))
        return failure(s,WPA_FORMAT);
    uint8_t *plain=s->rx_mpdu+h;
    badger_bytes_copy(s->rx_mpdu,m,h);
    badger_bytes_copy(tag,m+n-12u,8u);
    if (!wpa_ccmp(s->ptk+32u,m,h,pn,c+8u,payload,plain,tag,1)) return failure(s,WPA_MIC);
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
    if (matched) { ++s->protected_rx; s->stage=WPA_PROTECTED; }
    return matched;
}

int clean_wpa_protected(clean_wpa_t *s, const uint8_t *m, uint32_t n, uint32_t now) {
    return protected_receive(s,m,n,now,0);
}

static int ba_protected_sink(void *context, const uint8_t *mpdu, uint32_t length,
                              uint32_t now) {
    clean_wpa_t *s = context;
    if (!s->installed || s->error) return -1;
    int result = protected_receive(s,mpdu,length,now,1);
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

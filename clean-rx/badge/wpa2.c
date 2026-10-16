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
    s->installed=0u;
    return -1;
}

void clean_wpa_init(clean_wpa_t *s, const uint8_t secret[64], const uint8_t mac[6],
                     const uint8_t ap[6], const uint8_t *rsn, uint8_t rn) {
    wpa_wipe(s,sizeof(*s));
    badger_bytes_copy(s->pmk,secret,32u); badger_bytes_copy(s->snonce,secret+32u,32u);
    badger_bytes_copy(s->mac,mac,6u); badger_bytes_copy(s->bssid,ap,6u);
    if (rn>sizeof(s->rsn) || rn<20u) { (void)failure(s,WPA_RSN); return; }
    badger_bytes_copy(s->rsn,rsn,rn); s->rsn_length=rn;
}

static uint32_t response(clean_wpa_t *s, const uint8_t *incoming, int m4, uint8_t frame[512]) {
    uint8_t *m=frame+6u, *e=m+32u;
    badger_bytes_zero(frame,512u);
    clean_station_header(m,8u,s->sequence++,s->mac,s->bssid);
    m[1]=1u; badger_bytes_copy(m+24u,snap,8u);
    e[0]=incoming[0]; e[1]=3u;
    uint32_t n=m4?99u:121u;
    e[3]=(uint8_t)(n-4u); e[4]=2u; e[5]=m4?3u:1u; e[6]=10u;
    badger_bytes_copy(e+9u,incoming+9u,8u);
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
        if (s->stage!=WPA_WAIT_M1 && (s->stage!=WPA_WAIT_M3 ||
            !wpa_equal(e+9u,s->replay,8u) || !wpa_equal(e+17u,s->anonce,32u)))
            return failure(s,WPA_REPLAY);
        if (s->stage==WPA_WAIT_M1) {
            badger_bytes_copy(s->replay,e+9u,8u); badger_bytes_copy(s->anonce,e+17u,32u);
            wpa_ptk(s->pmk,s->bssid,s->mac,s->anonce,s->snonce,s->ptk);
            s->stage=WPA_WAIT_M3;
        }
        ++s->m1; *length=response(s,e,0,frame); return 1;
    }
    if (info!=0x13cau || s->stage==WPA_WAIT_M1) return failure(s,WPA_FORMAT);
    uint8_t hash[20];
    wpa_sha1(e,en,hash);
    if (s->installed) {
        if (!wpa_equal(hash,s->m3_hash,20u)) return failure(s,WPA_REPLAY);
        ++s->m3; *length=response(s,e,1,frame); return 1;
    }
    if (!greater(e+9u,s->replay,8u)) return failure(s,WPA_REPLAY);
    if (!wpa_equal(e+17u,s->anonce,32u)) return failure(s,WPA_NONCE);
    uint8_t copy[363], mic[20], plain[256];
    badger_bytes_copy(copy,e,en); badger_bytes_zero(copy+81u,16u);
    wpa_hmac(s->ptk,16u,copy,en,mic); wpa_wipe(copy,sizeof(copy));
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
            if (gtk++ || len!=24u || (plain[at+6u]&0xf8u) || plain[at+7u]) {
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
    badger_bytes_copy(s->gtk,candidate,16u); wpa_wipe(candidate,sizeof(candidate)); s->gtk_id=gtk_id;
    badger_bytes_copy(s->replay,e+9u,8u); badger_bytes_copy(s->m3_hash,hash,20u);
    s->installed=1u; s->stage=WPA_KEYS; ++s->m3;
    wpa_wipe(s->pmk,sizeof(s->pmk));
    *length=response(s,e,1,frame);
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
    badger_bytes_copy(payload,prefix,16u);
    clean_station_header(m,8u,s->sequence++,s->mac,s->bssid); m[1]=0x41u;
    m[24]=s->tx_pn[0]; m[25]=s->tx_pn[1]; m[27]=32u;
    badger_bytes_copy(m+28u,s->tx_pn+2u,4u);
    badger_bytes_copy(payload+16u,s->snonce,16u);
    if (!wpa_ccmp(s->ptk+32u,m,24u,s->tx_pn,payload,32u,m+32u,m+64u,0)) return 0u;
    badger_bytes_store_u32(m+72u,diag_crc32(m,72u));
    (void)clean_ofdm_plcp_rate(frame,76u,0u);
    return 82u;
}

int clean_wpa_protected(clean_wpa_t *s, const uint8_t *m, uint32_t n) {
    if (!s->installed || s->error || n<52u || !clean_frame_valid(m,n) ||
        (m[0]!=8u && m[0]!=0x88u) || (m[1]&0xc7u)!=0x42u ||
        !wpa_equal(m+4u,s->mac,6u) || !wpa_equal(m+10u,s->bssid,6u)) return 0;
    uint32_t h=m[0]==0x88u?26u:24u;
    if (n<h+20u || (m[22]&15u) || (h==26u && (m[24]&128u))) return failure(s,WPA_FORMAT);
    const uint8_t *c=m+h;
    if (c[2] || c[3]!=32u) return failure(s,WPA_FORMAT);
    uint8_t pn[6]={c[0],c[1],c[4],c[5],c[6],c[7]}, big[6], old[6], plain[2304], tag[8];
    uint32_t tid=h==26u?(m[24]&15u):16u;
    for (uint32_t i=0u;i<6u;++i) { big[i]=pn[5u-i]; old[i]=s->rx_pn[tid][5u-i]; }
    if (!greater(big,old,6u)) return 0; /* A valid MAC retry must not reset replay state. */
    uint32_t payload=n-h-20u;
    if (payload>sizeof(plain)) return failure(s,WPA_FORMAT);
    badger_bytes_copy(tag,m+n-12u,8u);
    if (!wpa_ccmp(s->ptk+32u,m,h,pn,c+8u,payload,plain,tag,1)) return failure(s,WPA_MIC);
    badger_bytes_copy(s->rx_pn[tid],pn,6u);
    static const uint8_t reply[16]={170u,170u,3u,0u,0u,0u,136u,181u,'U','2','6','W','P','A','2',1u};
    int matched=payload>=32u && wpa_equal(plain,reply,16u) && wpa_equal(plain+16u,s->snonce,16u);
    wpa_wipe(plain,sizeof(plain));
    if (matched) { ++s->protected_rx; s->stage=WPA_PROTECTED; }
    return matched;
}

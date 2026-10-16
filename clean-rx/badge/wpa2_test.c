#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wpa2.h"

static void hex(uint8_t *out, const char *text) {
    for (unsigned i=0u;text[2u*i];++i) {
        unsigned v;
        assert(sscanf(text+2u*i,"%2x",&v)==1);
        out[i]=(uint8_t)v;
    }
}

static void expected(const uint8_t *p, const char *value) {
    uint8_t bytes[256];
    hex(bytes,value);
    if (memcmp(p,bytes,strlen(value)/2u)) {
        fprintf(stderr,"Expected %s, got ",value);
        for (uint32_t i=0u;i<strlen(value)/2u;++i) fprintf(stderr,"%02x",p[i]);
        fputc('\n',stderr);
        assert(0);
    }
}

static void crypto(void) {
    uint8_t out[64], key[32], input[64];
    wpa_sha1((const uint8_t *)"abc",3u,out);
    expected(out,"a9993e364706816aba3e25717850c26c9cd0d89d");
    memset(key,11,20u);
    wpa_hmac(key,20u,(const uint8_t *)"Hi There",8u,out);
    expected(out,"b617318655057264e28bc0b6fb378c8ef146be00");
    assert(wpa_pmk((const uint8_t *)"password",8u,(const uint8_t *)"IEEE",4u,out));
    expected(out,"f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e");
    for (uint32_t i=0u;i<16u;++i) key[i]=(uint8_t)i;
    hex(input,"00112233445566778899aabbccddeeff");
    wpa_aes_t aes;
    wpa_aes_init(&aes,key); wpa_aes_encrypt(&aes,input,out);
    expected(out,"69c4e0d86a7b0430d8cdb78070b4c55a");
    hex(input,"1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5");
    assert(wpa_unwrap(key,input,24u,out));
    expected(out,"00112233445566778899aabbccddeeff");
    input[0]^=1u; assert(!wpa_unwrap(key,input,24u,out));
    for (uint32_t i=0u;i<16u;++i) assert(!out[i]);
    uint8_t h[24], pn[6]={1u}, tag[8], cipher[32], plain[32];
    hex(h,"0841000002010203040502060708090a020b0c0d0e0f3012");
    hex(input,"aaaa0300000088b5575041322070726f7465637465642074657374");
    assert(wpa_ccmp(key,h,24u,pn,input,27u,cipher,tag,0));
    expected(cipher,"70abfd47bd90908b9dc25b318403cd179ad85baaf98172268a3035");
    expected(tag,"65d2853da1d5333d");
    assert(wpa_ccmp(key,h,24u,pn,cipher,27u,plain,tag,1) && !memcmp(input,plain,27u));
    tag[0]^=1u; assert(!wpa_ccmp(key,h,24u,pn,cipher,27u,plain,tag,1));
    for (uint32_t i=0u;i<27u;++i) assert(!plain[i]);
    uint8_t an[32], sn[32], ap[6]={2u,1u,2u,3u,4u,5u}, sta[6]={2u,6u,7u,8u,9u,10u};
    memset(key,1,32u); memset(an,2,32u); memset(sn,3,32u);
    wpa_ptk(key,ap,sta,an,sn,out);
    expected(out,"1d93b7dfbc04e28d2d14d3bc9c52566b62f5880acd9bff233bc207a89a734849a4d9906338d8966a61703bae214a0c83");
}

static const uint8_t rsn[22]={48u,20u,1u,0u,0u,15u,172u,4u,1u,0u,0u,15u,172u,4u,1u,0u,0u,15u,172u,2u,0u,0u};

static void binding(void) {
    uint8_t secret[64], ap[6]={2u,1u,2u,3u,4u,5u}, digest[20], changed[20];
    for (unsigned i=0u;i<sizeof(secret);++i) secret[i]=(uint8_t)i;
    wpa_secret_binding(secret,ap,11u,digest);
    expected(digest,"f18b1be11c549f1e96bc3bec6c7daa76a9cfaa4d");
    for (unsigned i=0u;i<sizeof(secret);++i) {
        secret[i]^=1u;
        wpa_secret_binding(secret,ap,11u,changed);
        assert(!wpa_equal(digest,changed,8u));
        secret[i]^=1u;
    }
    for (unsigned i=0u;i<sizeof(ap);++i) {
        ap[i]^=1u;
        wpa_secret_binding(secret,ap,11u,changed);
        assert(!wpa_equal(digest,changed,8u));
        ap[i]^=1u;
    }
    wpa_secret_binding(secret,ap,1u,changed);
    assert(!wpa_equal(digest,changed,8u));
}

static uint32_t wrap(const uint8_t key[16], const uint8_t plain[48], uint8_t out[56]) {
    wpa_aes_t aes; wpa_aes_init(&aes,key); memset(out,0xa6,8u); memcpy(out+8u,plain,48u);
    uint8_t block[16];
    for (unsigned j=0u;j<6u;++j) for (unsigned i=1u;i<=6u;++i) {
        memcpy(block,out,8u); memcpy(block+8u,out+8u*i,8u); wpa_aes_encrypt(&aes,block,block);
        memcpy(out,block,8u); out[7]^=(uint8_t)(6u*j+i); memcpy(out+8u*i,block+8u,8u);
    }
    return 56u;
}

static uint32_t incoming(clean_wpa_t *s, uint8_t m[512], int m3) {
    memset(m,0,512u); m[0]=8u; m[1]=2u;
    memcpy(m+4u,s->mac,6u); memcpy(m+10u,s->bssid,6u); memcpy(m+16u,s->bssid,6u);
    const uint8_t llc[8]={170u,170u,3u,0u,0u,0u,136u,142u}; memcpy(m+24u,llc,8u);
    uint8_t *e=m+32u;
    uint32_t n=m3?155u:99u;
    e[0]=2u; e[1]=3u; e[3]=(uint8_t)(n-4u); e[4]=2u;
    e[5]=m3?0x13u:0u; e[6]=m3?0xcau:0x8au; e[8]=16u; e[16]=m3?2u:1u;
    memset(e+17u,2,32u);
    if (m3) {
        uint8_t plain[48]={0}, mic[20];
        memcpy(plain,rsn,22u);
        uint8_t gtk[24]={221u,22u,0u,15u,172u,1u,1u,0u};
        memset(gtk+8u,4,16u); memcpy(plain+22u,gtk,24u); plain[46]=221u;
        e[98]=(uint8_t)wrap(s->ptk+16u,plain,e+99u);
        wpa_hmac(s->ptk,16u,e,n,mic); memcpy(e+81u,mic,16u);
    }
    badger_bytes_store_u32(m+32u+n,diag_crc32(m,32u+n));
    return 36u+n;
}

static void sign_key(clean_wpa_t *s, uint8_t *m, uint32_t n) {
    uint8_t mic[20], *e=m+32u;
    memset(e+81u,0,16u);
    wpa_hmac(s->ptk,16u,e,((uint32_t)e[2]<<8u)+e[3]+4u,mic);
    memcpy(e+81u,mic,16u);
    badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
}

static void key_data_failures(clean_wpa_t before) {
    uint8_t m[512], tx[512], plain[48];
    uint32_t length, n;
    clean_wpa_t s=before;
    n=incoming(&s,m,1);
    m[32u+17u]^=1u;
    badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_receive(&s,m,n,tx,&length)==-1 && s.error==WPA_NONCE);
    s=before; n=incoming(&s,m,1);
    m[35]=200u;
    badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_receive(&s,m,n,tx,&length)==-1 && s.error==WPA_FORMAT);
    s=before; n=incoming(&s,m,1);
    m[32u+99u]^=1u; sign_key(&s,m,n);
    assert(clean_wpa_receive(&s,m,n,tx,&length)==-1 && s.error==WPA_UNWRAP);
    for (unsigned fault=0u;fault<5u;++fault) {
        s=before; n=incoming(&s,m,1);
        assert(wpa_unwrap(s.ptk+16u,m+32u+99u,56u,plain));
        if (fault==0u) plain[21]^=1u;
        if (fault==1u) plain[28]=0x80u;
        if (fault==2u) plain[47]=1u;
        if (fault==3u) plain[1]=255u;
        if (fault==4u) plain[22]=1u;
        wrap(s.ptk+16u,plain,m+32u+99u); sign_key(&s,m,n);
        assert(clean_wpa_receive(&s,m,n,tx,&length)==-1 && !s.installed);
        assert(s.error==(fault==0u?WPA_RSN:fault==1u || fault==4u?WPA_GTK:WPA_FORMAT));
        for (unsigned i=0u;i<48u;++i) assert(!s.ptk[i]);
    }
    s=before; n=incoming(&s,m,1); s.ptk[0]^=1u;
    assert(clean_wpa_receive(&s,m,n,tx,&length)==-1 && s.error==WPA_MIC);
}

static uint32_t protected_frame(clean_wpa_t *s, uint8_t m[512], uint8_t tid, uint8_t number) {
    uint32_t h=tid==16u?24u:26u;
    uint8_t pn[6]={number}, plain[32]={
        170u,170u,3u,0u,0u,0u,136u,181u,'U','2','6','W','P','A','2',1u
    };
    memset(m,0,512u); m[0]=h==24u?8u:0x88u; m[1]=0x42u;
    memcpy(m+4u,s->mac,6u); memcpy(m+10u,s->bssid,6u); memcpy(m+16u,s->bssid,6u);
    if (h==26u) m[24]=tid;
    m[h]=number; m[h+3u]=32u; memcpy(plain+16u,s->snonce,16u);
    assert(wpa_ccmp(s->ptk+32u,m,h,pn,plain,32u,m+h+8u,m+h+40u,0));
    badger_bytes_store_u32(m+h+48u,diag_crc32(m,h+48u));
    return h+52u;
}

static void protected_traffic(clean_wpa_t before) {
    uint8_t m[512], tx[512];
    clean_wpa_t s=before;
    for (uint8_t tid=0u;tid<=16u;++tid) {
        uint32_t n=protected_frame(&s,m,tid,1u);
        assert(clean_wpa_protected(&s,m,n)==1 && s.rx_pn[tid][0]==1u);
        assert(clean_wpa_protected(&s,m,n)==0 && !s.error);
        n=protected_frame(&s,m,tid,2u);
        assert(clean_wpa_protected(&s,m,n)==1 && s.rx_pn[tid][0]==2u);
    }
    uint32_t n=protected_frame(&s,m,16u,3u);
    m[n-12u]^=1u; badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_protected(&s,m,n)==-1 && s.error==WPA_MIC && s.rx_pn[16][0]==2u);
    assert(!s.installed);
    s=before;
    assert(clean_wpa_probe(&s,tx)==82u && s.tx_pn[0]==1u);
    assert(clean_wpa_probe(&s,tx)==82u && s.tx_pn[0]==2u);
    memset(s.tx_pn,255u,6u);
    assert(!clean_wpa_probe(&s,tx) && s.error==WPA_EXHAUSTED && !s.installed);
}

static void handshake(void) {
    uint8_t secret[64], mac[6]={2u,6u,7u,8u,9u,10u}, ap[6]={2u,1u,2u,3u,4u,5u};
    memset(secret,1,32u); memset(secret+32u,3,32u);
    clean_wpa_t s;
    clean_wpa_init(&s,secret,mac,ap,rsn,22u);
    uint8_t m[512], tx[512]; uint32_t size, n=incoming(&s,m,0);
    assert(clean_wpa_receive(&s,m,n,tx,&size)==1 && size==163u && s.stage==WPA_WAIT_M3);
    uint8_t first[512]; memcpy(first,tx,size);
    assert(clean_wpa_receive(&s,m,n,tx,&size)==1 && !memcmp(first+38u,tx+38u,size-42u));
    n=incoming(&s,m,1);
    clean_wpa_t before=s;
    key_data_failures(before);
    memset(m+n-4u,0x5a,38u);
    n+=38u;
    badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_receive(&s,m,n,tx,&size)==1 && size==141u && s.installed && s.stage==WPA_KEYS);
    protected_traffic(s);
    s.tx_pn[0]=5u; s.rx_pn[0][0]=9u;
    assert(clean_wpa_receive(&s,m,n,tx,&size)==1 && s.tx_pn[0]==5u && s.rx_pn[0][0]==9u);
    s=before; m[32u+81u]^=1u; badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_receive(&s,m,n,tx,&size)==-1 && s.error==WPA_MIC && !s.installed);
    s=before; n=incoming(&s,m,1); m[48]=1u;
    badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_receive(&s,m,n,tx,&size)==-1 && s.error==WPA_REPLAY);
}

int main(void) {
    crypto(); binding(); handshake();
    puts("WPA2: independent SHA1/HMAC/PBKDF2/PTK/AES/CCMP vectors, unwrap, handshake, MIC/replay and no reinstall pass");
    return 0;
}

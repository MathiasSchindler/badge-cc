#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wpa2.h"
#include "station_rx.h"

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

static void provisioning(void) {
    uint8_t pmk[32], passphrase[63], ssid[32], raw[64];
    memset(passphrase,'p',sizeof(passphrase)); memset(ssid,'s',sizeof(ssid));
    const char *known="f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e";
    assert(wpa_psk((const uint8_t *)"password",8u,(const uint8_t *)"IEEE",4u,pmk));
    expected(pmk,known);
    memcpy(raw,known,64u);
    assert(wpa_psk(raw,64u,ssid,32u,pmk)); expected(pmk,known);
    for (uint32_t i=0u;i<64u;++i) if (raw[i]>='a' && raw[i]<='f') raw[i]-=32u;
    assert(wpa_psk(raw,64u,ssid,32u,pmk)); expected(pmk,known);
    for (uint32_t i=0u;i<64u;++i) {
        uint8_t saved=raw[i]; raw[i]='g'; memset(pmk,0xa5,32u);
        assert(!wpa_psk(raw,64u,ssid,32u,pmk));
        for (uint32_t j=0u;j<32u;++j) assert(!pmk[j]);
        raw[i]=saved;
    }
    assert(wpa_psk(passphrase,63u,ssid,32u,pmk));
    const uint32_t lengths[]={0u,7u,65u};
    for (uint32_t i=0u;i<3u;++i) {
        memset(pmk,0xa5,32u);
        assert(!wpa_psk(raw,lengths[i],ssid,32u,pmk));
        for (uint32_t j=0u;j<32u;++j) assert(!pmk[j]);
    }
    assert(!wpa_psk(raw,64u,ssid,0u,pmk));
    assert(!wpa_psk(raw,64u,ssid,33u,pmk));
    for (uint32_t j=0u;j<32u;++j) assert(!pmk[j]);
    puts("E1 provisioning: passphrase/raw PSK vectors, mixed hex, length boundaries and invalid-input wiping pass");
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

static uint32_t wrap_size(const uint8_t key[16], const uint8_t *plain, uint32_t size, uint8_t *out) {
    assert(size>=16u && size<=256u && !(size&7u));
    uint32_t count=size/8u;
    wpa_aes_t aes; wpa_aes_init(&aes,key); memset(out,0xa6,8u); memcpy(out+8u,plain,size);
    uint8_t block[16];
    for (unsigned j=0u;j<6u;++j) for (unsigned i=1u;i<=count;++i) {
        memcpy(block,out,8u); memcpy(block+8u,out+8u*i,8u); wpa_aes_encrypt(&aes,block,block);
        memcpy(out,block,8u);
        uint32_t t=count*j+i;
        for (uint32_t b=0u;b<4u;++b) out[7u-b]^=(uint8_t)(t>>(8u*b));
        memcpy(out+8u*i,block+8u,8u);
    }
    return size+8u;
}

static uint32_t wrap(const uint8_t key[16], const uint8_t plain[48], uint8_t out[56]) {
    return wrap_size(key,plain,48u,out);
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
        if (fault==1u) plain[23]=21u;
        if (fault==2u) plain[47]=1u;
        if (fault==3u) plain[1]=255u;
        if (fault==4u) plain[22]=1u;
        wrap(s.ptk+16u,plain,m+32u+99u); sign_key(&s,m,n);
        assert(clean_wpa_receive(&s,m,n,tx,&length)==-1 && !s.installed);
        assert(s.error==(fault==0u?WPA_RSN:fault==1u || fault==4u?WPA_GTK:WPA_FORMAT));
        for (unsigned i=0u;i<48u;++i) assert(!s.ptk[i]);
    }
    for (uint32_t duplicate=0u;duplicate<2u;++duplicate) {
        s=before; n=incoming(&s,m,1);
        assert(wpa_unwrap(s.ptk+16u,m+32u+99u,56u,plain));
        uint8_t repeated[72]={0};
        memcpy(repeated,plain,46u);
        uint32_t bytes=duplicate?24u:22u;
        memcpy(repeated+46u,plain+(duplicate?22u:0u),bytes);
        repeated[46u+bytes]=221u;
        uint8_t *e=m+32u;
        e[98]=(uint8_t)wrap_size(s.ptk+16u,repeated,sizeof(repeated),e+99u);
        e[3]=(uint8_t)(95u+e[98]); n=36u+99u+e[98];
        sign_key(&s,m,n);
        assert(clean_wpa_receive(&s,m,n,tx,&length)==-1 &&
               s.error==(duplicate?WPA_GTK:WPA_RSN) && !s.installed);
        for (uint32_t i=0u;i<16u;++i) assert(!s.gtk[i]);
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
        assert(clean_wpa_protected(&s,m,n,0u)==1 && s.rx_pn[tid][0]==1u);
        assert(clean_wpa_protected(&s,m,n,1u)==0 && !s.error);
        n=protected_frame(&s,m,tid,2u);
        assert(clean_wpa_protected(&s,m,n,2u)==1 && s.rx_pn[tid][0]==2u);
    }
    uint32_t n=protected_frame(&s,m,16u,3u);
    m[n-12u]^=1u; badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_protected(&s,m,n,3u)==-1 && s.error==WPA_MIC && s.rx_pn[16][0]==2u);
    assert(!s.installed);
    s=before;
    assert(clean_wpa_probe(&s,tx)==82u && s.tx_pn[0]==1u);
    assert(clean_wpa_probe(&s,tx)==82u && s.tx_pn[0]==2u);
    s.tx_qos=1u;
    assert(clean_wpa_probe(&s,tx)==84u && s.tx_pn[0]==3u);
    assert(tx[6]==0x88u && tx[30]==0u && tx[31]==0u &&
           clean_frame_valid(tx+6u,78u) && clean_station_tx_tid(tx+6u,78u,1u)==0);
    uint8_t plain[32], tag[8], original[84];
    memcpy(original,tx,sizeof(original)); memcpy(tag,tx+72u,8u);
    assert(wpa_ccmp(s.ptk+32u,tx+6u,26u,s.tx_pn,tx+40u,32u,plain,tag,1));
    assert(!memcmp(plain+16u,s.snonce,16u));
    assert(clean_mac_retry(tx+6u,78u));
    assert(!memcmp(tx+32u,original+32u,48u) && s.tx_pn[0]==3u);
    memcpy(tag,tx+72u,8u);
    assert(wpa_ccmp(s.ptk+32u,tx+6u,26u,s.tx_pn,tx+40u,32u,plain,tag,1));
    memset(s.tx_pn,255u,6u);
    assert(!clean_wpa_probe(&s,tx) && s.error==WPA_EXHAUSTED && !s.installed);
}

static uint32_t protected_qos_fragment(clean_wpa_t *s, uint8_t *m, uint8_t tid,
        uint16_t sequence, uint8_t frag, int more, uint64_t number,
        const uint8_t *plain, uint32_t payload) {
    uint32_t h=tid==16u?24u:26u;
    uint8_t pn[6]={0};
    for (uint32_t i=0u;i<6u;++i) pn[i]=(uint8_t)(number>>(8u*i));
    memset(m,0,h+20u+payload);
    m[0]=tid==16u?8u:0x88u; m[1]=(uint8_t)(0x42u|(more?4u:0u));
    memcpy(m+4u,s->mac,6u); memcpy(m+10u,s->bssid,6u); memcpy(m+16u,s->bssid,6u);
    m[22]=(uint8_t)((sequence<<4u)|frag); m[23]=(uint8_t)(sequence>>4u);
    if (h==26u) m[24]=tid;
    m[h]=pn[0]; m[h+1u]=pn[1]; m[h+3u]=32u; memcpy(m+h+4u,pn+2u,4u);
    assert(wpa_ccmp(s->ptk+32u,m,h,pn,plain,payload,m+h+8u,m+h+8u+payload,0));
    badger_bytes_store_u32(m+h+16u+payload,diag_crc32(m,h+16u+payload));
    return h+20u+payload;
}

static uint32_t protected_fragment(clean_wpa_t *s, uint8_t *m, uint16_t sequence,
                                    uint8_t frag, int more, uint32_t number,
                                    const uint8_t *plain, uint32_t payload) {
    return protected_qos_fragment(s,m,16u,sequence,frag,more,number,plain,payload);
}

static void protected_fragments(clean_wpa_t before) {
    uint8_t m[CLEAN_MPDU_BYTES], plain[CLEAN_MSDU_BYTES];
    const uint8_t reply[16]={170u,170u,3u,0u,0u,0u,136u,181u,'U','2','6','W','P','A','2',1u};
    memset(plain,0x5a,sizeof(plain));
    memcpy(plain,reply,16u); memcpy(plain+16u,before.snonce,16u);
    const uint32_t sizes[]={32u,1500u,2304u};
    for (uint32_t size=0u;size<3u;++size) {
        clean_wpa_t s=before;
        uint32_t at=0u;
        for (uint8_t frag=0u;frag<16u;++frag) {
            uint32_t count=frag==15u?sizes[size]-at:sizes[size]/16u;
            uint32_t n=protected_fragment(&s,m,4095u,frag,frag!=15u,250u+frag,plain+at,count);
            at+=count;
            assert(clean_wpa_protected(&s,m,n,100u+frag)==(frag==15u));
            assert(!s.error && s.installed);
            assert(s.rx_pn[16][0]==(uint8_t)(250u+frag));
            assert(s.rx_pn[16][1]==(uint8_t)((250u+frag)>>8u));
            if (frag!=15u) {
                assert(s.rx_mac.active && s.rx_mac.length==24u+at);
                assert(!memcmp(s.rx_mac.frame+24u,plain,at));
            }
            assert(clean_mac_retry(m,n));
            assert(clean_wpa_protected(&s,m,n,101u+frag)==0 && !s.error);
        }
        assert(s.protected_rx==1u && s.rx_mac.delivered==1u && s.rx_mac.fragments==15u);
        for (uint32_t i=0u;i<sizeof(s.rx_mac.frame);++i) assert(!s.rx_mac.frame[i]);
        for (uint32_t i=0u;i<sizeof(s.rx_mpdu);++i) assert(!s.rx_mpdu[i]);
        uint32_t n=protected_fragment(&s,m,0u,0u,0,266u,plain,32u);
        assert(clean_wpa_protected(&s,m,n,200u)==1 && s.rx_mac.delivered==2u);
    }
    for (uint32_t fault=0u;fault<8u;++fault) {
        clean_wpa_t s=before;
        uint32_t first=fault==4u?2304u:1u;
        uint32_t n=protected_fragment(&s,m,10u,0u,1,1u,plain,first);
        assert(clean_wpa_protected(&s,m,n,0u)==0 && s.rx_mac.active);
        n=protected_fragment(&s,m,fault==1u?11u:10u,fault==2u?2u:1u,
            fault==6u, fault==0u?3u:2u,plain+first,first==2304u?0u:31u);
        if (fault==3u) { m[n-12u]^=1u; badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u)); }
        if (fault==4u) n=protected_fragment(&s,m,10u,1u,0,2u,plain,1u);
        if (fault==6u) {
            m[22]^=1u;
            badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
        }
        if (fault==7u) {
            m[27]=96u;
            badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
        }
        uint32_t now=fault==5u?CLEAN_MAC_RX_LIFETIME_US:1u;
        assert(clean_wpa_protected(&s,m,n,now)==-1);
        assert(s.error==(fault==3u||fault==6u?WPA_MIC:fault==7u?WPA_FORMAT:WPA_FRAGMENT));
        assert(!s.installed && !s.rx_mac.active && !s.protected_rx);
        for (uint32_t i=0u;i<sizeof(s.rx_mac.frame);++i) assert(!s.rx_mac.frame[i]);
    }
}

static void qos_plaintext_wiped(const clean_wpa_t *s) {
    for (uint32_t tid=0u;tid<16u;++tid) {
        assert(!s->rx_qos.tid[tid].active);
        for (uint32_t i=0u;i<sizeof(s->rx_qos.tid[tid].frame);++i)
            assert(!s->rx_qos.tid[tid].frame[i]);
        for (uint32_t i=0u;i<6u;++i) assert(!s->qos_fragment_pn[tid][i]);
    }
    for (uint32_t i=0u;i<sizeof(s->rx_mpdu);++i) assert(!s->rx_mpdu[i]);
}

static void aggregate_crypto(void) {
    static const uint32_t lengths[]={15u,16u,17u,2304u,2305u,3839u};
    static const char *hashes[]={
        "acfb7a95d788536b17142ab3f5a44fdd9fbd0eb5",
        "7f572d9363794862832ef1296f13a47fe4d1e4bb",
        "6bbf2739ce6a9dd04f172f1bb8b93a179173585d",
        "092f7d54b362fbe647c7ad3277fd744bcb178260",
        "ae32392d8942b32713f62f0822487774bf94914b",
        "46c72dcf4b8cf6f3417cf375fe7cc591175dd2b9"
    };
    static const char *tags[]={
        "1c5180f2b0de0756","bbd2ff2f99a21997","eb35f48930c7b204",
        "8df77fdeefdfbca9","f529c6ffa618b4a0","6ada419d268db7a6"
    };
    uint8_t h[26]={0x88u,0x42u}, key[16], pn[6]={1u,2u,3u,4u,5u,6u};
    uint8_t in[CLEAN_AMSDU_BYTES], out[CLEAN_AMSDU_BYTES+1u], plain[CLEAN_AMSDU_BYTES+1u];
    uint8_t tag[8], hash[20];
    for (uint32_t i=4u;i<22u;++i) h[i]=(uint8_t)i;
    h[24]=0x87u;
    for (uint32_t i=0u;i<16u;++i) key[i]=(uint8_t)i;
    for (uint32_t i=0u;i<sizeof(in);++i) in[i]=(uint8_t)(i*17u+3u);
    for (uint32_t i=0u;i<sizeof(lengths)/sizeof(lengths[0]);++i) {
        uint32_t n=lengths[i];
        memset(out,0xa5,sizeof(out)); memset(plain,0xa5,sizeof(plain));
        assert(wpa_ccmp(key,h,26u,pn,in,n,out,tag,0));
        wpa_sha1(out,n,hash); expected(hash,hashes[i]); expected(tag,tags[i]);
        assert(out[n]==0xa5u);
        assert(wpa_ccmp(key,h,26u,pn,out,n,plain,tag,1));
        assert(!memcmp(in,plain,n) && plain[n]==0xa5u);
        tag[0]^=1u;
        assert(!wpa_ccmp(key,h,26u,pn,out,n,plain,tag,1));
        for (uint32_t j=0u;j<n;++j) assert(!plain[j]);
        assert(plain[n]==0xa5u);
    }
    for (uint32_t fault=0u;fault<5u;++fault) {
        uint8_t header[26]; memcpy(header,h,sizeof(h));
        uint32_t n=2305u;
        if (fault==0u) header[24]&=127u;
        if (fault==1u) header[1]|=4u;
        if (fault==2u) header[22]=1u;
        if (fault==3u) header[1]|=128u;
        if (fault==4u) n=CLEAN_AMSDU_BYTES+1u;
        memset(out,0xa5,sizeof(out));
        assert(!wpa_ccmp(key,header,26u,pn,in,n,out,tag,0));
        for (uint32_t i=0u;i<sizeof(out);++i) assert(out[i]==0xa5u);
    }
    puts("CCMP A-MSDU: independent OpenSSL vectors across AES/MSDU/3839-byte boundaries, guard bytes, malformed headers and MIC-failure wiping pass");
}

static uint32_t aggregate_body(uint8_t body[CLEAN_AMSDU_BYTES], const clean_wpa_t *s,
                                uint32_t length) {
    assert(length>=94u && length<=CLEAN_AMSDU_BYTES);
    memset(body,0x5a,length);
    memcpy(body,s->mac,6u); memcpy(body+6u,s->bssid,6u);
    body[12]=0u; body[13]=32u;
    static const uint8_t reply[16]={170u,170u,3u,0u,0u,0u,136u,181u,'U','2','6','W','P','A','2',1u};
    memcpy(body+14u,reply,16u); memcpy(body+30u,s->snonce,16u);
    body[46]=0x89u; body[47]=0xacu;
    uint32_t second=length-62u;
    if (second>CLEAN_MSDU_BYTES) {
        /* Keep the first MSDU at the legacy maximum for a full aggregate. */
        uint32_t first=2304u, at=2320u;
        memset(body+46u,0x5a,length-46u);
        body[12]=(uint8_t)(first>>8u); body[13]=(uint8_t)first;
        memcpy(body+at,s->mac,6u); memcpy(body+at+6u,s->bssid,6u);
        second=length-at-14u;
        body[at+12u]=(uint8_t)(second>>8u); body[at+13u]=(uint8_t)second;
        return at;
    }
    memcpy(body+48u,s->mac,6u); memcpy(body+54u,s->bssid,6u);
    body[60]=(uint8_t)(second>>8u); body[61]=(uint8_t)second;
    return 48u;
}

static uint32_t protected_aggregate(clean_wpa_t *s, uint8_t *m, uint8_t tid,
        uint16_t sequence, uint8_t number, const uint8_t *body, uint32_t payload) {
    memset(m,0,46u+payload);
    m[0]=0x88u; m[1]=0x42u;
    memcpy(m+4u,s->mac,6u); memcpy(m+10u,s->bssid,6u); memcpy(m+16u,s->bssid,6u);
    m[22]=(uint8_t)(sequence<<4u); m[23]=(uint8_t)(sequence>>4u);
    m[24]=(uint8_t)(128u|tid);
    m[26]=number; m[29]=32u;
    uint8_t pn[6]={number};
    assert(wpa_ccmp(s->ptk+32u,m,26u,pn,body,payload,m+34u,m+34u+payload,0));
    badger_bytes_store_u32(m+42u+payload,diag_crc32(m,42u+payload));
    return 46u+payload;
}

static void protected_aggregates(clean_wpa_t before) {
    uint8_t body[CLEAN_AMSDU_BYTES], m[46u+CLEAN_AMSDU_BYTES];
    uint32_t maximum=CLEAN_MPDU_BYTES-46u;
    if (maximum>CLEAN_AMSDU_BYTES) maximum=CLEAN_AMSDU_BYTES;
    const uint32_t sizes[]={94u,1500u,maximum};
    for (uint32_t size=0u;size<3u;++size) {
        clean_wpa_t s=before; s.rx_amsdu=1u;
        aggregate_body(body,&s,sizes[size]);
        for (uint8_t tid=0u;tid<16u;++tid) {
            uint32_t n=protected_aggregate(&s,m,tid,4095u,1u,body,sizes[size]);
            assert(clean_wpa_protected(&s,m,n,0u)==1 && !s.error);
            assert(s.amsdu_rx==2u*tid+1u && s.amsdu_subframes==2u*(2u*tid+1u));
            assert(s.rx_qos.tid[tid].delivered==1u && s.rx_pn[tid][0]==1u);
            qos_plaintext_wiped(&s);
            assert(clean_mac_retry(m,n));
            assert(!clean_wpa_protected(&s,m,n,1u) && !s.error);
            n=protected_aggregate(&s,m,tid,4095u,2u,body,sizes[size]);
            assert(clean_mac_retry(m,n));
            assert(!clean_wpa_protected(&s,m,n,2u) && !s.error);
            assert(s.rx_pn[tid][0]==2u && s.rx_qos.tid[tid].duplicates==1u);
            n=protected_aggregate(&s,m,tid,0u,3u,body,sizes[size]);
            assert(clean_wpa_protected(&s,m,n,3u)==1 && s.rx_pn[tid][0]==3u);
            qos_plaintext_wiped(&s);
        }
        assert(s.protected_rx==32u && s.amsdu_rx==32u && s.amsdu_subframes==64u);
    }
    for (uint32_t fault=0u;fault<12u;++fault) {
        clean_wpa_t s=before; s.rx_amsdu=1u;
        uint32_t at=aggregate_body(body,&s,maximum);
        if (fault==0u) ++body[at+13u];
        if (fault==1u) body[at+6u]|=1u;
        if (fault==2u) body[at+12u]=body[at+13u]=0u;
        if (fault==3u) body[at]^=2u;
        if (fault==4u) memcpy(body,"\xaa\xaa\x03\x00\x00\x00",6u);
        uint32_t n=protected_aggregate(&s,m,7u,1u,1u,body,maximum);
        if (fault==5u) m[n-12u]^=1u;
        if (fault==6u) m[1]|=4u;
        if (fault==7u) m[22]|=1u;
        if (fault==8u) m[24]|=0x60u;
        if (fault==9u) s.rx_amsdu=0u;
        if (fault==10u) m[29]=96u;
        if (fault==11u) m[24]^=1u;
        badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
        assert(clean_wpa_protected(&s,m,n,0u)==-1);
        assert(s.error==((fault==5u||fault==11u)?WPA_MIC:WPA_FORMAT));
        assert(!s.installed && !s.protected_rx && !s.amsdu_rx && !s.amsdu_subframes);
        assert(!s.rx_pn[7][0] && !s.rx_qos.tid[7].delivered);
        qos_plaintext_wiped(&s);
    }
    clean_wpa_t s=before; s.rx_amsdu=1u;
    uint8_t plain[32]={170u,170u,3u,0u,0u,0u,136u,181u,'U','2','6','W','P','A','2',1u};
    memcpy(plain+16u,s.snonce,16u);
    uint32_t n=protected_qos_fragment(&s,m,0u,1u,0u,0,1u,plain,32u);
    m[24]|=128u;
    badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_protected(&s,m,n,0u)==-1 && s.error==WPA_FORMAT);
    assert(!s.protected_rx && !s.amsdu_rx && !s.rx_pn[0][0]);
    qos_plaintext_wiped(&s);
    puts("Protected A-MSDU: all TIDs, full receive bound, per-subframe addresses, no partial delivery, replay/Retry/wrap, MIC and unauthenticated aggregate-bit flip rejection pass");
}

static void protected_qos(clean_wpa_t before) {
    uint8_t m[CLEAN_MPDU_BYTES], plain[CLEAN_MSDU_BYTES];
    const uint8_t reply[16]={170u,170u,3u,0u,0u,0u,136u,181u,'U','2','6','W','P','A','2',1u};
    memset(plain,0x5a,sizeof(plain));
    memcpy(plain,reply,16u); memcpy(plain+16u,before.snonce,16u);
    const uint32_t sizes[]={32u,1500u,2304u};
    for (uint32_t size=0u;size<3u;++size) {
        clean_wpa_t s=before;
        uint32_t at=0u;
        for (uint8_t frag=0u;frag<16u;++frag) {
            uint32_t count=frag==15u?sizes[size]-at:sizes[size]/16u;
            for (uint8_t tid=0u;tid<16u;++tid) {
                uint32_t n=protected_qos_fragment(&s,m,tid,4095u,frag,frag!=15u,
                                                 250u+frag,plain+at,count);
                assert(clean_wpa_protected(&s,m,n,100u+frag)==(frag==15u));
                assert(!s.error && s.installed);
                if (frag!=15u) {
                    assert(s.rx_qos.tid[tid].active);
                    assert(s.rx_qos.tid[tid].length==26u+at+count);
                    assert(!memcmp(s.rx_qos.tid[tid].frame+26u,plain,at+count));
                }
                assert(clean_mac_retry(m,n));
                assert(clean_wpa_protected(&s,m,n,101u+frag)==0 && !s.error);
            }
            at+=count;
        }
        assert(s.protected_rx==16u && !s.rx_mac.delivered && !s.rx_pn[16][0]);
        qos_plaintext_wiped(&s);
        for (uint8_t tid=0u;tid<16u;++tid) {
            assert(s.rx_qos.tid[tid].delivered==1u && s.rx_qos.tid[tid].fragments==15u);
            assert(s.rx_pn[tid][0]==9u && s.rx_pn[tid][1]==1u);
            uint32_t n=protected_qos_fragment(&s,m,tid,0u,0u,0,266u,plain,32u);
            assert(clean_wpa_protected(&s,m,n,200u)==1);
        }
        assert(s.protected_rx==32u);
        qos_plaintext_wiped(&s);
    }
    for (uint32_t fault=0u;fault<10u;++fault) {
        clean_wpa_t s=before;
        uint32_t first=fault==4u?2304u:1u;
        uint32_t n=protected_qos_fragment(&s,m,0u,10u,0u,1,1u,plain,first);
        assert(clean_wpa_protected(&s,m,n,0u)==0);
        n=protected_qos_fragment(&s,m,1u,10u,0u,1,1u,plain,1u);
        assert(clean_wpa_protected(&s,m,n,0u)==0);
        n=protected_qos_fragment(&s,m,0u,fault==1u?11u:10u,fault==2u?2u:1u,0,
                                 fault==0u?3u:2u,plain,first==2304u?1u:31u);
        if (fault==3u) m[n-12u]^=1u;
        if (fault==6u) m[24]=2u;
        if (fault==7u) m[24]|=128u;
        if (fault==8u) m[24]|=0x60u;
        if (fault==9u) m[29]=96u;
        badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
        assert(clean_wpa_protected(&s,m,n,fault==5u?CLEAN_MAC_RX_LIFETIME_US:1u)==-1);
        assert(s.error==(fault==3u||fault==6u?WPA_MIC:
                         fault>=7u?WPA_FORMAT:WPA_FRAGMENT));
        assert(!s.installed && !s.protected_rx);
        qos_plaintext_wiped(&s);
    }
    clean_wpa_t s=before;
    uint32_t n=protected_qos_fragment(&s,m,0u,0x123u,0u,0,1u,plain,32u);
    assert(clean_wpa_protected(&s,m,n,0u)==1);
    n=protected_qos_fragment(&s,m,0u,0x123u,0u,0,2u,plain,32u);
    assert(clean_mac_retry(m,n));
    assert(clean_wpa_protected(&s,m,n,1u)==0 && !s.error);
    assert(s.protected_rx==1u && s.rx_pn[0][0]==2u && s.rx_qos.tid[0].duplicates==1u);
    n=protected_qos_fragment(&s,m,0u,0x124u,0u,0,3u,plain,32u);
    assert(clean_wpa_protected(&s,m,n,2u)==1);
    s=before;
    uint32_t start=UINT32_MAX-10u;
    for (uint8_t tid=0u;tid<16u;++tid) {
        n=protected_qos_fragment(&s,m,tid,7u,0u,1,1u,plain,1u);
        assert(clean_wpa_protected(&s,m,n,start)==0);
    }
    clean_wpa_rx_expire(&s,start+CLEAN_MAC_RX_LIFETIME_US);
    qos_plaintext_wiped(&s);
    assert(s.installed && !s.error);
    for (uint32_t tid=0u;tid<16u;++tid) assert(s.rx_qos.tid[tid].expired==1u);
    puts("WPA2 QoS: 16 interleaved TIDs x 16 fragments, size/sequence/PN boundaries, duplicate suppression, MIC/policy rejection and idle expiry/wiping pass; NOT HT negotiation");
}

static void ba_open(clean_ba_rx_t *ba, const clean_wpa_t *s, uint8_t tid,
                     uint16_t sequence) {
    uint8_t request[37]={0};
    request[0]=0xd0u;
    memcpy(request+4u,s->mac,6u);
    memcpy(request+10u,s->bssid,6u);
    memcpy(request+16u,s->bssid,6u);
    request[24]=3u;
    request[26]=1u;
    uint16_t parameters=(uint16_t)(2u | ((uint16_t)tid << 2u) | (8u << 6u));
    request[27]=(uint8_t)parameters;
    request[28]=(uint8_t)(parameters >> 8u);
    request[31]=(uint8_t)(sequence << 4u);
    request[32]=(uint8_t)(sequence >> 4u);
    badger_bytes_store_u32(request+33u,diag_crc32(request,33u));
    assert(clean_ba_action(ba,request,sizeof(request))==CLEAN_BA_PREPARED);
    assert(clean_ba_activate(ba,tid,0u)==CLEAN_BA_ACCEPTED);
}

static uint32_t ba_protected_frame(clean_wpa_t *s, uint8_t *m, uint8_t tid,
                                   uint16_t sequence, uint8_t pn) {
    uint32_t n=protected_frame(s,m,tid,pn);
    m[22]=(uint8_t)(sequence << 4u);
    m[23]=(uint8_t)(sequence >> 4u);
    m[24]|=0x60u;
    /* CCMP masks sequence and ACK policy out of its AAD. */
    badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    return n;
}

static void ba_wiped(const clean_wpa_t *s) {
    clean_ba_rx_t empty;
    clean_ba_init(&empty,s->mac,s->bssid);
    assert(s->ba && !memcmp(s->ba,&empty,sizeof(empty)));
    if (s->ba_stream) {
        clean_ampdu_stream_t stream;
        memset(&stream,0,sizeof(stream));
        stream.failed=1u;
        assert(!memcmp(s->ba_stream,&stream,sizeof(stream)));
    }
    assert(!s->ba_servicing && !s->ba_reset_pending);
}

static uint32_t ba_append(uint8_t *wire, uint32_t at, const uint8_t *m,
                           uint32_t n, int last) {
    wire[at]=(uint8_t)(n << 4u);
    wire[at+1u]=(uint8_t)(n >> 4u);
    wire[at+2u]=clean_ampdu_delimiter_crc(wire+at);
    wire[at+3u]=0x4eu;
    memcpy(wire+at+4u,m,n);
    at+=4u+n;
    if (!last) while (at&3u) wire[at++]=0u;
    return at;
}

static void protected_block_ack(clean_wpa_t before) {
    static clean_ba_rx_t ba;
    static clean_ampdu_stream_t stream;
    clean_wpa_t s=before;
    uint8_t m[512], first[512];
    assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
    ba_open(&ba,&s,3u,4095u);
    uint32_t n=ba_protected_frame(&s,m,3u,0u,2u);
    assert(clean_wpa_ba_receive(&s,&ba,m,n,1u)==CLEAN_BA_ACCEPTED);
    assert(s.protected_rx==0u && s.rx_pn[3][0]==0u);
    assert(clean_wpa_ba_receive(&s,&ba,m,n,2u)==CLEAN_BA_DUPLICATE);
    n=ba_protected_frame(&s,first,3u,4095u,1u);
    assert(clean_wpa_ba_receive(&s,&ba,first,n,3u)==CLEAN_BA_ACCEPTED);
    assert(s.protected_rx==2u && s.rx_pn[3][0]==2u && ba.delivered==2u && !s.error);
    assert(clean_wpa_ba_receive(&s,&ba,first,n,4u)==CLEAN_BA_DUPLICATE);
    assert(s.protected_rx==2u && s.rx_pn[3][0]==2u);
    n=ba_protected_frame(&s,m,3u,1u,1u);
    assert(clean_wpa_ba_receive(&s,&ba,m,n,5u)==CLEAN_BA_ACCEPTED);
    assert(s.protected_rx==2u && s.rx_pn[3][0]==2u && !s.error);
    ba_open(&ba,&s,4u,0u);
    n=ba_protected_frame(&s,m,4u,0u,1u);
    assert(clean_wpa_ba_receive(&s,&ba,m,n,6u)==CLEAN_BA_ACCEPTED);
    assert(s.protected_rx==3u && s.rx_pn[4][0]==1u);
    n=ba_protected_frame(&s,m,3u,3u,4u);
    assert(clean_wpa_ba_receive(&s,&ba,m,n,7u)==CLEAN_BA_ACCEPTED);
    assert(s.protected_rx==3u && s.rx_pn[3][0]==2u);
    assert(clean_wpa_ba_poll(&s,&ba,7u+CLEAN_BA_GAP_US)==CLEAN_BA_ACCEPTED);
    assert(s.protected_rx==4u && s.rx_pn[3][0]==4u);
    n=ba_protected_frame(&s,m,3u,5u,6u);
    assert(clean_wpa_ba_receive(&s,&ba,m,n,8u+CLEAN_BA_GAP_US)==CLEAN_BA_ACCEPTED);
    uint8_t bar[24]={0};
    bar[0]=0x84u;
    memcpy(bar+4u,s.mac,6u); memcpy(bar+10u,s.bssid,6u);
    bar[16]=4u; bar[17]=0x30u; bar[18]=0x50u;
    badger_bytes_store_u32(bar+20u,diag_crc32(bar,20u));
    assert(clean_wpa_ba_bar(&s,&ba,bar,sizeof(bar),9u+CLEAN_BA_GAP_US)==CLEAN_BA_ACCEPTED);
    assert(s.protected_rx==5u && s.rx_pn[3][0]==6u);
    n=ba_protected_frame(&s,m,3u,6u,7u);
    m[n-12u]^=1u;
    badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_ba_receive(&s,&ba,m,n,10u+CLEAN_BA_GAP_US)==CLEAN_BA_SINK_ERROR);
    assert(s.error==WPA_MIC && !s.installed && s.rx_pn[3][0]==6u);
    ba_wiped(&s);
    assert(clean_wpa_ba_poll(&s,&ba,11u+CLEAN_BA_GAP_US)==CLEAN_BA_INVALID);
    qos_plaintext_wiped(&s);

    /* Full delimiter stream -> bounded ciphertext reorder -> CCMP -> delivery. */
    s=before;
    assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
    ba_open(&ba,&s,7u,10u);
    uint8_t wire[256]={0};
    uint32_t size=0u;
    for (uint32_t i=0u;i<2u;++i) {
        uint8_t pn=(uint8_t)(2u-i);
        n=ba_protected_frame(&s,m,7u,(uint16_t)(9u+pn),pn);
        wire[size]=(uint8_t)(n << 4u);
        wire[size+1u]=(uint8_t)(n >> 4u);
        wire[size+2u]=clean_ampdu_delimiter_crc(wire+size);
        wire[size+3u]=0x4eu;
        memcpy(wire+size+4u,m,n);
        size+=4u+n;
        if (!i) size=(size+3u)&~3u;
    }
    assert(clean_wpa_ampdu_begin(&s,size)==CLEAN_BA_ACCEPTED);
    for (uint32_t offset=0u;offset<size;) {
        uint32_t chunk=size-offset<7u?size-offset:7u, used;
        assert(clean_wpa_ampdu_feed(&s,&ba,&stream,wire+offset,chunk,&used,offset)==
               CLEAN_BA_ACCEPTED && used==chunk);
        offset+=used;
    }
    assert(clean_ampdu_end(&stream)==CLEAN_BA_ACCEPTED && stream.frames==2u);
    assert(s.protected_rx==2u && s.rx_pn[7][0]==2u && ba.delivered==2u && !s.error);
    assert(clean_wpa_ampdu_feed(&s,&ba,&stream,0,0u,0,1u)==CLEAN_BA_INVALID);
    clean_ba_close(&ba,7u);
    assert(clean_wpa_ba_receive(&s,&ba,m,n,9u)==CLEAN_BA_NO_AGREEMENT);
    puts("Protected BA: ciphertext reordering before per-TID PN checks, sequence wrap, replay, MIC rejection and delimiter-stream delivery pass; NOT PSM/on-air BA");
}

static void protected_ba_lifecycle(clean_wpa_t before) {
    static clean_ba_rx_t ba, other;
    static clean_ampdu_stream_t stream, other_stream;
    clean_wpa_t s=before;
    uint8_t m[512], wire[512], plain[64]={
        170u,170u,3u,0u,0u,0u,136u,181u,'U','2','6','W','P','A','2',1u
    };
    memcpy(plain+16u,s.probe_token,16u);
    memset(&ba,0x5a,sizeof(ba));
    s.installed=0u;
    assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_INVALID);
    for (uint32_t i=0u;i<sizeof(ba);++i) assert(((uint8_t *)&ba)[i]==0x5au);
    s.installed=1u;
    assert(clean_wpa_ba_bind(&s,0,&stream)==CLEAN_BA_INVALID);
    assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
    ba_wiped(&s);
    assert(clean_wpa_ba_bind(&s,&other,&stream)==CLEAN_BA_CONFLICT);
    assert(clean_wpa_ba_bind(&s,&ba,&other_stream)==CLEAN_BA_CONFLICT);
    uint32_t n=ba_protected_frame(&s,m,3u,0u,1u);
    assert(clean_wpa_ba_receive(&s,&other,m,n,0u)==CLEAN_BA_INVALID);
    assert(clean_wpa_ba_receive(&s,&ba,m,n,0u)==CLEAN_BA_NO_AGREEMENT);
    for (uint8_t tid=0u;tid<4u;++tid) ba_open(&ba,&s,tid,0u);
    for (uint8_t tid=0u;tid<3u;++tid)
        for (uint8_t seq=1u;seq<=(tid==2u?2u:7u);++seq) {
            n=ba_protected_frame(&s,m,tid,seq,seq);
            assert(clean_wpa_ba_receive(&s,&ba,m,n,1u)==CLEAN_BA_ACCEPTED);
        }
    assert(ba.accepted==CLEAN_BA_SLOTS && !ba.delivered && !s.protected_rx);
    assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
    assert(ba.accepted==CLEAN_BA_SLOTS);
    n=ba_protected_frame(&s,m,3u,0u,1u);
    uint32_t size=ba_append(wire,0u,m,n,1), used;
    assert(clean_wpa_ampdu_begin(&s,size)==CLEAN_BA_ACCEPTED);
    assert(clean_wpa_ampdu_feed(&s,&ba,&other_stream,wire,size,&used,2u)==CLEAN_BA_INVALID &&
           !used);
    assert(clean_wpa_ampdu_feed(&s,&ba,&stream,wire,size,&used,2u)==CLEAN_BA_BUSY &&
           used==size && !stream.frames && !ba.tid[3].bitmap[0]);
    clean_ampdu_stream_t held=stream;
    assert(clean_wpa_ampdu_begin(&s,size+4u)==CLEAN_BA_BUSY);
    assert(!memcmp(&held,&stream,sizeof(stream)));
    assert(clean_wpa_ba_poll(&s,&ba,1u+CLEAN_BA_GAP_US)==CLEAN_BA_ACCEPTED);
    assert(ba.delivered==CLEAN_BA_SLOTS && s.protected_rx==CLEAN_BA_SLOTS);
    assert(clean_wpa_ampdu_feed(&s,&ba,&stream,0,0u,&used,2u+CLEAN_BA_GAP_US)==
           CLEAN_BA_ACCEPTED && !used);
    assert(clean_ampdu_end(&stream)==CLEAN_BA_ACCEPTED && stream.frames==1u &&
           s.rx_pn[3][0]==1u);
    for (uint8_t ppdu=0u;ppdu<3u;++ppdu) {
        size=0u;
        for (uint8_t i=0u;i<2u;++i) {
            uint8_t seq=(uint8_t)(2u+2u*ppdu-i);
            n=protected_qos_fragment(&s,m,3u,seq,0u,0,seq+1u,plain,
                                      33u+15u*ppdu+i);
            m[24]|=0x60u;
            badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
            size=ba_append(wire,size,m,n,i==1u);
        }
        assert(clean_wpa_ampdu_begin(&s,size)==CLEAN_BA_ACCEPTED);
        for (uint32_t offset=0u;offset<size;) {
            uint32_t chunk=size-offset<7u?size-offset:7u;
            assert(clean_wpa_ampdu_feed(&s,&ba,&stream,wire+offset,chunk,&used,
                                       3u+CLEAN_BA_GAP_US+ppdu)==CLEAN_BA_ACCEPTED &&
                   used==chunk);
            offset+=used;
        }
        assert(clean_ampdu_end(&stream)==CLEAN_BA_ACCEPTED && stream.frames==2u);
        assert(s.rx_pn[3][0]==3u+2u*ppdu && ba.delivered==19u+2u*ppdu);
    }
    assert(clean_wpa_ampdu_begin(&s,12u)==CLEAN_BA_ACCEPTED);
    assert(clean_wpa_ampdu_feed(&s,&ba,&stream,wire,2u,&used,5u+CLEAN_BA_GAP_US)==
           CLEAN_BA_ACCEPTED && used==2u && stream.delimiter_bytes==2u);
    n=ba_protected_frame(&s,m,3u,8u,9u);
    assert(clean_wpa_ba_receive(&s,&ba,m,n,6u+CLEAN_BA_GAP_US)==CLEAN_BA_ACCEPTED);
    assert(clean_wpa_abort(&s,WPA_ASSOCIATION)==-1);
    ba_wiped(&s);
    assert(clean_wpa_ampdu_begin(&s,size)==CLEAN_BA_INVALID);
    assert(clean_wpa_ba_unbind(&s)==CLEAN_BA_ACCEPTED && !s.ba && !s.ba_stream);

    uint8_t secret[64], tx[512], old[512];
    memcpy(old,m,n);
    uint32_t old_length=n, length;
    memset(secret,1,32u); memset(secret+32u,5,32u);
    clean_wpa_init(&s,secret,before.mac,before.bssid,rsn,22u);
    n=incoming(&s,m,0);
    assert(clean_wpa_receive(&s,m,n,tx,&length)==1);
    n=incoming(&s,m,1);
    assert(clean_wpa_receive(&s,m,n,tx,&length)==1 && s.installed);
    assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
    ba_wiped(&s);
    assert(clean_wpa_ba_receive(&s,&ba,old,old_length,1u)==CLEAN_BA_NO_AGREEMENT);
    ba_open(&ba,&s,3u,8u);
    assert(clean_wpa_ba_receive(&s,&ba,old,old_length,2u)==CLEAN_BA_SINK_ERROR &&
           s.error==WPA_MIC && !s.rx_pn[3][0]);
    ba_wiped(&s);

    s=before;
    assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
    ba_open(&ba,&s,3u,0u);
    n=ba_protected_frame(&s,m,3u,1u,1u);
    assert(clean_wpa_ba_receive(&s,&ba,m,n,0u)==CLEAN_BA_ACCEPTED);
    assert(clean_wpa_ampdu_begin(&s,12u)==CLEAN_BA_ACCEPTED);
    assert(clean_wpa_ampdu_feed(&s,&ba,&stream,wire,2u,&used,0u)==CLEAN_BA_ACCEPTED);
    assert(!clean_wpa_poll(&s,0u,tx,&length));
    assert(clean_wpa_poll(&s,CLEAN_WPA_DEADLINE_US,tx,&length)==-1 &&
           s.error==WPA_TIMEOUT);
    ba_wiped(&s);

    for (uint32_t path=0u;path<3u;++path) {
        s=before;
        assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
        ba_open(&ba,&s,3u,0u);
        n=ba_protected_frame(&s,m,3u,path==2u?0u:1u,1u);
        m[n-12u]^=1u;
        badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
        int result;
        if (path==2u) {
            size=ba_append(wire,0u,m,n,1);
            assert(clean_wpa_ampdu_begin(&s,size)==CLEAN_BA_ACCEPTED);
            result=clean_wpa_ampdu_feed(&s,&ba,&stream,wire,size,&used,1u);
            assert(used==size);
        } else {
            assert(clean_wpa_ba_receive(&s,&ba,m,n,1u)==CLEAN_BA_ACCEPTED);
            if (!path) result=clean_wpa_ba_poll(&s,&ba,1u+CLEAN_BA_GAP_US);
            else {
                uint8_t bar[24]={0x84u};
                memcpy(bar+4u,s.mac,6u); memcpy(bar+10u,s.bssid,6u);
                bar[16]=4u; bar[17]=0x30u; bar[18]=0x10u;
                badger_bytes_store_u32(bar+20u,diag_crc32(bar,20u));
                result=clean_wpa_ba_bar(&s,&ba,bar,sizeof(bar),2u);
            }
        }
        assert(result==CLEAN_BA_SINK_ERROR && s.error==WPA_MIC && !s.installed);
        ba_wiped(&s);
    }
    puts("D2 BA lifecycle: explicit binding, bounded pool pressure, lossless retry, repeated variable-length software PPDUs, abort and deferred MIC-failure wiping pass; NOT live station/PSM integration");
}

static uint32_t group_key(clean_wpa_t *s, uint8_t m[512], uint8_t replay,
                           uint8_t id, uint8_t value, uint8_t rsc) {
    (void)incoming(s,m,1);
    uint8_t *e=m+32u, plain[24]={221u,22u,0u,15u,172u,1u,0u,0u};
    plain[6]=id; memset(plain+8u,value,16u);
    e[3]=127u; e[5]=0x13u; e[6]=0x82u; e[16]=replay;
    memset(e+17u,0,64u); e[65]=rsc;
    e[98]=(uint8_t)wrap_size(s->ptk+16u,plain,sizeof(plain),e+99u);
    sign_key(s,m,167u);
    return 167u;
}

static uint32_t group_frame(clean_wpa_t *s, uint8_t *m, uint8_t id,
                             uint8_t tid, uint8_t pn, int broadcast) {
    uint8_t plain[32]={170u,170u,3u,0u,0u,0u,136u,181u,'U','2','6','W','P','A','2',1u};
    memcpy(plain+16u,s->probe_token,16u);
    uint32_t n=protected_qos_fragment(s,m,tid,0u,0u,0,pn,plain,32u);
    uint32_t h=tid==16u?24u:26u;
    memset(m+4u,255,6u);
    if (!broadcast) { m[4]=1u; m[5]=0u; m[6]=94u; m[7]=0u; m[8]=0u; m[9]=1u; }
    m[h+3u]=(uint8_t)(32u|(id<<6u));
    uint8_t number[6]={pn};
    assert(wpa_ccmp(s->group[id].key,m,h,number,plain,32u,m+h+8u,m+h+40u,0));
    badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    return n;
}

static uint32_t encrypt_key(uint8_t *m, uint32_t n, const uint8_t key[16],
                             uint8_t pn) {
    uint8_t plain[400], number[6]={pn};
    uint32_t size=n-28u;
    memcpy(plain,m+24u,size); m[1]|=64u;
    memset(m+24u,0,8u); m[24]=pn; m[27]=32u;
    assert(wpa_ccmp(key,m,24u,number,plain,size,m+32u,m+32u+size,0));
    badger_bytes_store_u32(m+n+12u,diag_crc32(m,n+12u));
    return n+16u;
}

static void encrypted_response(const uint8_t *frame, uint32_t size,
        const uint8_t tk[16], const uint8_t kck[16], uint16_t info) {
    const uint8_t *m=frame+6u;
    assert(m[1]==0x41u && clean_frame_valid(m,size-6u));
    uint8_t plain[160], tag[8], digest[20], mic[16];
    uint8_t pn[6]={m[24],m[25],m[28],m[29],m[30],m[31]};
    uint32_t length=size-50u;
    memcpy(tag,m+32u+length,8u);
    assert(wpa_ccmp(tk,m,24u,pn,m+32u,length,plain,tag,1));
    uint8_t *e=plain+8u;
    assert((((uint16_t)e[5]<<8u)|e[6])==info);
    memcpy(mic,e+81u,16u); memset(e+81u,0,16u);
    wpa_hmac(kck,16u,e,length-8u,digest);
    assert(!memcmp(mic,digest,16u));
}

static void canonical_block_ack(clean_wpa_t before) {
    static clean_ba_rx_t ba;
    static clean_ampdu_stream_t stream;
    static clean_mac_rx_t station;
    static clean_qos_rx_t qos;
    clean_wpa_t s=before;
    uint8_t m[512], first[512], tx[512], packet[530], snapshot[512];
    uint32_t n, length, size;
    s.stage=WPA_PROTECTED;
    assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
    ba_open(&ba,&s,3u,4095u);
    n=ba_protected_frame(&s,m,3u,0u,2u);
    memcpy(packet+18u,m,n); size=n+18u;
    assert(clean_station_qos_rx(&station,&qos,packet,&size,s.mac,s.bssid,0u)==
           CLEAN_MAC_PROTECTED && size==n+18u);
    assert(!memcmp(packet+18u,m,n) && !qos.tid[3].delivered);
    assert(!clean_wpa_receive(&s,packet+18u,n,tx,&length) && !length);
    assert(!s.protected_rx && !s.rx_pn[3][0] && ba.accepted==1u);
    assert(!clean_wpa_receive(&s,m,n,tx,&length) && ba.duplicates==1u);
    n=ba_protected_frame(&s,first,3u,4095u,1u);
    first[24]&=(uint8_t)~0x60u;
    badger_bytes_store_u32(first+n-4u,diag_crc32(first,n-4u));
    assert(!clean_wpa_receive(&s,first,n,tx,&length));
    assert(ba.delivered==2u && s.protected_rx==2u && s.rx_pn[3][0]==2u);
    n=ba_protected_frame(&s,m,3u,2u,4u);
    assert(!clean_wpa_receive(&s,m,n,tx,&length));
    assert(!clean_wpa_poll(&s,0xfffffff0u,0,&length) && !length);
    assert(ba.delivered==2u);
    assert(!clean_wpa_poll(&s,0xfffffff0u,tx,&length));
    assert(ba.delivered==3u && s.rx_pn[3][0]==4u);
    n=ba_protected_frame(&s,m,3u,4u,6u);
    assert(!clean_wpa_receive(&s,m,n,tx,&length));
    uint8_t bar[24]={0};
    bar[0]=0x84u; bar[16]=4u; bar[17]=0x30u; bar[18]=0x40u;
    memcpy(bar+4u,s.mac,6u); memcpy(bar+10u,s.bssid,6u);
    badger_bytes_store_u32(bar+20u,diag_crc32(bar,20u));
    memcpy(packet+18u,bar,sizeof(bar)); size=18u+sizeof(bar);
    assert(clean_station_qos_rx(&station,&qos,packet,&size,s.mac,s.bssid,s.clock)==
           CLEAN_MAC_IGNORED && size==42u);
    assert(!clean_wpa_receive(&s,packet+18u,sizeof(bar),tx,&length));
    assert(ba.delivered==4u && s.rx_pn[3][0]==6u);
    n=ba_protected_frame(&s,m,3u,5u,7u);
    m[n-12u]^=1u;
    badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_receive(&s,m,n,tx,&length)==-1 && s.error==WPA_MIC);
    ba_wiped(&s);

    s=before; s.stage=WPA_PROTECTED;
    assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
    for (uint8_t tid=0u;tid<4u;++tid) {
        ba_open(&ba,&s,tid,0u);
        for (uint8_t seq=1u;seq<=4u;++seq) {
            n=ba_protected_frame(&s,m,tid,seq,seq);
            assert(!clean_wpa_receive(&s,m,n,tx,&length));
        }
    }
    n=ba_protected_frame(&s,m,0u,5u,5u);
    memcpy(snapshot,m,n);
    assert(clean_wpa_receive(&s,m,n,tx,&length)==CLEAN_WPA_RX_RETRY && !length);
    assert(!memcmp(snapshot,m,n) && ba.accepted==CLEAN_BA_SLOTS && !ba.delivered);
    assert(!s.rx_pn[0][0] && !s.error);
    assert(!clean_wpa_poll(&s,CLEAN_BA_GAP_US,tx,&length));
    assert(ba.delivered==CLEAN_BA_SLOTS && s.rx_pn[0][0]==4u);
    assert(!clean_wpa_receive(&s,m,n,tx,&length));
    assert(ba.delivered==CLEAN_BA_SLOTS+1u && s.rx_pn[0][0]==5u);
    assert(!memcmp(snapshot,m,n) && !s.error);
    assert(clean_wpa_ba_unbind(&s)==CLEAN_BA_ACCEPTED);

    s=before; s.stage=WPA_PROTECTED;
    assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
    ba_open(&ba,&s,3u,0u);
    ba.tid[3].timeout_us=10u;
    ba.tid[3].last_rx=0xfffffffau;
    assert(!clean_wpa_poll(&s,4u,tx,&length));
    assert(!ba.tid[3].active && ba.expired==1u && !s.error);
    assert(clean_wpa_ba_unbind(&s)==CLEAN_BA_ACCEPTED);
    for (uint32_t path=0u;path<2u;++path) {
        s=before; s.stage=WPA_PROTECTED;
        assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
        ba_open(&ba,&s,3u,0u);
        n=ba_protected_frame(&s,m,3u,1u,1u);
        assert(!clean_wpa_receive(&s,m,n,tx,&length));
        ba.peer[0]^=2u;
        int result=path?clean_wpa_poll(&s,1u,tx,&length):
                        clean_wpa_receive(&s,m,n,tx,&length);
        assert(result==-1 && s.error==WPA_FORMAT);
        ba_wiped(&s);
    }
    s=before; s.stage=WPA_PROTECTED;
    uint8_t partial[16]={0};
    n=protected_qos_fragment(&s,m,3u,0u,0u,1,1u,partial,sizeof(partial));
    assert(!clean_wpa_receive(&s,m,n,tx,&length) && s.rx_qos.tid[3].active);
    assert(s.qos_fragment_pn[3][0]==1u);
    assert(!clean_wpa_poll(&s,CLEAN_MAC_RX_LIFETIME_US,0,&length));
    assert(!s.rx_qos.tid[3].active && s.rx_qos.tid[3].expired==1u);
    for (uint32_t i=0u;i<6u;++i) assert(!s.qos_fragment_pn[3][i]);
    qos_plaintext_wiped(&s);
    puts("Canonical WPA RX: station preprocessing, ciphertext reorder, normal/BA ACK policies, BAR, poll/gap/timeout service, exact pool pressure/retry and MIC wiping pass; NOT hardware BA");
}

static uint32_t ba_key_frame(clean_wpa_t *s, uint8_t *out, const uint8_t *key,
                              uint32_t bytes, uint16_t seq, uint8_t pn) {
    uint32_t n=protected_qos_fragment(s,out,3u,seq,0u,0,pn,key+24u,bytes-28u);
    out[24]|=0x60u;
    badger_bytes_store_u32(out+n-4u,diag_crc32(out,n-4u));
    return n;
}

static void ordered_key_replies(clean_wpa_t before) {
    static clean_ba_rx_t ba;
    static clean_ampdu_stream_t stream;
    clean_wpa_t s=before;
    uint8_t key[512], m[512], tail[512], tx[512], saved[512], wire[1024], bar[24];
    uint32_t n, tail_n, length, bytes, used;
    for (uint32_t path=0u;path<4u;++path) {
        s=before; s.stage=WPA_PROTECTED;
        assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
        ba_open(&ba,&s,3u,path==3u?9u:10u);
        bytes=group_key(&s,key,4u,3u,21u,0u);
        tail_n=ba_key_frame(&s,tail,key,bytes,11u,2u);
        bytes=group_key(&s,key,3u,2u,20u,0u);
        n=ba_key_frame(&s,m,key,bytes,10u,1u);
        if (path==0u) {
            assert(!clean_wpa_receive(&s,tail,tail_n,tx,&length));
            assert(clean_wpa_receive(&s,m,n,tx,&length)==1 && length);
        } else {
            if (path==1u) {
                assert(clean_wpa_ba_receive(&s,&ba,tail,tail_n,1u)==CLEAN_BA_ACCEPTED);
                assert(clean_wpa_ba_receive(&s,&ba,m,n,2u)==CLEAN_BA_ACCEPTED);
            } else if (path==2u) {
                bytes=ba_append(wire,0u,tail,tail_n,0);
                bytes=ba_append(wire,bytes,m,n,1);
                assert(clean_wpa_ampdu_begin(&s,bytes)==CLEAN_BA_ACCEPTED);
                assert(clean_wpa_ampdu_feed(&s,&ba,&stream,wire,bytes,&used,2u)==
                       CLEAN_BA_ACCEPTED && used==bytes);
            } else {
                assert(!clean_wpa_receive(&s,tail,tail_n,tx,&length));
                assert(!clean_wpa_receive(&s,m,n,tx,&length));
                assert(!ba.delivered && !s.rx_pn[3][0]);
                memset(bar,0,sizeof(bar));
                bar[0]=0x84u; bar[16]=4u; bar[17]=0x30u; bar[18]=0xa0u;
                memcpy(bar+4u,s.mac,6u); memcpy(bar+10u,s.bssid,6u);
                badger_bytes_store_u32(bar+20u,diag_crc32(bar,20u));
                assert(clean_wpa_receive(&s,bar,sizeof(bar),tx,&length)==
                       CLEAN_WPA_RX_RETRY && !length);
            }
            assert(s.ba_reply_length && ba.delivered==1u && s.rx_pn[3][0]==1u);
            memcpy(saved,s.ba_reply,sizeof(saved));
            assert(!clean_wpa_poll(&s,3u,0,&length) && !length);
            assert(!memcmp(saved,s.ba_reply,sizeof(saved)) && ba.delivered==1u);
            assert(clean_wpa_receive(&s,tail,tail_n,tx,&length)==CLEAN_WPA_RX_RETRY);
            assert(!length && ba.accepted==2u && ba.delivered==1u);
            assert(clean_wpa_poll(&s,4u,tx,&length)==1 && length);
        }
        assert(ba.delivered==1u && s.rx_pn[3][0]==1u && s.group_rekeys==1u);
        encrypted_response(tx,length,s.ptk+32u,s.ptk,0x302u);
        for (uint32_t i=0u;i<sizeof(s.ba_reply);++i) assert(!s.ba_reply[i]);
        clean_wpa_tx_complete(&s);
        assert(s.group_responses==1u);
        assert(!clean_wpa_poll(&s,5u,0,&length) && !length && ba.delivered==1u);
        assert(clean_wpa_poll(&s,6u,tx,&length)==1 && length);
        assert(ba.delivered==2u && s.rx_pn[3][0]==2u && s.group_rekeys==2u);
        encrypted_response(tx,length,s.ptk+32u,s.ptk,0x302u);
        clean_wpa_tx_complete(&s);
        assert(s.group_responses==2u && !s.error && !s.ba_reply_length);
        assert(!clean_wpa_poll(&s,7u,tx,&length) && !length);
        if (path==3u) {
            memcpy(saved,bar,sizeof(bar));
            assert(!clean_wpa_receive(&s,bar,sizeof(bar),tx,&length));
            assert(!memcmp(saved,bar,sizeof(bar)) && ba.delivered==2u);
        }
        assert(clean_wpa_ba_unbind(&s)==CLEAN_BA_ACCEPTED);
    }
    for (uint32_t unbind=0u;unbind<2u;++unbind) {
        s=before; s.stage=WPA_PROTECTED;
        assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
        ba_open(&ba,&s,3u,10u);
        bytes=group_key(&s,key,3u,2u,20u,0u);
        n=ba_key_frame(&s,m,key,bytes,10u,1u);
        assert(clean_wpa_ba_receive(&s,&ba,m,n,1u)==CLEAN_BA_ACCEPTED &&
               s.ba_reply_length);
        assert((unbind?clean_wpa_ba_unbind(&s):clean_wpa_ba_reset(&s))==
               CLEAN_BA_ACCEPTED);
        assert(s.ba_reply_length && !ba.tid[3].active);
        assert(clean_wpa_poll(&s,2u,tx,&length)==1 && length && !s.ba_reply_length);
        encrypted_response(tx,length,s.ptk+32u,s.ptk,0x302u);
        assert(clean_wpa_abort(&s,WPA_TRANSPORT)==-1);
    }
    s=before; s.stage=WPA_PROTECTED;
    assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
    ba_open(&ba,&s,3u,10u);
    tail_n=ba_protected_frame(&s,tail,3u,11u,2u);
    assert(!clean_wpa_receive(&s,tail,tail_n,tx,&length));
    bytes=incoming(&s,key,0);
    key[48]=3u; memset(key+49u,10,32u);
    badger_bytes_store_u32(key+bytes-4u,diag_crc32(key,bytes-4u));
    n=ba_key_frame(&s,m,key,bytes,10u,1u);
    memcpy(saved,s.ptk,48u);
    assert(clean_wpa_receive(&s,m,n,tx,&length)==1 && length && s.rekey);
    assert(s.rx_pn[3][0]==1u && !s.protected_rx);
    encrypted_response(tx,length,saved+32u,s.ptk,0x10au);
    ba_wiped(&s);
    assert(!s.ba_reply_length && !s.error);
    assert(clean_wpa_ba_unbind(&s)==CLEAN_BA_ACCEPTED);

    s=before; s.stage=WPA_PROTECTED;
    assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
    ba_open(&ba,&s,3u,10u);
    bytes=group_key(&s,key,3u,2u,20u,0u);
    n=ba_key_frame(&s,m,key,bytes,10u,1u);
    assert(clean_wpa_ba_receive(&s,&ba,m,n,1u)==CLEAN_BA_ACCEPTED && s.ba_reply_length);
    assert(clean_wpa_abort(&s,WPA_TRANSPORT)==-1 && !s.ba_reply_length);
    for (uint32_t i=0u;i<sizeof(s.ba_reply);++i) assert(!s.ba_reply[i]);
    ba_wiped(&s);
    s=before; s.stage=WPA_PROTECTED;
    assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
    ba_open(&ba,&s,3u,10u);
    bytes=incoming(&s,key,0);
    key[48]=3u; memset(key+49u,10,32u);
    badger_bytes_store_u32(key+bytes-4u,diag_crc32(key,bytes-4u));
    n=ba_key_frame(&s,m,key,bytes,10u,1u);
    assert(clean_wpa_ba_receive(&s,&ba,m,n,1u)==CLEAN_BA_ACCEPTED &&
           s.ba_reply_length && s.rekey);
    assert(clean_wpa_poll(&s,1u+CLEAN_WPA_DEADLINE_US,0,&length)==-1 &&
           !length && s.error==WPA_TIMEOUT && !s.ba_reply_length);
    for (uint32_t i=0u;i<sizeof(s.ba_reply);++i) assert(!s.ba_reply[i]);
    ba_wiped(&s);
    puts("Ordered EAPOL: canonical/direct/stream/BAR ingress, one-reply pressure, paused TX/deadline, two GTK replies, BA-only reset/unbind, PTK reset without stale delivery and abort wiping pass; NOT RF");
}

static void group_rekey(clean_wpa_t before) {
    static clean_ba_rx_t ba;
    static clean_ampdu_stream_t stream;
    uint8_t m[512], tx[512];
    uint32_t length,n;
    clean_wpa_t s=before;
    s.stage=WPA_PROTECTED; s.clock=100u;
    assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
    ba_open(&ba,&s,3u,10u);
    n=ba_protected_frame(&s,m,3u,11u,2u);
    assert(clean_wpa_ba_receive(&s,&ba,m,n,100u)==CLEAN_BA_ACCEPTED);
    for (uint32_t cycle=0u;cycle<3u;++cycle) {
        uint8_t id=(uint8_t)(2u+(cycle&1u)), value=(uint8_t)(20u+cycle);
        n=group_key(&s,m,(uint8_t)(3u+cycle),id,value,7u);
        assert(clean_wpa_receive(&s,m,n,tx,&length)==1);
        assert(s.group_rekeys==cycle+1u && s.group[id].valid && s.gtk_id==id);
        encrypted_response(tx,length,s.ptk+32u,s.ptk,0x302u);
        clean_wpa_tx_complete(&s);
        assert(s.group_responses==cycle+1u && s.stage==WPA_PROTECTED);
        clean_wpa_t installed=s;
        assert(clean_wpa_receive(&s,m,n,tx,&length)==1);
        assert(!memcmp(s.group,installed.group,sizeof(s.group)) &&
               s.group_rekeys==installed.group_rekeys);
        for (uint8_t tid=0u;tid<17u;++tid) {
            n=group_frame(&s,m,id,tid,7u,1);
            assert(!clean_wpa_protected(&s,m,n,101u) && !s.error);
            n=group_frame(&s,m,id,tid,8u,1);
            assert(clean_wpa_protected(&s,m,n,102u)==1 && !s.error);
            assert(!clean_wpa_protected(&s,m,n,103u));
            n=group_frame(&s,m,id,tid,9u,0);
            assert(clean_wpa_protected(&s,m,n,104u)==1);
            assert(s.group[id].pn[tid][0]==9u && !s.rx_pn[tid][0]);
        }
        n=group_key(&s,m,(uint8_t)(3u+cycle),id,value,7u);
        assert(clean_wpa_receive(&s,m,n,tx,&length)==1 &&
               s.group[id].pn[16][0]==9u);
        assert(ba.tid[3].active && ba.accepted==1u && !ba.delivered);
    }
    assert(s.group_rx==102u && s.broadcast_rx==51u && s.multicast_rx==51u &&
           s.group_verified==3u);
    n=group_frame(&s,m,2u,16u,10u,1);
    assert(clean_wpa_protected(&s,m,n,100u+CLEAN_WPA_GTK_OVERLAP_US-1u)==1);
    n=group_frame(&s,m,2u,16u,11u,1);
    /* Slot 3, not the active slot 2, expires at the exact overlap boundary. */
    n=group_frame(&s,m,3u,16u,10u,1);
    assert(!clean_wpa_protected(&s,m,n,100u+CLEAN_WPA_GTK_OVERLAP_US));
    assert(!s.group[3].valid && s.group[2].valid);
    for (uint32_t i=0u;i<16u;++i) assert(!s.group[3].key[i]);
    assert(clean_wpa_ba_unbind(&s)==CLEAN_BA_ACCEPTED);

    s=before; s.stage=WPA_PROTECTED;
    n=group_key(&s,m,3u,2u,20u,0u);
    n=encrypt_key(m,n,s.ptk+32u,1u);
    assert(clean_wpa_receive(&s,m,n,tx,&length)==1 && s.rx_pn[16][0]==1u);
    assert(!clean_wpa_receive(&s,m,n,tx,&length) && s.group_rekeys==1u);
    n=group_frame(&s,m,2u,16u,1u,1);
    assert(clean_wpa_receive(&s,m,n,tx,&length)==0 && s.group_rx==1u);
    n=group_key(&s,m,4u,2u,20u,10u);
    n=encrypt_key(m,n,s.ptk+32u,2u);
    assert(clean_wpa_receive(&s,m,n,tx,&length)==1 && s.group_rekeys==1u &&
           s.group[2].pn[16][0]==10u && s.rx_pn[16][0]==2u);
    n=group_frame(&s,m,2u,16u,9u,1);
    assert(!clean_wpa_protected(&s,m,n,1u) && s.group_rx==1u);
    n=group_frame(&s,m,2u,16u,11u,1);
    assert(clean_wpa_protected(&s,m,n,2u)==1 && s.group_rx==2u);
    s=before; s.stage=WPA_PROTECTED;
    n=group_key(&s,m,3u,2u,4u,0u);
    assert(clean_wpa_receive(&s,m,n,tx,&length)==-1 && s.error==WPA_REPLAY);
    s=before; s.stage=WPA_PROTECTED;
    n=group_key(&s,m,3u,2u,20u,0u);
    assert(clean_wpa_receive(&s,m,n,tx,&length)==1);
    n=group_key(&s,m,3u,2u,21u,0u);
    assert(clean_wpa_receive(&s,m,n,tx,&length)==-1 && s.error==WPA_REPLAY);

    for (uint32_t fault=0u;fault<7u;++fault) {
        s=before; s.stage=WPA_PROTECTED;
        n=group_key(&s,m,3u,2u,20u,0u);
        assert(clean_wpa_receive(&s,m,n,tx,&length)==1);
        n=group_frame(&s,m,2u,16u,1u,1);
        if (fault==0u) m[n-12u]^=1u;
        if (fault==1u) m[27]=32u;
        if (fault==2u) m[27]|=1u;
        if (fault==3u) m[4]=2u;
        if (fault==4u) m[10]^=1u;
        if (fault==5u) m[1]&=(uint8_t)~64u;
        if (fault==6u) m[27]=0xe0u;
        badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
        int result=clean_wpa_receive(&s,m,n,tx,&length);
        assert(result<=0 && !s.group_rx && !s.protected_rx && !length);
        if (fault==0u || fault==2u) {
            assert(s.error==(fault==0u?WPA_MIC:WPA_FORMAT) && !s.installed);
            for (uint32_t i=0u;i<sizeof(s.group);++i) assert(!((uint8_t *)s.group)[i]);
        } else assert(!s.error);
    }
    s=before; s.stage=WPA_PROTECTED;
    n=group_key(&s,m,3u,2u,20u,0u);
    assert(clean_wpa_receive(&s,m,n,tx,&length)==1);
    for (uint32_t i=0u;i<CLEAN_WPA_MAX_RETRIES;++i)
        assert(clean_wpa_receive(&s,m,n,tx,&length)==1);
    assert(clean_wpa_receive(&s,m,n,tx,&length)==-1 && s.error==WPA_TIMEOUT);
    s=before; s.stage=WPA_PROTECTED;
    n=group_key(&s,m,3u,2u,20u,0u);
    assert(clean_wpa_receive(&s,m,n,tx,&length)==1);
    s.clock=CLEAN_WPA_DEADLINE_US;
    assert(clean_wpa_receive(&s,m,n,tx,&length)==-1 && s.error==WPA_TIMEOUT);
    puts("E2 GTK: three replacements, broadcast/multicast on all 17 replay lanes, RSC, key-ID/MIC/plaintext rejection, duplicate PN preservation, bounded overlap and retries pass; NOT RF");
}

static void pairwise_rekey(clean_wpa_t before) {
    static clean_ba_rx_t ba;
    static clean_ampdu_stream_t stream;
    uint8_t m[512], tx[512], old_ptk[48], old_nonce[32], plain[48];
    uint32_t length,n;
    clean_wpa_t s=before;
    for (uint32_t i=0u;i<32u;++i) assert(before.pmk[i]==1u);
    s.stage=WPA_PROTECTED;
    assert(clean_wpa_ba_bind(&s,&ba,&stream)==CLEAN_BA_ACCEPTED);
    for (uint8_t cycle=0u;cycle<3u;++cycle) {
        s.clock=100u+(uint32_t)cycle*10000000u;
        ba_open(&ba,&s,3u,10u);
        n=ba_protected_frame(&s,m,3u,11u,2u);
        assert(clean_wpa_ba_receive(&s,&ba,m,n,s.clock)==CLEAN_BA_ACCEPTED);
        uint32_t used;
        const uint8_t prefix[2]={0u};
        assert(clean_wpa_ampdu_begin(&s,12u)==CLEAN_BA_ACCEPTED);
        assert(clean_wpa_ampdu_feed(&s,&ba,&stream,prefix,sizeof(prefix),&used,s.clock)==
               CLEAN_BA_ACCEPTED && used==sizeof(prefix));
        memcpy(old_ptk,s.ptk,48u); memcpy(old_nonce,s.snonce,32u);
        s.tx_pn[0]=50u; s.rx_pn[16][0]=50u; s.group[s.gtk_id].pn[16][0]=10u;
        n=incoming(&s,m,0); m[48]=(uint8_t)(3u+2u*cycle);
        memset(m+49u,10+cycle,32u);
        badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
        n=encrypt_key(m,n,old_ptk+32u,51u);
        assert(clean_wpa_receive(&s,m,n,tx,&length)==1 && s.rekey);
        ba_wiped(&s);
        assert(clean_wpa_ba_poll(&s,&ba,s.clock)==CLEAN_BA_INVALID);
        assert(clean_wpa_ampdu_begin(&s,12u)==CLEAN_BA_INVALID);
        assert(memcmp(old_nonce,s.snonce,32u) && memcmp(old_ptk,s.ptk,48u));
        uint8_t pmk[32], derived[48];
        memset(pmk,1,32u);
        wpa_ptk(pmk,s.bssid,s.mac,s.anonce,s.snonce,derived);
        assert(!memcmp(derived,s.ptk,48u));
        assert(!memcmp(old_ptk,s.old_ptk,48u) && s.tx_pn[0]==51u);
        encrypted_response(tx,length,old_ptk+32u,s.ptk,0x10au);
        memcpy(old_nonce,s.snonce,32u);
        clean_wpa_tx_complete(&s);
        assert(s.m2==cycle+1u);
        assert(!clean_wpa_poll(&s,s.clock+CLEAN_WPA_RETRY_US-1u,tx,&length));
        assert(clean_wpa_poll(&s,s.clock+1u,tx,&length)==1);
        encrypted_response(tx,length,old_ptk+32u,s.ptk,0x10au);
        assert(!memcmp(old_nonce,s.snonce,32u) && s.rx_pn[16][0]==51u);
        n=incoming(&s,m,1);
        assert(wpa_unwrap(s.ptk+16u,m+131u,56u,plain));
        plain[28]=s.gtk_id; memcpy(plain+30u,s.gtk,16u);
        wrap(s.ptk+16u,plain,m+131u);
        m[48]=(uint8_t)(4u+2u*cycle); memcpy(m+49u,s.anonce,32u);
        sign_key(&s,m,n);
        n=encrypt_key(m,n,old_ptk+32u,52u);
        assert(clean_wpa_receive(&s,m,n,tx,&length)==1 && !s.rekey);
        encrypted_response(tx,length,old_ptk+32u,s.ptk,0x30au);
        assert(s.pairwise_rekeys==cycle+1u && !s.tx_pn[0] && !s.rx_pn[16][0]);
        assert(s.group[s.gtk_id].pn[16][0]==10u);
        assert(s.old_valid && !memcmp(s.old_ptk,old_ptk,48u));
        ba_wiped(&s);
        ba_open(&ba,&s,3u,10u);
        uint8_t queued[512];
        uint32_t qn=ba_protected_frame(&s,queued,3u,11u,2u);
        assert(clean_wpa_ba_receive(&s,&ba,queued,qn,s.clock)==CLEAN_BA_ACCEPTED);
        assert(!clean_wpa_receive(&s,m,n,tx,&length));
        uint8_t retry[512]; memcpy(retry,m,n);
        uint8_t retry_plain[400], tag[8], number[6]={52u};
        memcpy(tag,retry+n-12u,8u);
        assert(wpa_ccmp(old_ptk+32u,retry,24u,number,retry+32u,n-44u,
                        retry_plain,tag,1));
        number[0]=53u; retry[24]=53u;
        assert(wpa_ccmp(old_ptk+32u,retry,24u,number,retry_plain,n-44u,
                        retry+32u,retry+n-12u,0));
        badger_bytes_store_u32(retry+n-4u,diag_crc32(retry,n-4u));
        assert(clean_wpa_receive(&s,retry,n,tx,&length)==1 && !s.tx_pn[0] &&
               !s.rx_pn[16][0] && s.pairwise_rekeys==cycle+1u);
        encrypted_response(tx,length,old_ptk+32u,s.ptk,0x30au);
        assert(ba.tid[3].active && ba.accepted==1u && !ba.delivered);
        clean_wpa_tx_complete(&s);
        assert(s.m4==cycle+1u && s.stage==WPA_M4_SENT);
        uint8_t reply[32]={170u,170u,3u,0u,0u,0u,136u,181u,'U','2','6','W','P','A','2',1u};
        memcpy(reply+16u,s.probe_token,16u);
        n=protected_fragment(&s,m,cycle,0u,0,1u,reply,32u);
        assert(clean_wpa_receive(&s,m,n,tx,&length)==0 && !length);
        assert(s.stage==WPA_PROTECTED && s.protected_rx==cycle+1u);
        assert(s.pairwise_verified==cycle+1u && !s.old_valid);
        for (uint32_t i=0u;i<48u;++i) assert(!s.old_ptk[i]);
        assert(clean_wpa_probe(&s,tx) && s.tx_pn[0]==1u);
        assert(clean_wpa_ba_reset(&s)==CLEAN_BA_ACCEPTED);
        ba_wiped(&s);
    }
    assert(clean_wpa_ba_unbind(&s)==CLEAN_BA_ACCEPTED);
    s=before; s.stage=WPA_PROTECTED; s.clock=0xffff0000u;
    n=incoming(&s,m,0); m[48]=3u; memset(m+49u,10,32u);
    badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_receive(&s,m,n,tx,&length)==1 && s.rekey);
    assert(clean_wpa_poll(&s,0xffff0000u+CLEAN_WPA_DEADLINE_US,tx,&length)==-1);
    assert(!s.installed && s.error==WPA_TIMEOUT);
    for (uint32_t i=0u;i<48u;++i) assert(!s.old_ptk[i] && !s.ptk[i]);
    for (uint32_t i=0u;i<32u;++i) assert(!s.pmk[i]);
    puts("E2 PTK: three encrypted four-way rekeys during traffic, fresh per-epoch SNonces, old-TK M2/M4 envelopes, new-KCK MICs, atomic PN epochs, unchanged GTK PN and wrap-safe failure wiping pass; NOT RF");
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
    protected_block_ack(s);
    canonical_block_ack(s);
    ordered_key_replies(s);
    protected_ba_lifecycle(s);
    protected_fragments(s);
    protected_qos(s);
    protected_aggregates(s);
    group_rekey(s);
    pairwise_rekey(s);
    s.tx_pn[0]=5u; s.rx_pn[0][0]=9u;
    assert(clean_wpa_receive(&s,m,n,tx,&size)==1 && s.tx_pn[0]==5u && s.rx_pn[0][0]==9u);
    clean_wpa_t installed=s;
    m[48]=3u; sign_key(&s,m,n);
    assert(clean_wpa_receive(&s,m,n,tx,&size)==1 && s.tx_pn[0]==5u &&
           s.rx_pn[0][0]==9u && s.replay[7]==3u &&
           !memcmp(s.ptk,installed.ptk,48u) && !memcmp(s.gtk,installed.gtk,16u));
    m[48]=2u; sign_key(&s,m,n);
    assert(clean_wpa_receive(&s,m,n,tx,&size)==-1 && s.error==WPA_REPLAY);
    s=installed; n=incoming(&s,m,1); m[48]=4u;
    m[32u+99u]^=1u; sign_key(&s,m,n);
    assert(clean_wpa_receive(&s,m,n,tx,&size)==-1 && s.error==WPA_UNWRAP && !s.installed);
    uint8_t plain[48];
    for (uint32_t change=0u;change<4u;++change) {
        s=installed; n=incoming(&s,m,1); m[48]=4u;
        assert(wpa_unwrap(s.ptk+16u,m+32u+99u,56u,plain));
        if (change==0u) plain[29]=0xf6u;
        if (change==1u) plain[28]|=0xf8u;
        if (change==2u) plain[30]^=1u;
        if (change==3u) plain[28]^=1u;
        wrap(s.ptk+16u,plain,m+32u+99u); sign_key(&s,m,n);
        if (change<2u) {
            assert(clean_wpa_receive(&s,m,n,tx,&size)==1 &&
                s.tx_pn[0]==5u && s.rx_pn[0][0]==9u &&
                !memcmp(s.ptk,installed.ptk,48u) && !memcmp(s.gtk,installed.gtk,16u));
        } else assert(clean_wpa_receive(&s,m,n,tx,&size)==-1 &&
                      s.error==WPA_REPLAY && !s.installed);
    }
    s=installed; n=incoming(&s,m,1); m[32u+81u]^=1u;
    badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_receive(&s,m,n,tx,&size)==-1 && s.error==WPA_MIC && !s.installed);
    s=installed; s.stage=WPA_PROTECTED;
    n=incoming(&s,m,1); m[48]=3u; sign_key(&s,m,n);
    assert(clean_wpa_receive(&s,m,n,tx,&size)==1 && s.stage==WPA_PROTECTED &&
           s.tx_pn[0]==5u && s.rx_pn[0][0]==9u);
    assert(!clean_wpa_poll(&s,0u,tx,&size));
    assert(!clean_wpa_poll(&s,CLEAN_WPA_DEADLINE_US,tx,&size));
    assert(clean_wpa_abort(&s,WPA_TRANSPORT)==-1 && !s.installed && s.error==WPA_TRANSPORT);
    for (uint32_t i=0u;i<48u;++i) assert(!s.ptk[i]);
    s=before; n=incoming(&s,m,1); m[32u+81u]^=1u;
    badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_receive(&s,m,n,tx,&size)==-1 && s.error==WPA_MIC && !s.installed);
    s=before; n=incoming(&s,m,1); m[48]=1u;
    badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_receive(&s,m,n,tx,&size)==-1 && s.error==WPA_REPLAY);
}

static void handshake_timers(void) {
    uint8_t secret[64], mac[6]={2u,6u,7u,8u,9u,10u}, ap[6]={2u,1u,2u,3u,4u,5u};
    memset(secret,1,32u); memset(secret+32u,3,32u);
    clean_wpa_t s;
    uint8_t m[512], tx[512], first[512]; uint32_t length, n;
    const uint32_t start=0xffff0000u;
    clean_wpa_init(&s,secret,mac,ap,rsn,22u);
    assert(!clean_wpa_poll(&s,start,tx,&length));
    assert(!clean_wpa_poll(&s,start+CLEAN_WPA_DEADLINE_US-1u,tx,&length));
    assert(clean_wpa_poll(&s,start+CLEAN_WPA_DEADLINE_US,tx,&length)==-1 && s.error==WPA_TIMEOUT);
    for (uint32_t i=0u;i<32u;++i) assert(!s.pmk[i] && !s.snonce[i]);
    for (uint32_t phase=0u;phase<2u;++phase) {
        clean_wpa_init(&s,secret,mac,ap,rsn,22u);
        assert(!clean_wpa_poll(&s,start,tx,&length));
        n=incoming(&s,m,0); assert(clean_wpa_receive(&s,m,n,tx,&length)==1);
        if (phase) {
            n=incoming(&s,m,1); assert(clean_wpa_receive(&s,m,n,tx,&length)==1);
            s.tx_pn[0]=42u; s.rx_pn[0][0]=17u; s.stage=WPA_M4_SENT;
        }
        memcpy(first,tx,length);
        for (uint32_t i=1u;i<=CLEAN_WPA_MAX_RETRIES;++i) {
            assert(!clean_wpa_poll(&s,start+i*CLEAN_WPA_RETRY_US-1u,tx,&length));
            assert(clean_wpa_poll(&s,start+i*CLEAN_WPA_RETRY_US,tx,&length)==1);
            assert(length==(phase?141u:163u) && !memcmp(first+38u,tx+38u,length-42u));
            assert(s.retransmissions==i && s.installed==phase);
            if (phase) assert(s.tx_pn[0]==42u && s.rx_pn[0][0]==17u);
            /* Incoming duplicates must not reset the local retry budget. */
            assert(clean_wpa_receive(&s,m,n,tx,&length)==1 && s.retries==i);
        }
        assert(clean_wpa_poll(&s,start+(CLEAN_WPA_MAX_RETRIES+1u)*CLEAN_WPA_RETRY_US,
                              tx,&length)==-1 && s.error==WPA_TIMEOUT && !s.installed);
        for (uint32_t i=0u;i<48u;++i) assert(!s.ptk[i]);
    }
    clean_wpa_init(&s,secret,mac,ap,rsn,22u);
    assert(!clean_wpa_poll(&s,start,tx,&length));
    n=incoming(&s,m,0); assert(clean_wpa_receive(&s,m,n,tx,&length)==1);
    uint8_t ptk[48]; memcpy(ptk,s.ptk,48u);
    m[48]=2u; badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_receive(&s,m,n,tx,&length)==1 && !memcmp(ptk,s.ptk,48u) && tx[54]==2u);
    m[48]=1u; badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_receive(&s,m,n,tx,&length)==-1 && s.error==WPA_REPLAY);
    clean_wpa_init(&s,secret,mac,ap,rsn,22u);
    n=incoming(&s,m,0); assert(clean_wpa_receive(&s,m,n,tx,&length)==1);
    m[49]^=1u; badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_receive(&s,m,n,tx,&length)==-1 && s.error==WPA_NONCE && !s.installed);
    clean_wpa_init(&s,secret,mac,ap,rsn,22u);
    n=incoming(&s,m,0); memset(m+49u,0,32u);
    badger_bytes_store_u32(m+n-4u,diag_crc32(m,n-4u));
    assert(clean_wpa_receive(&s,m,n,tx,&length)==-1 && s.error==WPA_NONCE);
    uint8_t bad_rsn[22]; memcpy(bad_rsn,rsn,22u); bad_rsn[19]=1u;
    clean_wpa_init(&s,secret,mac,ap,bad_rsn,22u);
    assert(s.error==WPA_RSN && !s.installed);
    memset(secret+32u,0,32u);
    clean_wpa_init(&s,secret,mac,ap,rsn,22u); assert(s.error==WPA_NONCE);
    puts("E1 handshake: M1/M3/M4 loss, bounded retries/deadline, timer wrap, duplicate budgets, advanced/stale replay, nonce/RSN rejection and PN preservation pass");
}

int main(void) {
    crypto(); provisioning(); aggregate_crypto(); binding(); handshake(); handshake_timers();
    puts("WPA2: independent crypto vectors, handshake, MIC/replay and no reinstall pass");
    puts("WPA2: authenticated 16-fragment RX, 2304-byte bounds, PN carry/continuity, expiry and plaintext wiping pass");
    return 0;
}

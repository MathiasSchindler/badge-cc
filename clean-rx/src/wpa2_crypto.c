#include "wpa2_crypto.h"
#include "frame_limits.h"
#include "../../src/shared/bytes.h"

void wpa_wipe(void *p, uint32_t n) {
    volatile uint8_t *b = p;
    while (n--) *b++ = 0u;
}

int wpa_equal(const uint8_t *a, const uint8_t *b, uint32_t n) {
    uint32_t difference = 0u;
    for (uint32_t i = 0u; i < n; ++i) difference |= a[i] ^ b[i];
    return difference == 0u;
}

static uint32_t rol(uint32_t v, uint32_t n) { return (v << n) | (v >> (32u - n)); }

static void sha_block(wpa_sha1_t *s) {
    uint32_t w[80];
    for (uint32_t i = 0u; i < 16u; ++i)
        w[i] = ((uint32_t)s->block[4u*i] << 24u) | ((uint32_t)s->block[4u*i+1u] << 16u) |
               ((uint32_t)s->block[4u*i+2u] << 8u) | s->block[4u*i+3u];
    for (uint32_t i = 16u; i < 80u; ++i) w[i] = rol(w[i-3u]^w[i-8u]^w[i-14u]^w[i-16u], 1u);
    uint32_t a=s->h[0], b=s->h[1], c=s->h[2], d=s->h[3], e=s->h[4];
    for (uint32_t i = 0u; i < 80u; ++i) {
        uint32_t f, k;
        if (i < 20u) { f=(b&c)|(~b&d); k=0x5a827999u; }
        else if (i < 40u) { f=b^c^d; k=0x6ed9eba1u; }
        else if (i < 60u) { f=(b&c)|(b&d)|(c&d); k=0x8f1bbcdcu; }
        else { f=b^c^d; k=0xca62c1d6u; }
        uint32_t t=rol(a,5u)+f+e+k+w[i];
        e=d; d=c; c=rol(b,30u); b=a; a=t;
    }
    s->h[0]+=a; s->h[1]+=b; s->h[2]+=c; s->h[3]+=d; s->h[4]+=e;
    wpa_wipe(w, sizeof(w));
}

void wpa_sha1_init(wpa_sha1_t *s) {
    static const uint32_t initial[5] = {0x67452301u,0xefcdab89u,0x98badcfeu,0x10325476u,0xc3d2e1f0u};
    for (uint32_t i=0u;i<5u;++i) s->h[i]=initial[i];
    s->bytes=0u;
}

void wpa_sha1_update(wpa_sha1_t *s, const uint8_t *p, uint32_t n) {
    while (n--) {
        s->block[s->bytes & 63u] = *p++;
        if ((++s->bytes & 63u) == 0u) sha_block(s);
    }
}

void wpa_sha1_final(wpa_sha1_t *s, uint8_t out[20]) {
    uint32_t bytes=s->bytes;
    uint8_t pad=128u, zero=0u, length[8];
    for (uint32_t i=0u;i<8u;++i) length[i]=0u;
    length[3]=(uint8_t)(bytes >> 29u);
    uint32_t bits=bytes << 3u;
    for (uint32_t i=0u;i<4u;++i) length[7u-i]=(uint8_t)(bits >> (8u*i));
    wpa_sha1_update(s,&pad,1u);
    while ((s->bytes & 63u) != 56u) wpa_sha1_update(s,&zero,1u);
    wpa_sha1_update(s,length,8u);
    for (uint32_t i=0u;i<20u;++i) out[i]=(uint8_t)(s->h[i/4u] >> (24u-8u*(i&3u)));
    wpa_wipe(s,sizeof(*s));
}

void wpa_sha1(const uint8_t *p, uint32_t n, uint8_t out[20]) {
    wpa_sha1_t s;
    wpa_sha1_init(&s); wpa_sha1_update(&s,p,n); wpa_sha1_final(&s,out);
}

void wpa_secret_binding(const uint8_t secret[64], const uint8_t bssid[6],
                         uint8_t channel, uint8_t out[20]) {
    wpa_sha1_t s;
    wpa_sha1_init(&s);
    wpa_sha1_update(&s,secret,64u);
    wpa_sha1_update(&s,bssid,6u);
    wpa_sha1_update(&s,&channel,1u);
    wpa_sha1_final(&s,out);
}

void wpa_hmac(const uint8_t *key, uint32_t kn, const uint8_t *p, uint32_t n, uint8_t out[20]) {
    uint8_t block[64], digest[20];
    badger_bytes_zero(block,sizeof(block));
    if (kn > 64u) { wpa_sha1(key,kn,block); kn=20u; }
    else badger_bytes_copy(block,key,kn);
    for (uint32_t i=0u;i<64u;++i) block[i]^=0x36u;
    wpa_sha1_t s;
    wpa_sha1_init(&s); wpa_sha1_update(&s,block,64u); wpa_sha1_update(&s,p,n); wpa_sha1_final(&s,digest);
    for (uint32_t i=0u;i<64u;++i) block[i]^=0x36u^0x5cu;
    wpa_sha1_init(&s); wpa_sha1_update(&s,block,64u); wpa_sha1_update(&s,digest,20u); wpa_sha1_final(&s,out);
    wpa_wipe(block,sizeof(block)); wpa_wipe(digest,sizeof(digest));
}

int wpa_pmk(const uint8_t *password, uint32_t n, const uint8_t *ssid, uint32_t sn, uint8_t pmk[32]) {
    if (n < 8u || n > 63u || !sn || sn > 32u) { wpa_wipe(pmk,32u); return 0; }
    uint8_t salt[36], u[20], t[20];
    badger_bytes_zero(salt,sizeof(salt));
    badger_bytes_copy(salt,ssid,sn);
    for (uint32_t block=1u;block<=2u;++block) {
        salt[sn+3u]=(uint8_t)block;
        wpa_hmac(password,n,salt,sn+4u,u);
        badger_bytes_copy(t,u,20u);
        for (uint32_t round=1u;round<4096u;++round) {
            wpa_hmac(password,n,u,20u,u);
            for (uint32_t i=0u;i<20u;++i) t[i]^=u[i];
        }
        badger_bytes_copy(pmk+(block-1u)*20u,t,block==1u?20u:12u);
    }
    wpa_wipe(salt,sizeof(salt)); wpa_wipe(u,sizeof(u)); wpa_wipe(t,sizeof(t));
    return 1;
}

static int hex_digit(uint8_t c) {
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}

int wpa_psk(const uint8_t *credential, uint32_t n, const uint8_t *ssid,
             uint32_t sn, uint8_t pmk[32]) {
    if (n!=64u) return wpa_pmk(credential,n,ssid,sn,pmk);
    wpa_wipe(pmk,32u);
    if (!sn || sn>32u) return 0;
    for (uint32_t i=0u;i<64u;++i) if (hex_digit(credential[i])<0) return 0;
    for (uint32_t i=0u;i<32u;++i)
        pmk[i]=(uint8_t)((hex_digit(credential[2u*i])<<4u)|hex_digit(credential[2u*i+1u]));
    return 1;
}

static int less(const uint8_t *a, const uint8_t *b, uint32_t n) {
    for (uint32_t i=0u;i<n;++i) if (a[i]!=b[i]) return a[i]<b[i];
    return 0;
}

void wpa_ptk(const uint8_t pmk[32], const uint8_t ap[6], const uint8_t sta[6],
             const uint8_t anonce[32], const uint8_t snonce[32], uint8_t ptk[48]) {
    static const uint8_t label[]="Pairwise key expansion";
    uint8_t input[100], digest[20];
    uint32_t at=sizeof(label);
    badger_bytes_copy(input,label,at);
    int order=less(ap,sta,6u);
    badger_bytes_copy(input+at,order?ap:sta,6u); at+=6u;
    badger_bytes_copy(input+at,order?sta:ap,6u); at+=6u;
    order=less(anonce,snonce,32u);
    badger_bytes_copy(input+at,order?anonce:snonce,32u); at+=32u;
    badger_bytes_copy(input+at,order?snonce:anonce,32u); at+=32u;
    for (uint32_t i=0u;i<3u;++i) {
        input[at]=(uint8_t)i;
        wpa_hmac(pmk,32u,input,at+1u,digest);
        badger_bytes_copy(ptk+20u*i,digest,i==2u?8u:20u);
    }
    wpa_wipe(input,sizeof(input)); wpa_wipe(digest,sizeof(digest));
}

static uint8_t mul(uint8_t a, uint8_t b) {
    uint8_t v=0u;
    for (uint32_t i=0u;i<8u;++i) {
        v^=(uint8_t)(a & (uint8_t)(0u-(b&1u)));
        a=(uint8_t)((a<<1u)^((a&128u)?0x1bu:0u)); b>>=1u;
    }
    return v;
}

void wpa_aes_init(wpa_aes_t *a, const uint8_t key[16]) {
    for (uint32_t x=0u;x<256u;++x) {
        uint8_t v=1u, p=(uint8_t)x;
        for (uint32_t bit=0u;bit<8u;++bit) {
            if (254u & (1u<<bit)) v=mul(v,p);
            p=mul(p,p);
        }
        uint8_t s=v;
        for (uint32_t i=1u;i<=4u;++i) s^=(uint8_t)((v<<i)|(v>>(8u-i)));
        s^=0x63u; a->box[x]=s; a->inverse[s]=(uint8_t)x;
    }
    badger_bytes_copy(a->round,key,16u);
    uint8_t rc=1u;
    for (uint32_t at=16u;at<176u;at+=4u) {
        uint8_t t[4];
        badger_bytes_copy(t,a->round+at-4u,4u);
        if (!(at&15u)) {
            uint8_t first=t[0];
            t[0]=a->box[t[1]]^rc; t[1]=a->box[t[2]]; t[2]=a->box[t[3]]; t[3]=a->box[first];
            rc=mul(rc,2u);
        }
        for (uint32_t j=0u;j<4u;++j) a->round[at+j]=a->round[at-16u+j]^t[j];
    }
}

static void aes_block(const wpa_aes_t *a, const uint8_t in[16], uint8_t out[16], int inverse) {
    uint8_t s[16], t[16];
    for (uint32_t i=0u;i<16u;++i) s[i]=in[i]^a->round[(inverse?160u:0u)+i];
    for (uint32_t round=1u;round<=10u;++round) {
        for (uint32_t col=0u;col<4u;++col)
            for (uint32_t row=0u;row<4u;++row) {
                uint32_t source=4u*((col+(inverse?4u-row:row))&3u)+row;
                t[4u*col+row]=inverse?a->inverse[s[source]]:a->box[s[source]];
            }
        if (inverse) for (uint32_t i=0u;i<16u;++i) t[i]^=a->round[(10u-round)*16u+i];
        for (uint32_t col=0u;col<4u;++col) {
            const uint8_t *p=t+4u*col;
            for (uint32_t row=0u;row<4u;++row) {
                uint8_t value=p[row];
                if (round!=10u) {
                    if (inverse) value=mul(p[row],14u)^mul(p[(row+1u)&3u],11u)^
                        mul(p[(row+2u)&3u],13u)^mul(p[(row+3u)&3u],9u);
                    else value=mul(p[row],2u)^mul(p[(row+1u)&3u],3u)^p[(row+2u)&3u]^p[(row+3u)&3u];
                }
                s[4u*col+row]=value;
            }
        }
        if (!inverse) for (uint32_t i=0u;i<16u;++i) s[i]^=a->round[16u*round+i];
    }
    badger_bytes_copy(out,s,16u); wpa_wipe(s,sizeof(s)); wpa_wipe(t,sizeof(t));
}

void wpa_aes_encrypt(const wpa_aes_t *a, const uint8_t in[16], uint8_t out[16]) {
    aes_block(a,in,out,0);
}

int wpa_unwrap(const uint8_t key[16], const uint8_t *in, uint32_t n, uint8_t *out) {
    if (n<24u || n>264u || (n&7u)) return 0;
    wpa_aes_t aes;
    wpa_aes_init(&aes,key);
    uint8_t a[8], block[16];
    badger_bytes_copy(a,in,8u); badger_bytes_copy(out,in+8u,n-8u);
    uint32_t count=n/8u-1u;
    for (uint32_t round=6u;round>0u;--round) for (uint32_t i=count;i>0u;--i) {
        badger_bytes_copy(block,a,8u);
        uint32_t t=count*(round-1u)+i;
        for (uint32_t b=0u;b<4u;++b) block[7u-b]^=(uint8_t)(t>>(8u*b));
        badger_bytes_copy(block+8u,out+8u*(i-1u),8u);
        aes_block(&aes,block,block,1);
        badger_bytes_copy(a,block,8u); badger_bytes_copy(out+8u*(i-1u),block+8u,8u);
    }
    uint8_t difference=0u;
    for (uint32_t i=0u;i<8u;++i) difference|=a[i]^0xa6u;
    wpa_wipe(&aes,sizeof(aes)); wpa_wipe(a,sizeof(a)); wpa_wipe(block,sizeof(block));
    if (difference) { wpa_wipe(out,n-8u); return 0; }
    return 1;
}

static void cbc(const wpa_aes_t *a, uint8_t mac[16], const uint8_t block[16]) {
    for (uint32_t i=0u;i<16u;++i) mac[i]^=block[i];
    wpa_aes_encrypt(a,mac,mac);
}

int wpa_ccmp(const uint8_t key[16], const uint8_t *header, uint32_t hn,
              const uint8_t pn[6], const uint8_t *in, uint32_t n,
              uint8_t *out, uint8_t tag[8], int decrypt) {
    if ((hn!=24u && hn!=26u) || (header[1]&3u)==3u ||
        (header[0]&0x8fu)!=(hn==24u?8u:0x88u)) return 0;
    int aggregate=hn==26u && (header[24]&128u);
    if (n>(aggregate?CLEAN_AMSDU_BYTES:CLEAN_MSDU_BYTES) ||
        (aggregate && (header[0]!=0x88u || (header[1]&0x84u) || (header[22]&15u))))
        return 0;
    wpa_aes_t aes;
    wpa_aes_init(&aes,key);
    uint8_t nonce[13], block[16], mac[16], stream[16], aad[32], expected[8];
    badger_bytes_zero(block,sizeof(block)); badger_bytes_zero(mac,sizeof(mac)); badger_bytes_zero(aad,sizeof(aad));
    nonce[0]=hn==26u?(header[24]&15u):0u;
    badger_bytes_copy(nonce+1u,header+10u,6u);
    for (uint32_t i=0u;i<6u;++i) nonce[7u+i]=pn[5u-i];
    block[0]=0x59u; badger_bytes_copy(block+1u,nonce,13u);
    block[14]=(uint8_t)(n>>8u); block[15]=(uint8_t)n; cbc(&aes,mac,block);
    aad[1]=(uint8_t)(hn-2u);
    aad[2]=header[0]&0x8fu; aad[3]=header[1]&0xc7u;
    badger_bytes_copy(aad+4u,header+4u,18u); aad[22]=header[22]&15u;
    if (hn==26u) aad[24]=nonce[0];
    cbc(&aes,mac,aad); cbc(&aes,mac,aad+16u);
    block[0]=1u; block[14]=block[15]=0u;
    wpa_aes_encrypt(&aes,block,stream);
    badger_bytes_copy(expected,stream,8u);
    for (uint32_t at=0u;at<n;at+=16u) {
        uint32_t count=n-at<16u?n-at:16u, counter=at/16u+1u;
        block[14]=(uint8_t)(counter>>8u); block[15]=(uint8_t)counter;
        wpa_aes_encrypt(&aes,block,stream);
        uint8_t plain[16];
        badger_bytes_zero(plain,sizeof(plain));
        for (uint32_t i=0u;i<count;++i) {
            plain[i]=decrypt?(in[at+i]^stream[i]):in[at+i];
            out[at+i]=decrypt?plain[i]:(plain[i]^stream[i]);
        }
        cbc(&aes,mac,plain); wpa_wipe(plain,sizeof(plain));
    }
    for (uint32_t i=0u;i<8u;++i) expected[i]^=mac[i];
    int ok=!decrypt || wpa_equal(tag,expected,8u);
    if (!decrypt) badger_bytes_copy(tag,expected,8u);
    if (!ok) wpa_wipe(out,n);
    wpa_wipe(&aes,sizeof(aes)); wpa_wipe(mac,sizeof(mac)); wpa_wipe(stream,sizeof(stream));
    wpa_wipe(expected,sizeof(expected));
    return ok;
}

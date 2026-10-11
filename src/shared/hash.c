#include "r0_hash.h"

static uint32_t rotate(uint32_t value, uint32_t count) {
    return (value >> count) | (value << (32u-count));
}

static void transform(r0_sha256_t *hash) {
    static const uint32_t constants[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    uint32_t schedule[64];
    for(uint32_t i=0u;i<16u;++i) {
        const uint8_t *b=hash->block+i*4u;
        schedule[i]=(uint32_t)b[0]<<24u|(uint32_t)b[1]<<16u|(uint32_t)b[2]<<8u|b[3];
    }
    for(uint32_t i=16u;i<64u;++i) {
        uint32_t x=schedule[i-15u],y=schedule[i-2u];
        schedule[i]=schedule[i-16u]+(rotate(x,7u)^rotate(x,18u)^(x>>3u))+
                    schedule[i-7u]+(rotate(y,17u)^rotate(y,19u)^(y>>10u));
    }
    uint32_t a=hash->words[0],b=hash->words[1],c=hash->words[2],d=hash->words[3];
    uint32_t e=hash->words[4],f=hash->words[5],g=hash->words[6],h=hash->words[7];
    for(uint32_t i=0u;i<64u;++i) {
        uint32_t first=h+(rotate(e,6u)^rotate(e,11u)^rotate(e,25u))+
                       ((e&f)^(~e&g))+constants[i]+schedule[i];
        uint32_t second=(rotate(a,2u)^rotate(a,13u)^rotate(a,22u))+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+first;d=c;c=b;b=a;a=first+second;
    }
    hash->words[0]+=a;hash->words[1]+=b;hash->words[2]+=c;hash->words[3]+=d;
    hash->words[4]+=e;hash->words[5]+=f;hash->words[6]+=g;hash->words[7]+=h;
}

void r0_sha256_init(r0_sha256_t *hash) {
    static const uint32_t initial[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                                      0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    for(uint32_t i=0u;i<8u;++i)hash->words[i]=initial[i];
    hash->bytes=hash->used=0u;
}

void r0_sha256_update(r0_sha256_t *hash, const uint8_t *bytes, uint32_t length) {
    hash->bytes+=length;
    for(uint32_t i=0u;i<length;++i) {
        hash->block[hash->used++]=bytes[i];
        if(hash->used==64u) { transform(hash); hash->used=0u; }
    }
}

void r0_sha256_finish(r0_sha256_t *hash, uint8_t digest[32]) {
    uint32_t high=hash->bytes>>29u,low=hash->bytes<<3u;
    hash->block[hash->used++]=0x80u;
    if(hash->used>56u) {
        while(hash->used<64u)hash->block[hash->used++]=0u;
        transform(hash);hash->used=0u;
    }
    while(hash->used<56u)hash->block[hash->used++]=0u;
    for(uint32_t i=0u;i<4u;++i) {
        hash->block[56u+i]=(uint8_t)(high>>(24u-8u*i));
        hash->block[60u+i]=(uint8_t)(low>>(24u-8u*i));
    }
    transform(hash);
    for(uint32_t i=0u;i<32u;++i)digest[i]=(uint8_t)(hash->words[i/4u]>>(24u-8u*(i&3u)));
}

uint32_t r0_crc32_update(uint32_t crc, const uint8_t *bytes, uint32_t length) {
    for(uint32_t i=0u;i<length;++i) {
        crc^=bytes[i];
        for(uint32_t bit=0u;bit<8u;++bit)crc=(crc>>1u)^(0xedb88320u&(0u-(crc&1u)));
    }
    return crc;
}

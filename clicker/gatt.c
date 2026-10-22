#include "gatt.h"
#include "../src/shared/bytes.h"

#define READ 1u
#define WRITE 2u
#define AUTH 4u
typedef struct { uint16_t type; uint8_t size, flags; const uint8_t *value; } attribute_t;
static const uint8_t name[] = CLICKER_DEVICE_NAME;
static const uint8_t maker[] = "GitHub / Pimoroni";
static const uint8_t model[] = "Universe Badge";
static const uint8_t serial[] = "Freestanding Clicker";
static const uint8_t report_map[] = {
    0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,0x05,0x07,0x19,0xe0,0x29,0xe7,
    0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,0x95,0x01,0x75,0x08,
    0x81,0x03,0x95,0x05,0x75,0x01,0x05,0x08,0x19,0x01,0x29,0x05,0x91,0x02,
    0x95,0x01,0x75,0x03,0x91,0x03,0x95,0x06,0x75,0x08,0x15,0x00,0x25,0x65,
    0x05,0x07,0x19,0x00,0x29,0x65,0x81,0x00,0xc0
};
#define VALUE(uuid, permissions, ...) \
    {uuid, sizeof((const uint8_t[]){__VA_ARGS__}), permissions, (const uint8_t[]){__VA_ARGS__}}
#define DECL(properties, handle, uuid) VALUE(0x2803, READ, properties, handle, 0, (uuid)&255u, (uuid)>>8u)
#define SERVICE(uuid) VALUE(0x2800, READ, (uuid)&255u, (uuid)>>8u)
static const attribute_t attributes[] = {
    SERVICE(0x1800), DECL(2,3,0x2a00), {0x2a00,sizeof(name)-1u,READ,name},
    DECL(2,5,0x2a01), VALUE(0x2a01,READ,0xc1,3), DECL(2,7,0x2a04),
    VALUE(0x2a04,READ,12,0,24,0,0,0,0xf4,1),
    SERVICE(0x1801), DECL(0x20,10,0x2a05), VALUE(0x2a05,AUTH,1,0,41,0),
    VALUE(0x2902,READ|WRITE|AUTH,0,0),
    SERVICE(0x180a), DECL(2,14,0x2a29), {0x2a29,sizeof(maker)-1u,READ,maker},
    DECL(2,16,0x2a24), {0x2a24,sizeof(model)-1u,READ,model},
    DECL(2,18,0x2a25), {0x2a25,sizeof(serial)-1u,READ,serial},
    DECL(2,20,0x2a50), VALUE(0x2a50,READ,2,0xfe,0xca,0x27,0x40,0,1),
    SERVICE(0x1812), DECL(2,23,0x2a4a), VALUE(0x2a4a,READ|AUTH,0x11,1,0,2),
    DECL(2,25,0x2a4b), {0x2a4b,sizeof(report_map),READ|AUTH,report_map},
    DECL(6,27,0x2a4e), VALUE(0x2a4e,READ|WRITE|AUTH,1),
    DECL(4,29,0x2a4c), VALUE(0x2a4c,WRITE|AUTH,0),
    DECL(0x12,31,0x2a4d), VALUE(0x2a4d,READ|AUTH,0,0,0,0,0,0,0,0),
    VALUE(0x2902,READ|WRITE|AUTH,0,0), VALUE(0x2908,READ,1,1),
    DECL(0x0e,35,0x2a4d), VALUE(0x2a4d,READ|WRITE|AUTH,0), VALUE(0x2908,READ,1,2),
    DECL(0x12,38,0x2a22), VALUE(0x2a22,READ|AUTH,0,0,0,0,0,0,0,0),
    VALUE(0x2902,READ|WRITE|AUTH,0,0),
    DECL(0x0e,41,0x2a32), VALUE(0x2a32,READ|WRITE|AUTH,0)
};
#define ATTR_COUNT (sizeof(attributes)/sizeof(attributes[0]))
static uint16_t get16(const uint8_t *p) { return (uint16_t)((uint32_t)p[0] | (uint32_t)p[1]<<8u); }
static void put16(uint8_t *p, uint16_t value) { p[0]=(uint8_t)value; p[1]=(uint8_t)(value>>8u); }

void clicker_gatt_init(clicker_gatt_t *g) {
    badger_bytes_zero((uint8_t *)g, sizeof(*g));
    g->mtu=23u; g->protocol=1u;
}
static uint32_t error(clicker_gatt_t *g, uint8_t opcode, uint16_t handle, uint8_t reason, uint8_t *out) {
    ++g->errors;
    if ((opcode & 0x40u) != 0u) return 0u;
    out[0]=1u; out[1]=opcode; put16(out+2u,handle); out[4]=reason;
    return 5u;
}
static uint8_t permission(const clicker_gatt_t *g, uint16_t h, uint8_t flag) {
    if (h==0u || h>ATTR_COUNT) return 1u;
    uint8_t flags=attributes[h-1u].flags;
    if ((flags&flag)==0u) return flag==READ?2u:3u;
    if ((flags&AUTH)!=0u && (!g->encrypted || !g->authenticated)) return 5u;
    return 0u;
}
static const uint8_t *value(const clicker_gatt_t *g, uint16_t h, uint8_t scratch[2]) {
    if (h==31u || h==38u) return g->report;
    if (h==27u) { scratch[0]=g->protocol; return scratch; }
    if (h==35u || h==41u) { scratch[0]=g->leds; return scratch; }
    if (h==11u || h==32u || h==39u) {
        put16(scratch,h==11u?g->service_ccc:h==32u?g->report_ccc:g->boot_ccc); return scratch;
    }
    return attributes[h-1u].value;
}
static uint16_t uuid(const uint8_t *p, uint32_t n) {
    if (n==2u) return get16(p);
    static const uint8_t base[16]={0xfb,0x34,0x9b,0x5f,0x80,0,0,0x80,0,0x10,0,0,0,0,0,0};
    if (n!=16u) return 0u;
    for (uint32_t i=0u;i<16u;++i) if (i!=12u && i!=13u && p[i]!=base[i]) return 0u;
    return get16(p+12u);
}
static uint16_t group_end(uint16_t handle) {
    for (uint16_t h=(uint16_t)(handle+1u);h<=ATTR_COUNT;++h)
        if (attributes[h-1u].type==0x2800u) return (uint16_t)(h-1u);
    return (uint16_t)ATTR_COUNT;
}
uint32_t clicker_gatt_request(clicker_gatt_t *g,const uint8_t *p,uint32_t n,uint8_t *out) {
    if (n==0u || n>g->mtu) return error(g,n?p[0]:0u,0u,4u,out);
    uint8_t op=p[0], scratch[2];
    uint16_t h=n>=3u?get16(p+1u):0u;
    if (op==2u) {
        if (n!=3u || h<23u) return error(g,op,0u,4u,out);
        g->mtu=h<CLICKER_GATT_MTU?h:CLICKER_GATT_MTU;
        out[0]=3u; put16(out+1u,CLICKER_GATT_MTU); return 3u;
    }
    if (op==0x1eu) {
        if (n!=1u) return error(g,op,0u,4u,out);
        return 0u;
    }
    if (op==0x0au || op==0x0cu) {
        if (n!=(op==0x0au?3u:5u)) return error(g,op,h,4u,out);
        uint8_t denied=permission(g,h,READ);
        if (denied) return error(g,op,h,denied,out);
        uint32_t offset=op==0x0cu?get16(p+3u):0u, size=attributes[h-1u].size;
        if (offset>size) return error(g,op,h,7u,out);
        uint32_t length=size-offset;
        if (length>g->mtu-1u) length=g->mtu-1u;
        out[0]=(uint8_t)(op+1u); badger_bytes_copy(out+1u,value(g,h,scratch)+offset,length);
        return length+1u;
    }
    if (op==0x0eu) {
        if (n<5u || (n&1u)==0u) return error(g,op,h,4u,out);
        uint32_t used=1u; out[0]=0x0fu;
        for (uint32_t i=1u;i<n;i+=2u) {
            h=get16(p+i); uint8_t denied=permission(g,h,READ);
            if (denied) return error(g,op,h,denied,out);
            uint32_t size=attributes[h-1u].size;
            if (size>g->mtu-used) size=g->mtu-used;
            badger_bytes_copy(out+used,value(g,h,scratch),size); used+=size;
        }
        return used;
    }
    if (op==0x12u || op==0x52u) {
        if (n<3u) return error(g,op,h,4u,out);
        uint8_t denied=permission(g,h,WRITE);
        if (denied) return error(g,op,h,denied,out);
        if (n-3u!=attributes[h-1u].size) return error(g,op,h,0x0du,out);
        if (h==11u || h==32u || h==39u) {
            uint16_t setting=get16(p+3u);
            if ((h==11u && setting!=0u && setting!=2u) || (h!=11u && setting>1u))
                return error(g,op,h,0x13u,out);
            uint16_t *ccc=h==11u?&g->service_ccc:h==32u?&g->report_ccc:&g->boot_ccc;
            if (*ccc!=setting) { *ccc=setting; g->dirty=1u; }
        } else if (h==27u || h==29u) {
            if (p[3]>1u) return error(g,op,h,0x13u,out);
            if (h==27u) g->protocol=p[3]; else g->suspended=p[3]==0u;
        } else if (h==35u || h==41u) g->leds=p[3]&31u;
        else return error(g,op,h,3u,out);
        out[0]=0x13u; return op==0x52u?0u:1u;
    }
    if (op==4u || op==6u || op==8u || op==0x10u) {
        if (n<5u) return error(g,op,h,4u,out);
        uint16_t last=get16(p+3u);
        if (h==0u || h>last) return error(g,op,h,1u,out);
        if ((op==4u && n!=5u) || (op==6u && n!=9u) ||
            ((op==8u || op==0x10u) && n!=7u && n!=21u)) return error(g,op,h,4u,out);
        uint16_t wanted=op==4u?0u:op==6u?get16(p+5u):uuid(p+5u,n-5u);
        if (op==0x10u && wanted!=0x2800u && wanted!=0x2801u) return error(g,op,h,0x10u,out);
        uint32_t used=(op==6u?1u:2u), element=0u;
        out[0]=(uint8_t)(op+1u); out[1]=1u;
        for (uint16_t current=h; current<=last && current<=ATTR_COUNT; ++current) {
            const attribute_t *a=&attributes[current-1u];
            if (op!=4u && a->type!=wanted) continue;
            if (op==6u && (wanted!=0x2800u || a->size!=2u || get16(a->value)!=get16(p+7u))) continue;
            uint32_t size=op==4u?4u:op==6u?4u:op==0x10u?6u:(uint32_t)a->size+2u;
            if (op==8u && size>g->mtu-2u) size=g->mtu-2u;
            if ((element && size!=element) || used+size>g->mtu) break;
            if (op==8u) {
                uint8_t denied=permission(g,current,READ);
                if (denied) return used==2u?error(g,op,current,denied,out):used;
            }
            put16(out+used,current);
            if (op==4u) put16(out+used+2u,a->type);
            else if (op==6u || op==0x10u) {
                put16(out+used+2u,group_end(current));
                if (op==0x10u) badger_bytes_copy(out+used+4u,a->value,2u);
            } else badger_bytes_copy(out+used+2u,value(g,current,scratch),size-2u);
            element=size; used+=size;
        }
        if (!element) return error(g,op,h,0x0au,out);
        if (op==8u || op==0x10u) out[1]=(uint8_t)element;
        return used;
    }
    return error(g,op,h,6u,out);
}
uint16_t clicker_gatt_notification_handle(const clicker_gatt_t *g) {
    if (!g->encrypted || !g->authenticated || g->suspended) return 0u;
    return g->protocol ? (g->report_ccc?31u:0u) : (g->boot_ccc?38u:0u);
}

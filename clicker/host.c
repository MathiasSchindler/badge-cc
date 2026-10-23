#include "host.h"
#include "crypto.h"
#include "../src/shared/bytes.h"

#define PAIR_RNG 1u
#define PAIR_CONFIRM 2u
#define PAIR_RANDOM 3u
#define PAIR_ENCRYPT 4u
#define PAIR_KEYS 5u
#define PAIR_COMMIT 6u
#define TAG_RNG 1u
#define TAG_ADVERTISE 2u
static uint16_t get16(const uint8_t *p) { return (uint16_t)((uint32_t)p[0]|(uint32_t)p[1]<<8u); }
static void put16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8u); }
static int failed(clicker_host_t *h,uint8_t error) { h->error=error;return 0; }
static uint32_t tick(clicker_host_t *h) { return h->bt->io.now(h->bt->io.context); }

static int command(clicker_host_t *h,uint16_t opcode,const uint8_t *data,uint8_t n,uint8_t tag) {
    if (h->command_count==8u || n>32u) return failed(h,2u);
    clicker_command_t *c=&h->commands[(h->command_head+h->command_count)&7u];
    c->opcode=opcode;c->length=n;c->tag=tag;badger_bytes_copy(c->data,data,n);
    ++h->command_count;return 1;
}
static int l2cap(clicker_host_t *h,uint16_t cid,const uint8_t *data,uint32_t n) {
    if (!h->connected) return failed(h,1u);
    if (h->output_count==8u || n>CLICKER_GATT_MTU) return failed(h,2u);
    clicker_acl_t *a=&h->output[(h->output_head+h->output_count)&7u];
    a->length=(uint16_t)(n+4u);a->offset=0u;
    put16(a->data,(uint16_t)n);put16(a->data+2u,cid);badger_bytes_copy(a->data+4u,data,n);
    ++h->output_count;return 1;
}
static int disconnect(clicker_host_t *h,uint16_t handle) {
    uint8_t parameters[3];put16(parameters,handle);parameters[2]=0x13u;
    return command(h,0x0406u,parameters,sizeof(parameters),0u);
}
static int advertise(clicker_host_t *h) {
    uint8_t enabled=1u;return command(h,0x200au,&enabled,1u,TAG_ADVERTISE);
}
static void clear_pairing(clicker_host_t *h) {
    clicker_secret_clear(h->tk,sizeof(h->tk));clicker_secret_clear(h->random,sizeof(h->random));
    clicker_secret_clear(h->stk,sizeof(h->stk));clicker_secret_clear(h->entropy,sizeof(h->entropy));
    clicker_secret_clear(h->confirm,sizeof(h->confirm));h->pairing=0u;h->rng_bytes=0u;
    clicker_secret_clear(&h->candidate,sizeof(h->candidate));
    h->passkey=0u;h->keys_sent=h->peer_keys=0u;
}
static int pair_failed(clicker_host_t *h,uint8_t reason) {
    uint8_t p[2]={5u,reason};h->pair_error=reason;
    clear_pairing(h);
    if (!l2cap(h,6u,p,sizeof(p))) return 0;
    h->closing=1u;h->switch_started=tick(h);
    return 1;
}
static int known_peer(const clicker_host_t *h,uint8_t type,const uint8_t address[6]) {
    for (uint32_t i=0u;i<CLICKER_BONDS;++i) {
        const clicker_bond_t *b=&h->bonds->peers[i];
        if (!b->valid) continue;
        if ((b->type==type && clicker_secret_equal(b->address,address,6u)) ||
            (type==1u && b->has_irk && clicker_resolves(b->irk,address))) return (int)i;
    }
    return -1;
}
static int distribute(clicker_host_t *h) {
    if (h->pairing!=PAIR_KEYS) return 1;
    if (!h->keys_sent) {
        uint8_t p[17];p[0]=6u;badger_bytes_copy(p+1u,h->candidate.ltk,16u);
        if (!l2cap(h,6u,p,17u)) return 0;
        p[0]=7u;put16(p+1u,h->candidate.ediv);badger_bytes_copy(p+3u,h->candidate.random,8u);
        if (!l2cap(h,6u,p,11u)) return 0;
        h->keys_sent=1u;
    }
    if (h->peer_keys==0u) h->pairing=PAIR_COMMIT;
    return 1;
}
static int random_complete(clicker_host_t *h,const uint8_t *data,uint32_t n) {
    if (!h->connected || h->pairing!=PAIR_RNG) return 1;
    if (n!=9u || h->rng_bytes>56u) return failed(h,1u);
    badger_bytes_copy(h->entropy+h->rng_bytes,data+1u,8u);h->rng_bytes+=8u;
    if (h->rng_bytes<64u) return command(h,0x2018u,0,0u,TAG_RNG);
    uint32_t value=badger_bytes_load_u32(h->entropy);
    uint8_t difference=0u;
    for (uint32_t i=1u;i<64u;++i) difference|=h->entropy[i]^h->entropy[0];
    if (!difference) return failed(h,6u);
    if (value>=4294000000u) {h->rng_bytes=0u;return command(h,0x2018u,0,0u,TAG_RNG);}
    h->passkey=value%1000000u;
    badger_bytes_store_u32(h->tk,h->passkey);
    badger_bytes_copy(h->random,h->entropy+4u,16u);
    badger_bytes_zero((uint8_t *)&h->candidate,sizeof(h->candidate));
    h->candidate.valid=1u;h->candidate.type=h->peer_type;
    badger_bytes_copy(h->candidate.address,h->peer,6u);
    badger_bytes_copy(h->candidate.ltk,h->entropy+20u,16u);
    badger_bytes_copy(h->candidate.random,h->entropy+36u,8u);
    h->candidate.ediv=get16(h->entropy+44u);
    clicker_secret_clear(h->entropy,sizeof(h->entropy));
    h->pairing=PAIR_CONFIRM;
    return l2cap(h,6u,h->response,7u);
}
static int smp(clicker_host_t *h,const uint8_t *p,uint32_t n) {
    if (!n) return failed(h,1u);
    h->last_smp=p[0];
    if (p[0]==1u) {
        if (n!=7u) return pair_failed(h,0x0au);
        h->pairing_features=((uint32_t)p[1]<<24u)|((uint32_t)p[3]<<16u)|((uint32_t)p[4]<<8u)|p[6];
        if (h->encrypted || h->pairing) return pair_failed(h,9u);
        if ((p[1]!=2u && p[1]!=4u) || p[2]!=0u || (p[3]&3u)!=1u)
            return pair_failed(h,3u);
        if (p[4]!=16u) return pair_failed(h,6u);
        if ((p[6]&1u)==0u || (p[5]&0xf0u) || (p[6]&0xf0u)) return pair_failed(h,3u);
        int slot=known_peer(h,h->peer_type,h->peer);
        if (slot<0) for (uint32_t i=0u;i<CLICKER_BONDS;++i) if (!h->bonds->peers[i].valid) {slot=(int)i;break;}
        if (slot<0) return pair_failed(h,5u);
        badger_bytes_copy(h->request,p,7u);
        h->response[0]=2u;h->response[1]=0u;h->response[2]=0u;h->response[3]=5u;
        h->response[4]=16u;h->response[5]=p[5]&2u;h->response[6]=1u;
        h->pairing=PAIR_RNG;h->pair_error=0u;h->rng_bytes=0u;h->keys_sent=0u;
        h->pairing_started=tick(h);
        return command(h,0x2018u,0,0u,TAG_RNG);
    }
    if (p[0]==3u && n==17u && h->pairing==PAIR_CONFIRM) {
        badger_bytes_copy(h->confirm,p+1u,16u);
        uint8_t response[17];response[0]=3u;
        clicker_c1(h->tk,h->random,h->request,h->response,h->peer_type,h->peer,0u,h->own,response+1u);
        h->pairing=PAIR_RANDOM;return l2cap(h,6u,response,sizeof(response));
    }
    if (p[0]==4u && n==17u && h->pairing==PAIR_RANDOM) {
        uint8_t check[16];
        clicker_c1(h->tk,p+1u,h->request,h->response,h->peer_type,h->peer,0u,h->own,check);
        int ok=clicker_secret_equal(check,h->confirm,16u);clicker_secret_clear(check,sizeof(check));
        if (!ok) return pair_failed(h,4u);
        clicker_s1(h->tk,h->random,p+1u,h->stk);
        uint8_t response[17];response[0]=4u;badger_bytes_copy(response+1u,h->random,16u);
        h->pairing=PAIR_ENCRYPT;return l2cap(h,6u,response,sizeof(response));
    }
    if (h->pairing==PAIR_KEYS && h->encrypted) {
        if (p[0]==8u && n==17u && (h->peer_keys&1u)) {
            badger_bytes_copy(h->candidate.irk,p+1u,16u);h->candidate.has_irk=1u;h->peer_keys&=(uint8_t)~1u;
            return distribute(h);
        }
        if (p[0]==9u && n==8u && (h->peer_keys&2u) && p[1]<=1u) {
            h->candidate.type=p[1];badger_bytes_copy(h->candidate.address,p+2u,6u);
            h->peer_keys&=(uint8_t)~2u;return distribute(h);
        }
    }
    if (p[0]==5u && n==2u) {
        h->pair_error=p[1];clear_pairing(h);h->closing=1u;h->switch_started=tick(h);return 1;
    }
    return pair_failed(h,7u);
}

static int signaling(clicker_host_t *h,const uint8_t *p,uint32_t n) {
    uint32_t at=0u;
    while (at<n) {
        if (n-at<4u) return failed(h,1u);
        uint8_t code=p[at],id=p[at+1u];uint32_t size=get16(p+at+2u);at+=4u;
        if (!id || size>n-at) return failed(h,1u);
        uint8_t reply[12];reply[1]=id;
        if (code==0x0au && size==2u) {
            reply[0]=0x0bu;put16(reply+2u,8u);put16(reply+4u,get16(p+at));
            put16(reply+6u,0u);badger_bytes_store_u32(reply+8u,0x00000090u);
            if (!l2cap(h,5u,reply,12u)) return 0;
        } else if (code!=1u && code!=0x13u) {
            reply[0]=1u;put16(reply+2u,2u);put16(reply+4u,0u);
            if (!l2cap(h,5u,reply,6u)) return 0;
        }
        at+=size;
    }
    return 1;
}
static int received_acl(clicker_host_t *h,const uint8_t *p,uint32_t n) {
    if (n<4u || get16(p+2u)!=n-4u) return failed(h,1u);
    if (!h->connected || (get16(p)&0xfffu)!=h->handle) return 1;
    uint8_t boundary=(uint8_t)((get16(p)>>12u)&3u);
    if (boundary==0u || boundary==2u) {
        if (h->incoming_size || n<8u) return failed(h,1u);
        uint32_t expected=(uint32_t)get16(p+4u)+4u;
        if (expected>sizeof(h->incoming) || expected<4u) return failed(h,1u);
        h->incoming_expected=(uint16_t)expected;
        h->incoming_started=tick(h);
    } else if (boundary!=1u || h->incoming_size==0u) return failed(h,1u);
    if (n-4u>(uint32_t)(h->incoming_expected-h->incoming_size)) return failed(h,1u);
    badger_bytes_copy(h->incoming+h->incoming_size,p+4u,n-4u);
    h->incoming_size+=(uint16_t)(n-4u);
    if (h->incoming_size!=h->incoming_expected) return 1;
    uint16_t cid=get16(h->incoming+2u);uint32_t length=h->incoming_size-4u;
    h->incoming_size=h->incoming_expected=0u;
    if (cid==4u) {
        uint8_t reply[CLICKER_GATT_MTU];
        uint32_t count=clicker_gatt_request(&h->gatt,h->incoming+4u,length,reply);
        return !count || l2cap(h,4u,reply,count);
    }
    if (cid==6u) return smp(h,h->incoming+4u,length);
    if (cid==5u) return signaling(h,h->incoming+4u,length);
    ++h->rejected;return 1;
}

static int ltk_request(clicker_host_t *h,const uint8_t *p,uint32_t n) {
    if (n!=15u) return failed(h,1u);
    uint8_t parameters[18];put16(parameters,get16(p+3u));
    if (!h->connected || get16(p+3u)!=h->handle) {
        ++h->rejected;
        return command(h,0x201bu,parameters,2u,0u);
    }
    uint8_t zero=0u;for (uint32_t i=5u;i<15u;++i) zero|=p[i];
    if (!zero && h->pairing==PAIR_ENCRYPT) {
        badger_bytes_copy(parameters+2u,h->stk,16u);h->authenticated=1u;
        return command(h,0x201au,parameters,18u,0u);
    }
    for (uint32_t i=0u;i<CLICKER_BONDS;++i) {
        clicker_bond_t *b=&h->bonds->peers[i];
        if (b->valid && b->ediv==get16(p+13u) && clicker_secret_equal(b->random,p+5u,8u)) {
            if (h->new_only) {
                ++h->rejected;h->closing=1u;h->switch_started=tick(h);
                return command(h,0x201bu,parameters,2u,0u);
            }
            badger_bytes_copy(parameters+2u,b->ltk,16u);h->session_bond=(int8_t)i;h->authenticated=1u;
            return command(h,0x201au,parameters,18u,0u);
        }
    }
    return command(h,0x201bu,parameters,2u,0u);
}
static int connected(clicker_host_t *h,const uint8_t *p,uint32_t n) {
    if ((p[2]==1u && n!=21u) || (p[2]==0x0au && n!=33u)) return failed(h,1u);
    if (p[3]!=0u) {h->last_status=p[3];return advertise(h);}
    uint16_t handle=get16(p+4u);
    if (p[6]!=1u || p[7]>1u || handle>0x0effu) return failed(h,1u);
    h->advertising=0u;
    if (h->connected || (h->new_only && known_peer(h,p[7],p+8u)>=0)) {
        ++h->rejected;return disconnect(h,handle);
    }
    h->handle=handle;h->peer_type=p[7];badger_bytes_copy(h->peer,p+8u,6u);
    h->connected=1u;h->advertising=0u;h->session_bond=-1;h->encrypted=h->authenticated=0u;
    h->last_smp=h->stopped_pairing=h->encryption_status=h->disconnect_reason=0u;
    h->acl_credit=h->acl_total;h->outstanding=0u;h->closing=0u;++h->connections;
    clicker_gatt_init(&h->gatt);clear_pairing(h);
    uint8_t security[2]={0x0bu,5u};return l2cap(h,6u,security,sizeof(security));
}
static int encrypted(clicker_host_t *h,const uint8_t *p,uint32_t n) {
    if (n!=6u) return failed(h,1u);
    if (!h->connected || get16(p+3u)!=h->handle) return 1;
    h->last_status=p[2];
    h->encryption_status=p[2];
    h->encrypted=(p[2]==0u && p[5]!=0u);
    h->gatt.encrypted=h->encrypted;h->gatt.authenticated=h->encrypted && h->authenticated;
    if (!h->encrypted) {h->closing=1u;h->switch_started=tick(h);return 1;}
    if (!h->authenticated) return failed(h,1u);
    if (h->pairing==PAIR_ENCRYPT) {
        h->pairing=PAIR_KEYS;h->peer_keys=h->response[5]?3u:0u;
        return distribute(h);
    }
    if (h->session_bond>=0) {
        const clicker_bond_t *b=&h->bonds->peers[(uint8_t)h->session_bond];
        h->gatt.report_ccc=b->report_ccc;h->gatt.boot_ccc=b->boot_ccc;h->gatt.service_ccc=b->service_ccc;
        h->pair_error=0u;
    }
    return 1;
}
int clicker_host_packet(clicker_host_t *h,uint8_t type,const uint8_t *p,uint32_t n) {
    if (h->error) return 0;
    if (type==2u) return received_acl(h,p,n);
    if (type!=4u || n<2u || p[1]+2u!=n) return failed(h,1u);
    h->last_event=p[0];
    if (p[0]==0x0eu || p[0]==0x0fu) {
        uint8_t status,credits;uint16_t opcode;const uint8_t *result;uint32_t size;
        if (p[0]==0x0eu) {
            if (n<6u) return failed(h,1u);
            credits=p[2];opcode=get16(p+3u);status=p[5];result=p+5u;size=n-5u;
        } else {
            if (n!=6u) return failed(h,1u);
            status=p[2];credits=p[3];opcode=get16(p+4u);result=p+2u;size=1u;
        }
        h->bt->credits=credits;h->last_status=status;
        if (!h->waiting || opcode!=h->command_opcode) return failed(h,3u);
        uint8_t tag=h->command_tag;h->waiting=0u;
        if (status==2u &&
            (opcode==0x0406u || opcode==0x201au || opcode==0x201bu ||
             opcode==0x2020u || opcode==0x2021u)) {
            ++h->stale_commands;
            return 1;
        }
        if (status==0x12u && opcode==0x0406u && !h->connected) {
            ++h->stale_commands;
            return 1;
        }
        if (status) return failed(h,3u);
        if (tag==TAG_RNG) return random_complete(h,result,size);
        if (tag==TAG_ADVERTISE) h->advertising=1u;
        return 1;
    }
    if (p[0]==0x13u) {
        if (n<3u || n!=3u+4u*p[2]) return failed(h,1u);
        for (uint32_t i=0u;i<p[2];++i) {
            uint16_t handle=get16(p+3u+i*4u),count=get16(p+5u+i*4u);
            if (h->connected && handle==h->handle) {
                if (count>h->outstanding) return failed(h,2u);
                h->outstanding-=count;h->acl_credit+=count;
            }
        }
        return 1;
    }
    if (p[0]==5u) {
        if (n!=6u) return failed(h,1u);
        if (h->connected && get16(p+3u)!=h->handle) return 1;
        h->stopped_pairing=h->pairing;h->disconnect_reason=p[5];
        h->connected=h->encrypted=h->authenticated=0u;h->session_bond=-1;
        h->output_count=h->output_head=h->incoming_size=h->incoming_expected=0u;
        h->command_count=h->command_head=0u;h->outstanding=0u;h->acl_credit=h->acl_total;
        h->closing=0u;++h->disconnects;clicker_gatt_init(&h->gatt);clear_pairing(h);
        return advertise(h);
    }
    if (p[0]==8u) return encrypted(h,p,n);
    if (p[0]==0x10u || p[0]==0x1au) return failed(h,1u);
    if (p[0]==0x3eu) {
        if (n<3u) return failed(h,1u);
        h->last_meta=p[2];
        if (p[2]==1u || p[2]==0x0au) return connected(h,p,n);
        if (p[2]==5u) return ltk_request(h,p,n);
        if (p[2]==6u) {
            if (n!=13u) return failed(h,1u);
            uint8_t parameters[14];badger_bytes_copy(parameters,p+3u,10u);
            badger_bytes_zero(parameters+10u,4u);
            if (!h->connected || get16(p+3u)!=h->handle) {
                put16(parameters,get16(p+3u));parameters[2]=0x3bu;
                return command(h,0x2021u,parameters,3u,0u);
            }
            uint32_t min=get16(p+5u),max=get16(p+7u),latency=get16(p+9u),timeout=get16(p+11u);
            if (min<6u || max>3200u || min>max || latency>499u || timeout<10u ||
                timeout>3200u || timeout*4u<=(latency+1u)*max) {
                put16(parameters,h->handle);parameters[2]=0x3bu;
                return command(h,0x2021u,parameters,3u,0u);
            }
            return command(h,0x2020u,parameters,14u,0u);
        }
    }
    return 1;
}

int clicker_host_init(clicker_host_t *h,clicker_bt_t *bt,clicker_bonds_t *bonds,
                       const uint8_t address[6],uint16_t acl_size,uint8_t acl_packets) {
    badger_bytes_zero((uint8_t *)h,sizeof(*h));
    h->bt=bt;h->bonds=bonds;h->session_bond=-1;
    if (!acl_packets || acl_size<27u) return failed(h,2u);
    static const uint8_t fallback[6]={0xac,0x1f,0x12,0xa2,0x43,0x43};
    uint8_t zero=0u,ones=0u;
    for (uint32_t i=0u;i<6u;++i) {zero|=address[i];ones|=address[i]^255u;}
    if (!zero || !ones || (address[5]&1u) ||
        clicker_secret_equal(address,fallback,6u)) return failed(h,6u);
    h->acl_size=acl_size>251u?251u:acl_size;
    h->acl_credit=h->acl_total=acl_packets;badger_bytes_copy(h->own,address,6u);
    clicker_gatt_init(&h->gatt);
    static const uint8_t mask[8]={0x90,0xe0,4,2,0,0x80,0,0x20};
    static const uint8_t le_mask[8]={0x7d,2,0,0,0,0,0,0};
    static const uint8_t parameters[15]={0xa0,0,0xf0,0,0,0,0,0,0,0,0,0,0,7,0};
    static const uint8_t data[32]={
        27,2,1,6,3,3,0x12,0x18,3,0x19,0xc1,3,15,8,
        'G','i','t','H','u','b',' ','C','l','i','c','k','e','r'
    };
    static const uint8_t scan[32]={
        22,21,9,'G','i','t','H','u','b',' ','B','a','d','g','e',' ','C','l','i','c','k','e','r'
    };
    return command(h,0x0c01u,mask,8u,0u) && command(h,0x2001u,le_mask,8u,0u) &&
        command(h,0x2006u,parameters,15u,0u) && command(h,0x2008u,data,32u,0u) &&
        command(h,0x2009u,scan,32u,0u) && advertise(h);
}
int clicker_host_ready(const clicker_host_t *h) {
    return !h->error && h->connected && h->session_bond>=0 && !h->closing && !h->new_only &&
        !h->dirty && !h->gatt.dirty && clicker_gatt_notification_handle(&h->gatt)!=0u;
}
int clicker_host_notify(clicker_host_t *h,const uint8_t report[8]) {
    if (!clicker_host_ready(h) || h->output_count==8u) return 0;
    uint8_t p[11];p[0]=0x1bu;put16(p+1u,clicker_gatt_notification_handle(&h->gatt));
    badger_bytes_copy(p+3u,report,8u);
    if (!l2cap(h,4u,p,sizeof(p))) return 0;
    badger_bytes_copy(h->gatt.report,report,8u);++h->notifications;return 1;
}
void clicker_host_new_computer(clicker_host_t *h) {
    if (h->connected && !h->closing) {
        uint8_t released[8]={0};
        if (clicker_host_ready(h) && !clicker_host_notify(h,released)) {h->error=2u;return;}
        h->closing=1u;h->switch_started=tick(h);
    }
    h->new_only=1u;
}
int clicker_host_persist(clicker_host_t *h,const clicker_bond_io_t *io) {
    if (h->error || !h->connected || h->output_count || h->outstanding) return !h->error;
    if (h->pairing==PAIR_COMMIT && h->keys_sent) {
        int slot=known_peer(h,h->candidate.type,h->candidate.address);
        if (slot<0) for (uint32_t i=0u;i<CLICKER_BONDS;++i) if (!h->bonds->peers[i].valid) {slot=(int)i;break;}
        if (slot<0) return failed(h,5u);
        badger_bytes_copy((uint8_t *)&h->bonds->peers[slot],(const uint8_t *)&h->candidate,sizeof(h->candidate));
        h->session_bond=(int8_t)slot;h->dirty=1u;
    }
    if (h->session_bond>=0 && (h->dirty || h->gatt.dirty)) {
        clicker_bond_t *b=&h->bonds->peers[(uint8_t)h->session_bond];
        b->report_ccc=h->gatt.report_ccc;b->boot_ccc=h->gatt.boot_ccc;b->service_ccc=h->gatt.service_ccc;
        if (!clicker_bonds_save(h->bonds,io)) return failed(h,5u);
        h->dirty=h->gatt.dirty=0u;
        if (h->pairing==PAIR_COMMIT) {clear_pairing(h);h->new_only=0u;}
    }
    return 1;
}
int clicker_host_pump(clicker_host_t *h) {
    if (h->error) return -1;
    uint8_t packet[CLICKER_BT_PACKET_MAX],type;uint32_t length;
    int received=clicker_bt_receive(h->bt,&type,packet,sizeof(packet),&length);
    if (received<0) {failed(h,4u);return -1;}
    if (received && !clicker_host_packet(h,type,packet,length)) return -1;
    if (h->waiting && tick(h)-h->command_started>2000000u) {failed(h,3u);return -1;}
    if (h->incoming_size && tick(h)-h->incoming_started>3000000u) {failed(h,1u);return -1;}
    if (h->pairing && tick(h)-h->pairing_started>30000000u && !pair_failed(h,8u)) return -1;
    if (h->closing==1u && ((!h->output_count && !h->outstanding) ||
                           tick(h)-h->switch_started>2000000u)) {
        if (!disconnect(h,h->handle)) return -1;h->closing=2u;
    }
    if (!h->waiting && h->command_count && h->bt->credits) {
        clicker_command_t *c=&h->commands[h->command_head];
        packet[0]=(uint8_t)c->opcode;packet[1]=(uint8_t)(c->opcode>>8u);packet[2]=c->length;
        badger_bytes_copy(packet+3u,c->data,c->length);
        int sent=clicker_bt_send(h->bt,1u,packet,c->length+3u);
        if (sent<0) {failed(h,4u);return -1;}
        if (sent) {
            h->waiting=1u;h->command_opcode=c->opcode;h->command_tag=c->tag;
            h->command_started=tick(h);
            --h->bt->credits;h->command_head=(h->command_head+1u)&7u;--h->command_count;
        }
    }
    if (h->connected && h->output_count && h->acl_credit) {
        clicker_acl_t *a=&h->output[h->output_head];
        uint32_t size=a->length-a->offset;if (size>h->acl_size) size=h->acl_size;
        put16(packet,(uint16_t)(h->handle|(a->offset?0x1000u:0u)));put16(packet+2u,(uint16_t)size);
        badger_bytes_copy(packet+4u,a->data+a->offset,size);
        int sent=clicker_bt_send(h->bt,2u,packet,size+4u);
        if (sent<0) {failed(h,4u);return -1;}
        if (sent) {
            --h->acl_credit;++h->outstanding;a->offset+=(uint16_t)size;
            if (a->offset==a->length) {h->output_head=(h->output_head+1u)&7u;--h->output_count;}
        }
    }
    return received;
}

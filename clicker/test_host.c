#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "host.h"
#include "crypto.h"
#include "../src/shared/bytes.h"

static uint32_t ticks;
static uint8_t events[64][32], lengths[64], event_in,event_out,entropy;
static uint8_t transmitted[64][200], transmitted_size[64], transmitted_cid[64], transmissions;
static uint8_t flash[8192], last_parameters[32];
static uint16_t last_opcode;
static uint32_t negative_replies;
static clicker_host_t host;
static clicker_bt_t bt;
static clicker_bonds_t bonds;
static const uint8_t own[6]={0x60,0x0c,0x18,0xc1,0xcd,0x28},peer[6]={1,2,3,4,5,6};
static uint16_t get16(const uint8_t *p) {return (uint16_t)((uint32_t)p[0]|(uint32_t)p[1]<<8u);}
static uint32_t clock_tick(void *context) {(void)context;ticks+=100u;return ticks;}
static void event(const uint8_t *p,uint32_t n) {
    assert((uint8_t)(event_in-event_out)<64u && n<=32u);
    memcpy(events[event_in&63u],p,n);lengths[event_in&63u]=(uint8_t)n;++event_in;
}
int clicker_bt_send(clicker_bt_t *unused,uint8_t type,const uint8_t *p,uint32_t n) {
    (void)unused;
    if (type==1u) {
        assert(n==(uint32_t)p[2]+3u && p[2]<=32u);
        last_opcode=get16(p);memcpy(last_parameters,p+3u,p[2]);
        if (last_opcode==0x201bu) ++negative_replies;
        uint8_t reply[14]={0x0e,4,1,p[0],p[1],0};
        if (last_opcode==0x2018u) {
            reply[1]=12u;
            for (uint32_t i=0u;i<8u;++i) reply[6u+i]=++entropy;
            event(reply,14u);
        } else event(reply,6u);
    } else {
        assert(type==2u && n>=8u && get16(p+2u)==n-4u);
        assert(transmissions<64u);
        uint16_t cid=get16(p+6u),length=get16(p+4u);
        assert(length==n-8u && length<=200u);
        memcpy(transmitted[transmissions],p+8u,length);
        transmitted_cid[transmissions]=(uint8_t)cid;transmitted_size[transmissions]=(uint8_t)length;
        ++transmissions;
        uint8_t completed[7]={0x13,5,1,p[0],(uint8_t)(p[1]&15u),1,0};
        event(completed,sizeof(completed));
    }
    return 1;
}
int clicker_bt_receive(clicker_bt_t *unused,uint8_t *type,uint8_t *p,uint32_t capacity,uint32_t *n) {
    (void)unused;
    if (event_in==event_out) {*n=0u;return 0;}
    *n=lengths[event_out&63u];assert(*n<=capacity);
    memcpy(p,events[event_out&63u],*n);++event_out;*type=4u;return 1;
}
static void pumps(void) {
    for (uint32_t i=0u;i<100u;++i) assert(clicker_host_pump(&host)>=0);
}
static void acl(uint16_t cid,const uint8_t *p,uint32_t n) {
    uint8_t frame[200];frame[0]=1;frame[1]=0x20;
    frame[2]=(uint8_t)(n+4u);frame[3]=0;
    frame[4]=(uint8_t)n;frame[5]=0;frame[6]=(uint8_t)cid;frame[7]=0;
    memcpy(frame+8u,p,n);assert(clicker_host_packet(&host,2u,frame,n+8u));pumps();
}
static void connect_peer(void) {
    uint8_t p[21]={0x3e,19,1,0,1,0,1,0};
    memcpy(p+8u,peer,6u);p[14]=24;p[18]=0xf4;p[19]=1;
    assert(clicker_host_packet(&host,4u,p,sizeof(p)));pumps();
}
static int erase(void *context,uint32_t offset) {
    (void)context;assert(offset>=CLICKER_BOND_OFFSET && offset<=CLICKER_BOND_OFFSET+4096u);
    memset(flash+offset-CLICKER_BOND_OFFSET,255,4096u);return 1;
}
static int program(void *context,uint32_t offset,const uint8_t *p,uint32_t n) {
    (void)context;assert(offset>=CLICKER_BOND_OFFSET && offset+n<=CLICKER_BOND_OFFSET+8192u);
    for (uint32_t i=0u;i<n;++i) flash[offset-CLICKER_BOND_OFFSET+i]&=p[i];
    return 1;
}
static int read_flash(void *context,uint32_t offset,uint8_t *p,uint32_t n) {
    (void)context;assert(offset>=CLICKER_BOND_OFFSET && offset+n<=CLICKER_BOND_OFFSET+8192u);
    memcpy(p,flash+offset-CLICKER_BOND_OFFSET,n);return 1;
}
static const clicker_bond_io_t io={0,erase,program,read_flash};
int main(void) {
    memset(&bt,0,sizeof(bt));bt.credits=1u;bt.io.now=clock_tick;
    memset(flash,255,sizeof(flash));assert(clicker_bonds_load(&bonds,&io));
    assert(clicker_bonds_save(&bonds,&io));
    assert(clicker_host_init(&host,&bt,&bonds,own,251u,8u));pumps();
    assert(host.advertising && !host.error && !host.connected);
    connect_peer();assert(host.connected && !clicker_host_ready(&host));
    const uint8_t read_map[]={0x0a,25,0};
    acl(4u,read_map,sizeof(read_map));
    assert(transmitted[transmissions-1u][0]==1u && transmitted[transmissions-1u][4]==5u);
    const uint8_t request[7]={1,4,0,0x0d,16,2,1};
    acl(6u,request,sizeof(request));
    assert(host.pairing==2u && host.passkey<1000000u && host.response[3]==5u);
    uint8_t tk[16]={0},random[16],confirm[17];badger_bytes_store_u32(tk,host.passkey);
    for (uint32_t i=0u;i<16u;++i) random[i]=(uint8_t)(0x80u+i);
    confirm[0]=3u;clicker_c1(tk,random,request,host.response,0u,peer,0u,own,confirm+1u);
    acl(6u,confirm,sizeof(confirm));assert(host.pairing==3u);
    uint8_t random_pdu[17];random_pdu[0]=4u;memcpy(random_pdu+1u,random,16u);
    acl(6u,random_pdu,sizeof(random_pdu));assert(host.pairing==4u);
    uint8_t ltk[15]={0x3e,13,5,1,0};uint8_t expected[16];
    clicker_s1(tk,host.random,random,expected);
    assert(clicker_host_packet(&host,4u,ltk,sizeof(ltk)));pumps();
    assert(last_opcode==0x201au && memcmp(last_parameters+2u,expected,16u)==0);
    const uint8_t encryption[6]={8,4,0,1,0,1};
    assert(clicker_host_packet(&host,4u,encryption,sizeof(encryption)));
    assert(host.pairing==5u && host.gatt.encrypted && host.gatt.authenticated);
    assert(host.keys_sent && host.output_count==2u);
    assert(host.output[host.output_head].data[4]==6u);
    assert(host.output[(host.output_head+1u)&7u].data[4]==7u);
    uint8_t irk[17]={8};for (uint32_t i=1u;i<17u;++i) irk[i]=(uint8_t)(100u+i);
    acl(6u,irk,sizeof(irk));
    uint8_t identity[8]={9,0};memcpy(identity+2u,peer,6u);acl(6u,identity,sizeof(identity));
    assert(host.pairing==6u && !host.output_count && !host.outstanding);
    assert(clicker_host_persist(&host,&io));
    assert(host.session_bond==0 && !host.pairing && bonds.peers[0].valid && bonds.peers[0].has_irk);
    const uint8_t ccc[]={0x12,32,0,1,0};acl(4u,ccc,sizeof(ccc));
    assert(host.gatt.dirty && !clicker_host_ready(&host));
    assert(clicker_host_persist(&host,&io) && clicker_host_ready(&host));
    uint8_t report[8]={0,0,0x4e,0,0,0,0,0};
    assert(clicker_host_notify(&host,report));pumps();
    assert(transmitted_cid[transmissions-1u]==4u && transmitted_size[transmissions-1u]==11u);
    assert(transmitted[transmissions-1u][0]==0x1bu && get16(transmitted[transmissions-1u]+1u)==31u);
    assert(memcmp(transmitted[transmissions-1u]+3u,report,8u)==0);
    memset(report,0,sizeof(report));assert(clicker_host_notify(&host,report));pumps();
    assert(memcmp(transmitted[transmissions-1u]+3u,report,8u)==0);
    assert(host.acl_credit==8u && !host.outstanding);
    const uint8_t disconnected[6]={5,4,0,1,0,0x13};
    assert(clicker_host_packet(&host,4u,disconnected,sizeof(disconnected)));pumps();connect_peer();
    memcpy(ltk+5u,bonds.peers[0].random,8u);
    ltk[13]=(uint8_t)bonds.peers[0].ediv;ltk[14]=(uint8_t)(bonds.peers[0].ediv>>8u);
    assert(clicker_host_packet(&host,4u,ltk,sizeof(ltk)));pumps();
    assert(clicker_host_packet(&host,4u,encryption,sizeof(encryption)));
    assert(clicker_host_ready(&host) && host.session_bond==0);
    clicker_host_new_computer(&host);pumps();
    assert(host.new_only && host.closing==2u && last_opcode==0x0406u);
    assert(clicker_host_packet(&host,4u,disconnected,sizeof(disconnected)));pumps();
    connect_peer();assert(!host.connected && host.rejected==1u && bonds.peers[0].valid);
    assert(clicker_host_packet(&host,4u,ltk,sizeof(ltk)));
    pumps();assert(!host.error && !host.authenticated);
    uint8_t changed_address[21]={0x3e,19,1,0,1,0,1,0,7,8,9,10,11,12,24,0,0,0,0xf4,1,0};
    assert(clicker_host_packet(&host,4u,changed_address,sizeof(changed_address)));pumps();
    uint32_t negatives=negative_replies;
    assert(clicker_host_packet(&host,4u,ltk,sizeof(ltk)));pumps();
    assert(negative_replies==negatives+1u && !host.authenticated && host.session_bond<0);

    clicker_gatt_t g;uint8_t out[CLICKER_GATT_MTU];clicker_gatt_init(&g);
    const uint8_t services[]={0x10,1,0,0xff,0xff,0,0x28};
    assert(clicker_gatt_request(&g,services,sizeof(services),out)==20u && out[0]==0x11u && out[1]==6u);
    assert(get16(out+2u)==1u && get16(out+4u)==7u);
    const uint8_t mtu[]={2,0xff,0xff};
    assert(clicker_gatt_request(&g,mtu,sizeof(mtu),out)==3u && g.mtu==185u);
    assert(clicker_gatt_request(&g,read_map,sizeof(read_map),out)==5u && out[4]==5u);
    g.encrypted=g.authenticated=1u;
    assert(clicker_gatt_request(&g,read_map,sizeof(read_map),out)>60u);
    const uint8_t boot_ccc[]={0x12,39,0,1,0},boot_mode[]={0x52,27,0,0};
    assert(clicker_gatt_request(&g,boot_ccc,sizeof(boot_ccc),out)==1u);
    assert(clicker_gatt_request(&g,boot_mode,sizeof(boot_mode),out)==0u);
    assert(clicker_gatt_notification_handle(&g)==38u);
    assert(clicker_host_init(&host,&bt,&bonds,own,251u,8u));pumps();connect_peer();
    uint8_t fragment1[]={1,0x20,6,0,7,0,4,0,0x10,1};
    uint8_t fragment2[]={1,0x10,5,0,0,0xff,0xff,0,0x28};
    uint8_t before=transmissions;
    assert(clicker_host_packet(&host,2u,fragment1,sizeof(fragment1)));
    assert(host.incoming_size==6u && transmissions==before);
    assert(clicker_host_packet(&host,2u,fragment2,sizeof(fragment2)));pumps();
    assert(!host.incoming_size && transmitted[transmissions-1u][0]==0x11u);
    assert(!clicker_host_packet(&host,2u,fragment2,sizeof(fragment2)) && host.error==1u);
    assert(clicker_host_init(&host,&bt,&bonds,own,251u,8u));pumps();connect_peer();
    uint8_t unsupported[7]={1,0,0,5,16,2,1};
    acl(6u,unsupported,sizeof(unsupported));
    assert(host.pair_error==3u && !host.pairing);
    uint8_t guard[CLICKER_GATT_MTU+2u];guard[0]=0x5au;guard[sizeof(guard)-1u]=0xa5u;
    for (uint32_t opcode=0u;opcode<256u;++opcode) {
        for (uint32_t size=0u;size<9u;++size) {
            uint8_t malformed[9];memset(malformed,0xff,sizeof(malformed));malformed[0]=(uint8_t)opcode;
            clicker_gatt_init(&g);
            uint32_t count=clicker_gatt_request(&g,malformed,size,guard+1u);
            assert(count<=CLICKER_GATT_MTU);
            assert(guard[0]==0x5au && guard[sizeof(guard)-1u]==0xa5u);
        }
    }
    host.error=0u;host.connected=0u;host.waiting=1u;host.command_opcode=0x201au;
    host.command_tag=0u;
    const uint8_t stale[]={0x0e,4,1,0x1a,0x20,2};
    assert(clicker_host_packet(&host,4u,stale,sizeof(stale)) && host.stale_commands==1u);
    host.waiting=1u;host.command_opcode=0x0406u;
    const uint8_t already_closed[]={0x0f,4,0x12,1,6,4};
    assert(clicker_host_packet(&host,4u,already_closed,sizeof(already_closed)) &&
           host.stale_commands==2u && !host.error);
    puts("PASS: encrypted GATT/HID discovery, legacy passkey pairing/key distribution,");
    puts("      persisted CCC/LTK reconnect, ACL completion credits, press/release and new-host rejection");
    return 0;
}
